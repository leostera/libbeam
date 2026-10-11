/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Native loader-IR transformation tests, not executable opcode admission.
 */
#include "code_internal.h"
#include "binary_error.h"
#include <stdio.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"CHECK %s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while(0)
#define ARG(t,v) ((BeamOpArg){TAG_##t,(Sint)(v)})
typedef struct { size_t calls,fail,live,bytes; } Memory;
typedef union { max_align_t alignment; size_t size; } Header;
static void *allocate(void *context,size_t size)
{
    Memory *m=context; Header *h;
    if(++m->calls==m->fail) return NULL;
    h=malloc(sizeof(*h)+size); if(!h) return NULL;
    h->size=size; ++m->live; m->bytes+=size; return h+1;
}
static void release(void *context,void *pointer)
{
    Memory *m=context; Header *h=(Header *)pointer-1;
    CHECK(m->live && m->bytes>=h->size); --m->live; m->bytes-=h->size;
    memset(pointer,0xa5,h->size); free(h);
}
static LbBeamProgram *program(LbCodeSpace *space)
{
    void *memory; LbBeamProgram *p;
    CHECK(lb_alloc_domain_allocate(space->domain,sizeof(*p),&memory)==LB_ALLOC_OK);
    p=memory; memset(p,0,sizeof(*p)); p->domain=space->domain; p->atoms=space->atoms;
    return p;
}
static BeamOp input(BeamOpArg *a,Eterm kind)
{
    a[0]=ARG(p,0); a[1]=ARG(u,3); a[2]=ARG(u,2); a[3]=ARG(u,1); a[4]=ARG(x,0); a[5]=ARG(u,6);
    a[6]=ARG(a,kind); a[7]=ARG(u,1); a[8]=ARG(u,8); a[9]=ARG(n,NIL); a[10]=ARG(x,0); a[11]=ARG(x,1);
    return (BeamOp){.op=genop_bs_create_bin_6,.arity=12,.a=a};
}
static LoaderState state(LbCodeSpace *space,LbBeamProgram *p,BeamOp *op)
{
    return (LoaderState){.space=space,.program=p,.genop=op,.label_count=100};
}
static void check_result(BeamOp *op,Uint kind,Uint unit,Uint flags,BeamOpArg size)
{
    CHECK(op->op==genop_i_bs_create_bin_5 && op->arity==10 && op->a[4].type==TAG_u && op->a[4].val==5);
    CHECK(op->a[0].type==TAG_p && !op->a[0].val && op->a[1].val==3 && op->a[2].val==2 && op->a[3].type==TAG_x);
    CHECK(op->a[5].type==TAG_u && (Uint)op->a[5].val==kind && op->a[6].type==TAG_u && (Uint)op->a[6].val==unit);
    CHECK(op->a[7].type==TAG_u && (Uint)op->a[7].val==flags);
    CHECK(op->a[9].type==size.type && op->a[9].val==size.val);
}
static void branches(LbCodeSpace *space)
{
    const Eterm kinds[]={am_append,am_private_append,am_binary,am_binary,am_binary,
        am_float,am_float,am_integer,am_integer,am_utf8,am_utf16,am_utf32};
    const unsigned types[]={LB_BSC_APPEND,LB_BSC_PRIVATE_APPEND,LB_BSC_BINARY,LB_BSC_BINARY_FIXED_SIZE,LB_BSC_BINARY_ALL,
        LB_BSC_FLOAT,LB_BSC_FLOAT_FIXED_SIZE,LB_BSC_INTEGER,LB_BSC_INTEGER_FIXED_SIZE,LB_BSC_UTF8,LB_BSC_UTF16,LB_BSC_UTF32};
    for(size_t i=0;i<sizeof(kinds)/sizeof(kinds[0]);++i) {
        BeamOpArg a[12],before[12],size=ARG(x,1); BeamOp op=input(a,kinds[i]),tail={.op=genop_return_0};
        LbBeamProgram *p=program(space); LoaderState st=state(space,p,&op); Uint unit=8;
        op.next=&tail;
        if(i==3 || i==6 || i==8) { a[11]=ARG(i,2); size=ARG(u,16); unit=0; }
        if(i==4) size=a[11]=ARG(a,am_all);
        if(i>=9) { a[8]=ARG(u,0); unit=0; size=a[11]=ARG(a,am_undefined); }
        if(i==11) size=ARG(u,32);
        memcpy(before,a,sizeof(a)); CHECK(lb_transform(&st)==TE_OK && st.genop->next==&tail);
        CHECK(!memcmp(before,a,sizeof(a))); check_result(st.genop,types[i],unit,8,size);
        CHECK(st.genop->a[8].type==TAG_x && !st.genop->a[8].val);
        lb_beam_program_destroy(p);
    }
    /* Native multiplication overflow and the packed fixed-size 56-bit ceiling
     * are optimization fallbacks, not truncated field sizes or guard results. */
    const struct { Sint size; Uint unit; int fixed; } sizes[]={
        {0,256,1}, {(INT64_C(1)<<56)-1,1,1}, {INT64_C(1)<<55,2,0},
        {INT64_C(1)<<56,256,0}, {MAX_SMALL,256,0}, {-1,1,0}};
    for(size_t i=0;i<sizeof(sizes)/sizeof(sizes[0]);++i) {
        BeamOpArg a[12]; BeamOp op=input(a,am_integer); LbBeamProgram *p=program(space); LoaderState st=state(space,p,&op);
        a[8]=ARG(u,sizes[i].unit); a[11]=ARG(i,sizes[i].size);
        CHECK(lb_transform(&st)==TE_OK);
        check_result(st.genop,sizes[i].fixed?LB_BSC_INTEGER_FIXED_SIZE:LB_BSC_INTEGER,
                     sizes[i].fixed?0:sizes[i].unit,8,
                     sizes[i].fixed?ARG(u,(Uint)sizes[i].size*sizes[i].unit):a[11]);
        lb_beam_program_destroy(p);
    }
}
static void flags(LbCodeSpace *space)
{
    for(unsigned mode=0;mode<4;++mode) {
        Eterm cells[6],literals[1],items[]={am_signed,am_little,am_native}; Uint expected=0;
        BeamOpArg a[12]; BeamOp op=input(a,am_integer); LbBeamProgram *p=program(space); LoaderState st=state(space,p,&op);
        literals[0]=NIL;
        for(unsigned i=0;i<mode;++i) literals[0]=CONS(cells+2*i,items[i],literals[0]);
        p->literals=literals; p->literal_count=1; a[9]=ARG(q,0); a[7]=ARG(u,MAX_SMALL);
        if(mode>=2) expected=2;
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        if(mode==3) expected=0;
#endif
        CHECK(lb_transform(&st)==TE_OK);
        check_result(st.genop,LB_BSC_INTEGER,8,((Uint)MAX_SMALL<<3)|expected,ARG(x,1));
        lb_beam_program_destroy(p);
    }
}
static void refusals(Memory *m,LbCodeSpace *space)
{
    const struct { unsigned slot; int tag; Sint value; } bad[]={
        {0,TAG_f,0}, {0,TAG_f,100}, {1,TAG_u,-1}, {2,TAG_u,MAX_REG+1},
        {3,TAG_u,257}, {4,TAG_i,0}, {4,TAG_x,MAX_REG}, {5,TAG_u,0}, {5,TAG_u,7},
        {6,TAG_i,am_integer}, {6,TAG_a,am_none}, {7,TAG_u,MAX_SMALL+1}, {8,TAG_u,0},
        {8,TAG_u,257}, {9,TAG_a,am_native}, {9,TAG_q,99}, {10,TAG_x,MAX_REG},
        {10,TAG_u,0}, {11,TAG_u,8}, {11,TAG_q,99}};
    for(size_t i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
        BeamOpArg a[12],saved[12]; BeamOp op=input(a,am_integer); LbBeamProgram *p=program(space); LoaderState st=state(space,p,&op);
        a[bad[i].slot]=(BeamOpArg){bad[i].tag,bad[i].value}; memcpy(saved,a,sizeof(a)); size_t calls=m->calls;
        CHECK(lb_transform(&st)==TE_BAD_FORMAT && st.status==LB_CODE_FORMAT && st.genop==&op);
        CHECK(m->calls==calls && !memcmp(a,saved,sizeof(a))); lb_beam_program_destroy(p);
    }
    for(unsigned mode=0;mode<4;++mode) {
        BeamOpArg a[18]; BeamOp op=input(a,am_integer); LbBeamProgram *p=program(space); LoaderState st=state(space,p,&op);
        Eterm cells[2],literals[]={NIL}; size_t calls=m->calls;
        if(mode<2) {
            literals[0]=CONS(cells,mode?am_little:make_small(1),mode?make_small(0):NIL);
            p->literals=literals; p->literal_count=1; a[9]=ARG(q,0);
        } else if(mode==2) { a[6]=ARG(a,am_append); a[2]=ARG(u,MAX_REG); }
        else { memcpy(a+12,a+6,6*sizeof(*a)); a[5]=ARG(u,12); a[12]=ARG(a,am_private_append); op.arity=18; }
        CHECK(lb_transform(&st)==TE_BAD_FORMAT && st.status==LB_CODE_FORMAT && m->calls==calls);
        lb_beam_program_destroy(p);
    }
}
static void largest_descriptor(LbCodeSpace *space)
{
    size_t count=65532,arity=count+6; BeamOpArg *a=malloc(arity*sizeof(*a)); CHECK(a);
    BeamOp op=input(a,am_integer); LbBeamProgram *p=program(space); LoaderState st=state(space,p,&op);
    for(size_t i=12;i<arity;i+=6) memcpy(a+i,a+6,6*sizeof(*a));
    op.arity=(unsigned)arity; a[5]=ARG(u,count);
    CHECK(lb_transform(&st)==TE_OK && st.genop->arity==5+5*count/6);
    for(size_t i=5;i<st.genop->arity;i+=5) CHECK(st.genop->a[i].val==LB_BSC_INTEGER && st.genop->a[i+4].type==TAG_x);
    lb_beam_program_destroy(p); free(a);
}
static void failures(Memory *m,LbCodeSpace *space)
{
    size_t live=m->live,bytes=m->bytes;
    for(size_t fail=1;fail<=2;++fail) {
        BeamOpArg a[12],saved[12]; BeamOp op=input(a,am_integer); LbBeamProgram *p=program(space); LoaderState st=state(space,p,&op);
        memcpy(saved,a,sizeof(a)); m->fail=m->calls+fail;
        CHECK(lb_transform(&st)==TE_NO_MEMORY && p->error.status==LB_BEAM_NO_MEMORY && st.genop==&op && !memcmp(a,saved,sizeof(a)));
        m->fail=0; lb_beam_program_destroy(p); CHECK(m->live==live && m->bytes==bytes);
        op=input(a,am_integer); p=program(space); st=state(space,p,&op);
        CHECK(lb_transform(&st)==TE_OK); lb_beam_program_destroy(p); CHECK(m->live==live && m->bytes==bytes);
    }
}
static void fixture(Memory *m,LbCodeSpace *space,const char *path)
{
    FILE *file=fopen(path,"rb"); long size; unsigned char *data; LbBeamProgram *p; LbBeamError error;
    LbCodeModule *module=NULL; const BeamOp *source; size_t count=0,strings=0,live=m->live,bytes=m->bytes;
    CHECK(file && !fseek(file,0,SEEK_END)); size=ftell(file); CHECK(size>0 && !fseek(file,0,SEEK_SET));
    data=malloc((size_t)size); CHECK(data && fread(data,1,(size_t)size,file)==(size_t)size && !fclose(file));
    CHECK(lb_beam_program_prepare(space->domain,space->atoms,data,(size_t)size,&p,&error)==LB_BEAM_OK);
    for(source=p->ops;source;source=source->next) if(source->op==genop_bs_create_bin_6) {
        BeamOp op=*source; LoaderState st; BeamOpArg *copy=malloc(op.arity*sizeof(*copy)); CHECK(copy);
        memcpy(copy,source->a,op.arity*sizeof(*copy)); op.a=copy; op.next=NULL;
        for(size_t i=0;i<op.arity;++i) if(copy[i].type==TAG_x || copy[i].type==TAG_y) copy[i].val&=REG_MASK;
        if(copy[0].type==TAG_f && !copy[0].val) copy[0]=ARG(p,0);
        st=state(space,p,&op); st.label_count=lb_beam_image_info(p->image)->label_count;
        if(lb_transform(&st)!=TE_OK) {
            fprintf(stderr,"descriptor refusal at %zu\n",source->offset);
            for(size_t i=0;i<op.arity;++i) fprintf(stderr,"%zu: tag=%d value=%lld\n",i,copy[i].type,(long long)copy[i].val);
            exit(1);
        }
        CHECK(st.genop->arity==5+5*(source->arity-6)/6);
        for(size_t i=5;i<st.genop->arity;i+=5) if(st.genop->a[i].val==LB_BSC_STRING) ++strings;
        /* String offsets/sizes are checked against the retained StrT before
         * native code can build descriptors containing those offsets. */
        for(size_t i=6;i<source->arity;i+=6) if(copy[i].val==am_string) {
            BeamOp bad=*source; LoaderState invalid; BeamOpArg saved=copy[i+4]; size_t calls=m->calls;
            bad.a=copy; bad.next=NULL; copy[i+4]=ARG(u,SIZE_MAX);
            invalid=state(space,p,&bad); invalid.label_count=st.label_count;
            CHECK(lb_transform(&invalid)==TE_BAD_FORMAT && m->calls==calls); copy[i+4]=saved;
            saved=copy[i+5]; copy[i+5]=ARG(i,MAX_SMALL);
            invalid=state(space,p,&bad); invalid.label_count=st.label_count;
            CHECK(lb_transform(&invalid)==TE_BAD_FORMAT && m->calls==calls); copy[i+5]=saved;
        }
        ++count; free(copy);
    }
    CHECK(count>=5 && strings);
    LoaderState verify=state(space,p,p->ops); verify.label_count=lb_beam_image_info(p->image)->label_count;
    CHECK(lb_verify_program(&verify)==LB_CODE_UNSUPPORTED && !strcmp(p->error.stage,gen_opc[genop_bs_create_bin_6].name));
    lb_beam_program_destroy(p);
    LbCodeStatus status=lb_code_load(space,data,(size_t)size,&module,&error);
    /* The comprehension may also import erlang:error/1. Eager linking must
     * refuse that independently; the verifier check above isolates bs_create. */
    CHECK((status==LB_CODE_UNSUPPORTED || status==LB_CODE_UNRESOLVED) && !module && !space->modules);
    free(data);
    /* Preparation legitimately interns fixture atoms; complete space retirement
     * below checks that ownership, rather than pretending it never occurred. */
    CHECK(m->live>=live && m->bytes>=bytes);
    printf("CORE_BINARY_TRANSFORM_FIXTURE_OK operations=%zu strings=%zu native_generator=true execution_admission=false\n",count,strings);
}
int main(int argc,char **argv)
{
    Memory m={0}; LbSystemAllocator callbacks={&m,allocate,release}; LbEngine *engine; LbCodeSpace *space;
    CHECK(lb_engine_create(&callbacks,&engine)==LB_ENGINE_OK && lb_code_space_create(engine,&space)==LB_CODE_OK);
    size_t live=m.live,bytes=m.bytes;
    branches(space); flags(space); refusals(&m,space); largest_descriptor(space); failures(&m,space);
    CHECK(m.live==live && m.bytes==bytes && !lb_instruction_supported(op_i_bs_create_bin_jIWdW));
    if(argc==2) fixture(&m,space,argv[1]); else CHECK(argc==1);
    CHECK(lb_code_space_destroy(space)==LB_CODE_OK && lb_engine_shutdown(engine)==LB_ENGINE_OK);
    lb_engine_release(engine); CHECK(!m.live && !m.bytes);
    puts("CORE_BINARY_TRANSFORM_OK whole_native_generator=true checked_descriptors=true flags=true fixed_size_overflow_fallback=true maximum_descriptor=true failure_prefixes=2 same_space_retry=true physical_retirement=true opcode_admitted=false");
    return 0;
}
