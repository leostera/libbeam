/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Unpublished native-word emission/relocation components, not a second VM.
 */
#include "process_internal.h"
#include "binary_error.h"
#include "big.h"
#include "writable.h"
#include <stdio.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"CHECK %s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while(0)
#define ARG(t,v) ((BeamOpArg){TAG_##t,(Sint)(v)})
#define ROUNDS 8
#define OPERATIONS 32
typedef struct { size_t calls,fail,live,bytes; } Memory;
typedef union { max_align_t alignment; size_t size; } Header;
typedef struct {
    LbBeamProgram *program;
    LbCodeModule module;
    LoaderState loader;
    BeamOp *operations[OPERATIONS]; size_t count;
    size_t positions[ROUNDS*OPERATIONS];
} Emission;
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
static unsigned char *image(const char *path,size_t *size)
{
    FILE *f=fopen(path,"rb"); long n; unsigned char *data;
    CHECK(f && !fseek(f,0,SEEK_END)); n=ftell(f); CHECK(n>0 && !fseek(f,0,SEEK_SET));
    *size=(size_t)n; data=malloc(*size); CHECK(data && fread(data,1,*size,f)==*size && !fclose(f)); return data;
}
static void prepare(Emission *e,LbCodeSpace *space,const unsigned char *data,size_t size)
{
    LbBeamError error; const BeamOp *source; const LbBeamImageInfo *info;
    memset(e,0,sizeof(*e));
    CHECK(lb_beam_program_prepare(space->domain,space->atoms,data,size,&e->program,&error)==LB_BEAM_OK);
    info=lb_beam_image_info(e->program->image);
    for(source=e->program->ops;source;source=source->next) if(source->op==genop_bs_create_bin_6) {
        BeamOp op=*source; BeamOpArg *a=malloc(op.arity*sizeof(*a)); LoaderState transform={0}; CHECK(a);
        memcpy(a,source->a,op.arity*sizeof(*a)); op.a=a; op.next=NULL;
        for(size_t i=0;i<op.arity;++i) if(a[i].type==TAG_x || a[i].type==TAG_y) a[i].val&=REG_MASK;
        transform=(LoaderState){.space=space,.program=e->program,.genop=&op,.label_count=info->label_count};
        CHECK(lb_transform(&transform)==TE_OK && e->count<OPERATIONS);
        e->operations[e->count++]=transform.genop; free(a);
    }
    CHECK(e->count>=5 && info->label_count>2);
    /* Exercise both relative directions and the packed Y destination encoding.
     * This raw block is deliberately not a complete/public executable module. */
    e->operations[0]->a[0]=ARG(f,1); e->operations[1]->a[0]=ARG(f,2);
    e->operations[1]->a[3]=ARG(y,2);
    e->module=(LbCodeModule){.space=space,.program=e->program};
    e->loader=(LoaderState){.space=space,.program=e->program,.module_code=&e->module,
                           .label_count=info->label_count,.ci=info->function_count+1};
    e->loader.labels=lb_palloc(e->program,info->label_count,sizeof(Label)); CHECK(e->loader.labels);
}
static LbCodeStatus emit(Emission *e)
{
    LoaderState *st=&e->loader; size_t site=0; LbCodeStatus status;
    for(unsigned r=0;r<ROUNDS;++r) for(size_t i=0;i<e->count;++i) {
        LbBeamSelection selected; BeamOp *op=e->operations[i];
        CHECK(lb_beam_select_specific(op,&selected)==LB_BEAM_OK && selected.opcode==op_i_bs_create_bin_jIWdW);
        st->specific_op=(int)selected.opcode; e->positions[site++]=st->ci;
        status=lb_emit(st,op); if(status!=LB_CODE_OK) return status;
    }
    BeamOp ret={.op=genop_return_0,.arity=0}; LbBeamSelection selected; ret.a=ret.def_args;
    CHECK(lb_beam_select_specific(&ret,&selected)==LB_BEAM_OK); st->specific_op=(int)selected.opcode;
    status=lb_emit(st,&ret); if(status!=LB_CODE_OK) return status;
    st->labels[1].value=st->ci-1; st->labels[2].value=e->positions[0];
    return LB_CODE_OK;
}
static void check_words(Emission *e)
{
    LoaderState *st=&e->loader; size_t site=0,strings=0,literals=0; LbBeamBytes table;
    CHECK(st->capacity>256 && lb_beam_image_chunk(e->program->image,LB_BEAM_ID('S','t','r','T'),&table)==LB_BEAM_OK);
    for(unsigned r=0;r<ROUNDS;++r) for(size_t i=0;i<e->count;++i) {
        BeamOp *op=e->operations[i]; size_t position=e->positions[site++]; const BeamInstr *words=st->codev+position;
        Uint dst=(Uint)op->a[3].val,expected_dst=(dst+(op->a[3].type==TAG_y?CP_SIZE:0))*sizeof(Eterm)+(op->a[3].type==TAG_y);
        CHECK(words[0]==op_i_bs_create_bin_jIWdW && (words[2]&UINT32_MAX)==(Uint)op->a[1].val);
        CHECK((words[2]>>32)==expected_dst && words[3]==(Uint)op->a[2].val && words[4]==(Uint)op->a[4].val);
        if(i<2) CHECK((Sint)words[1]==(Sint)st->labels[i+1].value-(Sint)position);
        for(size_t j=5;j<op->arity;j+=5) {
            CHECK(words[j]==(Uint)op->a[j].val && words[j+1]==(Uint)op->a[j+1].val && words[j+2]==(Uint)op->a[j+2].val);
            if(words[j]==LB_BSC_STRING) {
                CHECK(words[j+3]==(Uint)(table.data+op->a[j+3].val) && words[j+4]==(Uint)op->a[j+4].val); ++strings;
            }
            for(size_t k=j+3;k<=j+4;++k) {
                BeamOpArg a=op->a[k]; Eterm term;
                if(a.type==TAG_q) {
                    CHECK(lb_beam_program_literal(e->program,e->loader.space->atoms,a.val,&term)==LB_BEAM_OK && words[k]==term); ++literals;
                } else if(a.type==TAG_x) CHECK(words[k]==make_loader_x_reg(a.val));
                else if(a.type==TAG_y) CHECK(words[k]==make_loader_y_reg(a.val+CP_SIZE));
            }
        }
    }
    CHECK(strings && literals && e->module.words==st->codev && e->module.word_count==st->ci && !e->module.published);
}
static void finish_refusals(LbCodeSpace *space,const unsigned char *data,size_t size)
{
    for(unsigned mode=0;mode<3;++mode) {
        Emission e; LbBeamBytes table; prepare(&e,space,data,size);
        CHECK(lb_beam_image_chunk(e.program->image,LB_BEAM_ID('S','t','r','T'),&table)==LB_BEAM_OK);
        for(size_t i=0;i<e.count;++i) for(size_t j=5;j<e.operations[i]->arity;j+=5) {
            BeamOpArg *a=e.operations[i]->a+j;
            if(a[0].val!=LB_BSC_STRING) continue;
            if(mode==0) a[3]=ARG(u,table.size+1);
            else if(mode==1) a[4]=ARG(u,SIZE_MAX);
            else { a[3]=ARG(u,table.size); a[4]=ARG(u,0); }
        }
        CHECK(emit(&e)==LB_CODE_OK); LbStringPatch *p=e.loader.string_patches; CHECK(p);
        CHECK(lb_finish_emit(&e.loader)==(mode==2?LB_CODE_OK:LB_CODE_FORMAT));
        if(mode==2) CHECK(e.loader.codev[p->pos]==(Uint)(table.data+table.size));
        else CHECK(!e.module.words && !e.module.word_count && !e.module.published);
        lb_beam_program_destroy(e.program);
    }
}
static size_t failures(Memory *m,LbCodeSpace *space,const unsigned char *data,size_t size)
{
    Emission e; size_t calls,live=m->live,bytes=m->bytes;
    prepare(&e,space,data,size); calls=m->calls; CHECK(emit(&e)==LB_CODE_OK); calls=m->calls-calls;
    CHECK(calls>ROUNDS*2 && lb_finish_emit(&e.loader)==LB_CODE_OK); check_words(&e);
    lb_beam_program_destroy(e.program); CHECK(m->live==live && m->bytes==bytes);
    for(size_t fail=1;fail<=calls;++fail) {
        prepare(&e,space,data,size); m->fail=m->calls+fail;
        CHECK(emit(&e)==LB_CODE_NO_MEMORY && !e.module.words && !e.module.word_count && !e.module.published);
        m->fail=0; lb_beam_program_destroy(e.program); CHECK(m->live==live && m->bytes==bytes);
        prepare(&e,space,data,size); CHECK(emit(&e)==LB_CODE_OK && lb_finish_emit(&e.loader)==LB_CODE_OK);
        check_words(&e); lb_beam_program_destroy(e.program); CHECK(m->live==live && m->bytes==bytes);
    }
    return calls;
}
static void consumer(LbCodeSpace *space,const unsigned char *data,size_t size,const char *identity_path)
{
    Emission e; size_t binary_size; unsigned char *binary=image(identity_path,&binary_size);
    LbCodeModule *module; LbCodeEntry *identity; LbBeamError error; Eterm function,nil=NIL,literal;
    LbProcess *p; LbBitWriter writer; LbBitstringView view; unsigned char expected[256];
    CHECK(lb_code_load(space,binary,binary_size,&module,&error)==LB_CODE_OK); free(binary);
    CHECK(lb_atoms_find(space->atoms,"identity",8,&function)==LB_ATOM_OK);
    CHECK(lb_code_entry_acquire(space,lb_code_module_name(module),function,1,&identity)==LB_CODE_OK);
    unsigned char *caller_copy=malloc(size); CHECK(caller_copy); memcpy(caller_copy,data,size);
    prepare(&e,space,caller_copy,size); memset(caller_copy,0xa5,size); free(caller_copy);
    CHECK(emit(&e)==LB_CODE_OK && lb_finish_emit(&e.loader)==LB_CODE_OK);
    LbStringPatch *string=e.loader.string_patches; LbPatch *constant=e.loader.literal_patches;
    CHECK(string && constant && string->bytes+9<=sizeof(expected));
    literal=e.loader.codev[constant->pos]; CHECK(lb_is_big(literal));
    size_t bytes=string->bytes+9; memcpy(expected,(void *)e.loader.codev[string->pos],string->bytes);
    memset(expected+string->bytes,0,9); expected[bytes-1]=1; /* 2^64, 72-bit LE */
    CHECK(lb_process_create(identity,&nil,1,5,&p)==LB_PROCESS_READY);
    CHECK(lb_binary_begin(p,bytes*8,0,&writer,&p->reg[0])==LB_WRITABLE_OK);
    CHECK(lb_bits_put_string(&writer,(void *)e.loader.codev[string->pos],string->bytes)==LB_BITS_OK);
    CHECK(lb_bits_put_integer(&writer,literal,72,LB_BITS_LITTLE_ENDIAN)==LB_BITS_OK && writer.offset==bytes*8);
    lb_beam_program_destroy(e.program); /* output retains neither code nor literal/string pointers */
    LbProcessStatus status=LB_PROCESS_YIELDED; size_t steps=0;
    while(status==LB_PROCESS_YIELDED && steps++<1000) {
        status=lb_process_run(p,1,100); CHECK(lb_process_collect(p,0)==status);
    }
    CHECK(status==LB_PROCESS_DONE && lb_bitstring_view(p->reg[0],&view) && view.bit_size==bytes*8 && !memcmp(view.data,expected,bytes));
    lb_process_destroy(p); lb_code_entry_release(identity); CHECK(lb_code_unload(module)==LB_CODE_OK);
}
static void components(LbCodeSpace *space)
{
    /* Check packed fields without asserting this unpublished word block is
     * executable code. No program image or fabricated runtime is required. */
    void *memory; CHECK(lb_alloc_domain_allocate(space->domain,sizeof(LbBeamProgram),&memory)==LB_ALLOC_OK);
    LbBeamProgram *p=memory; memset(p,0,sizeof(*p)); p->domain=space->domain; p->atoms=space->atoms;
    BeamOpArg a[]={ARG(p,0),ARG(u,UINT32_MAX),ARG(u,2),ARG(y,2),ARG(u,5),
        ARG(u,LB_BSC_INTEGER),ARG(u,1),ARG(u,8),ARG(y,1),ARG(x,0)};
    BeamOp op={.op=genop_i_bs_create_bin_5,.arity=10,.a=a};
    LoaderState st={.space=space,.program=p,.specific_op=op_i_bs_create_bin_jIWdW};
    CHECK(lb_emit(&st,&op)==LB_CODE_OK && st.ci==10);
    CHECK(st.codev[2]==(((Uint)(3*sizeof(Eterm)+1)<<32)|UINT32_MAX));
    CHECK(st.codev[8]==make_loader_y_reg(2) && st.codev[9]==make_loader_x_reg(0));
    size_t before=st.ci; a[4]=ARG(u,6); CHECK(lb_emit(&st,&op)==LB_CODE_FORMAT && st.ci==before);
    a[4]=ARG(u,5); a[5]=ARG(u,99); CHECK(lb_emit(&st,&op)==LB_CODE_FORMAT && st.ci==before);
    a[5]=ARG(u,LB_BSC_STRING); CHECK(lb_emit(&st,&op)==LB_CODE_FORMAT && st.ci==before);
    /* The existing unadmitted match-string emitter uses bit counts rather
     * than byte counts; packing must not change which field is rounded. */
    const size_t lengths[]={0,1,7,8,9,SIZE_MAX};
    st.labels=lb_palloc(p,2,sizeof(Label)); CHECK(st.labels); st.label_count=2;
    for(size_t i=0;i<sizeof(lengths)/sizeof(*lengths);++i) {
        BeamOpArg match_args[]={ARG(x,0),ARG(f,1),ARG(u,lengths[i]),ARG(u,0)};
        BeamOp match={.op=genop_i_bs_match_string_4,.arity=4,.a=match_args};
        st.specific_op=op_i_bs_match_string_xfWW;
        CHECK(lb_emit(&st,&match)==LB_CODE_OK && st.string_patches->pos==st.ci-1);
        CHECK(st.string_patches->bytes==lengths[i]/8+(lengths[i]%8!=0));
        CHECK(!lb_instruction_supported(op_i_bs_match_string_xfWW));
    }
    lb_beam_program_destroy(p);
}
int main(int argc,char **argv)
{
    Memory m={0}; LbSystemAllocator callbacks={&m,allocate,release}; LbEngine *engine; LbCodeSpace *space;
    CHECK(lb_engine_create(&callbacks,&engine)==LB_ENGINE_OK && lb_code_space_create(engine,&space)==LB_CODE_OK);
    components(space); CHECK(!lb_instruction_supported(op_i_bs_create_bin_jIWdW));
    if(argc==3) {
        size_t size; unsigned char *data=image(argv[1],&size); Emission warmup;
        prepare(&warmup,space,data,size); lb_beam_program_destroy(warmup.program); /* own fixture atoms */
        size_t prefixes=failures(&m,space,data,size); finish_refusals(space,data,size);
        consumer(space,data,size,argv[2]); free(data);
        printf("CORE_BINARY_EMIT_FIXTURE_OK failure_prefixes=%zu code_growth=true relative_labels=true packed_y=true literal_string_fixups=true whole_span_bounds=true empty_end_span=true owned_consumer=true identity_execution=true moving_gc=true code_admission=false\n",prefixes);
    } else CHECK(argc==1);
    CHECK(lb_code_space_destroy(space)==LB_CODE_OK && lb_engine_shutdown(engine)==LB_ENGINE_OK);
    lb_engine_release(engine); CHECK(!m.live && !m.bytes);
    puts("CORE_BINARY_EMIT_OK native_words=true descriptor_count=true packed_fields=true malformed_refusal=true physical_retirement=true opcode_admitted=false");
    return 0;
}
