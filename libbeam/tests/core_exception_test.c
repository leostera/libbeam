/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#include "process_internal.h"
#include "exception.h"
#include "binary_error.h"
#include "term_compare.h"
#include <stdio.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"CHECK %s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while(0)
static const Uint codes[]={LB_EXC_ERROR,LB_EXC_NORMAL,LB_EXC_INTERNAL_ERROR,LB_EXC_BADARG,LB_EXC_BADARITH,
    LB_EXC_BADMATCH,LB_EXC_FUNCTION_CLAUSE,LB_EXC_CASE_CLAUSE,LB_EXC_IF_CLAUSE,LB_EXC_UNDEF,
    LB_EXC_BADFUN,LB_EXC_BADARITY,LB_EXC_TIMEOUT_VALUE,LB_EXC_NOPROC,LB_EXC_NOTALIVE,
    LB_EXC_SYSTEM_LIMIT,LB_EXC_TRY_CLAUSE,LB_EXC_NOTSUP,LB_EXC_BADMAP,LB_EXC_BADKEY,
    LB_EXC_BADRECORD,LB_EXC_BADFIELD,LB_EXC_NOVALUE};
static const Eterm atoms[]={am_internal_error,am_normal,am_internal_error,am_badarg,am_badarith,
    am_badmatch,am_function_clause,am_case_clause,am_if_clause,am_undef,am_badfun,am_badarity,
    am_timeout_value,am_noproc,am_notalive,am_system_limit,am_try_clause,am_notsup,am_badmap,
    am_badkey,am_badrecord,am_badfield,am_novalue};
static int composite(size_t index)
{
    return index==5 || index==7 || index==10 || index==11 || index==16 || index>=18;
}
static void encodings(void)
{
    for(size_t i=0;i<LB_NUMBER_EXIT_CODES;++i) {
        CHECK(lb_exception_code_valid(codes[i]) && LB_GET_EXC_INDEX(codes[i])==i);
        CHECK(lb_exception_class(codes[i])==(i==1?am_exit:am_error));
        CHECK(lb_exception_atom(codes[i])==(i?atoms[i]:THE_NON_VALUE));
        for(unsigned flags=0;flags<256;++flags) {
            Uint code=codes[i]|((Uint)flags<<LB_EXF_OFFSET);
            CHECK(lb_exception_code_valid(code) && LB_GET_EXC_INDEX(code)==i);
        }
    }
    CHECK(lb_exception_class(LB_EXC_THROWN)==am_throw);
    const Uint invalid[]={LB_EXC_NULL,LB_TRAP,LB_EXC_BADARG|3,(Uint)1<<63,23u<<LB_EXC_OFFSET};
    for(size_t i=0;i<sizeof(invalid)/sizeof(*invalid);++i) {
        CHECK(!lb_exception_code_valid(invalid[i]) && is_non_value(lb_exception_atom(invalid[i])) && is_non_value(lb_exception_class(invalid[i])));
    }
}
typedef struct { size_t calls,fail,live; } Memory;
static void *allocate(void *context,size_t size)
{
    Memory *m=context; if(++m->calls==m->fail) return NULL;
    void *p=malloc(size); if(p) ++m->live; return p;
}
static void release(void *context,void *p) { Memory *m=context; CHECK(m->live); --m->live; free(p); }
static LbCodeEntry *entry(LbCodeSpace *space,LbCodeModule *module,const char *name)
{
    Eterm function; LbCodeEntry *result;
    CHECK(lb_atoms_find(space->atoms,name,strlen(name),&function)==LB_ATOM_OK);
    CHECK(lb_code_entry_acquire(space,lb_code_module_name(module),function,1,&result)==LB_CODE_OK); return result;
}
static LbProcess *process(LbCodeEntry *entry)
{
    unsigned char bytes[65]; memset(bytes,0x57,sizeof(bytes)); LbProcess *p;
    CHECK(lb_process_create_binary(entry,bytes,sizeof(bytes),32,&p)==LB_PROCESS_READY); return p;
}
static LbProcessStatus run(LbProcess *p)
{
    LbProcessStatus status=LB_PROCESS_YIELDED; size_t steps=0;
    while(status==LB_PROCESS_YIELDED && steps++<1000) status=lb_process_run(p,1,100);
    return status;
}
static void fill(LbProcess *p,size_t keep)
{
    while((size_t)(p->stop-p->htop)>=keep+2) { p->htop[0]=make_small(0); p->htop[1]=NIL; p->htop+=2; }
}
static void expansion(LbCodeEntry *identity,Memory *m)
{
    for(size_t i=0;i<LB_NUMBER_EXIT_CODES;++i) {
        LbProcess *p=process(identity); Eterm value=p->reg[0],out=make_small(17); size_t calls=m->calls;
        p->freason=LB_EXC_BADARG; p->fvalue=make_small(9); p->heap_reserved=3;
        if(i==2) CHECK(lb_exception_expand(p,codes[i],value,&out)==LB_ALLOC_INVALID && out==make_small(17) && m->calls==calls);
        else {
            CHECK(lb_exception_expand(p,codes[i],value,&out)==LB_ALLOC_OK);
            if(composite(i)) CHECK(is_tuple(out) && arityval(*boxed_val(out))==2 && boxed_val(out)[1]==atoms[i] && boxed_val(out)[2]==value);
            else CHECK(out==(i?atoms[i]:value) && m->calls==calls);
        }
        CHECK(p->freason==LB_EXC_BADARG && p->fvalue==make_small(9) && p->heap_reserved==3);
        lb_process_destroy(p);
    }
    for(unsigned mode=0;mode<2;++mode) {
        LbProcess *p=process(identity); Eterm out=make_small(17),value=p->reg[0]; size_t before,live;
        p->reg[0]=NIL; p->heap_reserved=3;
        if(mode) { p->freason=LB_EXC_CASE_CLAUSE; p->fvalue=make_small(9); }
        fill(p,3); before=p->collections; live=m->live;
        m->fail=m->calls+1;
        LbAllocStatus status=mode?lb_binary_error_prepare(p,LB_BSC_BINARY_ALL,1,LB_EXC_BADARG,am_type,value):
                                 lb_exception_expand(p,LB_EXC_BADMATCH,value,&out);
        CHECK(status==LB_ALLOC_NO_MEMORY && p->freason==(mode?LB_EXC_CASE_CLAUSE:LB_EXC_NULL) &&
              p->fvalue==(mode?make_small(9):NIL) && out==make_small(17));
        CHECK(p->collections==before && m->live==live && p->construction_root==NIL); m->fail=0;
        status=mode?lb_binary_error_prepare(p,LB_BSC_BINARY_ALL,1,LB_EXC_BADARG,am_type,value):
                    lb_exception_expand(p,LB_EXC_BADMATCH,value,&out);
        CHECK(status==LB_ALLOC_OK && p->collections>before && p->heap_reserved==3);
        if(mode) {
            CHECK(p->freason==(LB_EXC_BADARG|LB_EXF_HAS_EXT_INFO));
            Eterm cause=boxed_val(p->fvalue)[3]; out=boxed_val(cause)[4];
        } else out=boxed_val(out)[2];
        LbBitstringView view; CHECK(lb_bitstring_view(out,&view) && view.bit_size==520 && view.data[64]==0x57);
        lb_process_destroy(p);
    }
    LbProcess *p=process(identity); Eterm source=p->reg[0]; size_t calls=m->calls;
    p->freason=LB_EXC_CASE_CLAUSE; p->fvalue=source;
    CHECK(lb_binary_error_prepare(p,LB_BSC_INTEGER,1,LB_EXC_BADARG|LB_EXF_HAS_EXT_INFO,am_size,source)==LB_ALLOC_INVALID);
    CHECK(m->calls==calls && p->freason==LB_EXC_CASE_CLAUSE && p->fvalue==source);
    /* Codes with list/boxed low tags must never be traversed as term roots. */
    const Uint scalar_codes[]={LB_EXC_EXIT,LB_EXC_THROWN,LB_EXC_BADARG|LB_EXF_HAS_EXT_INFO};
    for(size_t i=0;i<3;++i) {
        p->freason=scalar_codes[i]; CHECK(lb_process_collect(p,0)==LB_PROCESS_READY && p->freason==scalar_codes[i]);
        CHECK(p->fvalue==p->reg[0]);
    }
    lb_process_destroy(p);
}
static void actual_errors(LbCodeSpace *space,LbCodeModule *module,LbCodeEntry *identity,Memory *m,int vectors)
{
    const char *names[]={"badmatch","case_clause","function_clause","bif"};
    const Eterm reasons[]={am_badmatch,am_case_clause,am_function_clause,am_badarg};
    for(size_t i=0;i<4;++i) {
        LbCodeEntry *code=entry(space,module,names[i]); LbProcess *p=process(code),*peer; Eterm nil=NIL;
        CHECK(run(p)==LB_PROCESS_EXCEPTION && lb_process_exception(p)==reasons[i]);
        CHECK(lb_exception_code_valid(p->freason) && (p->freason&LB_EXF_SAVETRACE) && p->fvalue==NIL);
        for(size_t x=0;x<MAX_REG;++x) p->reg[x]=NIL;
        CHECK(lb_process_collect(p,0)==LB_PROCESS_EXCEPTION); Eterm value=lb_process_exception_value(p);
        if(i<2) {
            CHECK(is_tuple(value) && boxed_val(value)[1]==reasons[i]); LbBitstringView view;
            CHECK(lb_bitstring_view(boxed_val(value)[2],&view) && view.bit_size==520);
            for(size_t byte=0;byte<65;++byte) CHECK(view.data[byte]==0x57);
            if(vectors) printf("%s:%s:65:87\n",names[i],names[i]);
        } else {
            CHECK(value==reasons[i]); if(vectors) printf("%s:%s\n",names[i],i==2?"function_clause":"badarg");
        }
        CHECK(is_non_value(lb_process_exception_info(p)));
        size_t words; CHECK(lb_flat_size(space->domain,value,&words));
        CHECK(lb_process_create(identity,&nil,1,words+16,&peer)==LB_PROCESS_READY);
        peer->reg[0]=lb_copy_flat(value,&peer->htop,&peer->off_heap); CHECK(!is_non_value(peer->reg[0]));
        int equal=0; CHECK(lb_term_equal(space->domain,value,peer->reg[0],&equal)==LB_ALLOC_OK && equal);
        lb_process_destroy(p); CHECK(run(peer)==LB_PROCESS_DONE && lb_process_collect(peer,0)==LB_PROCESS_DONE);
        CHECK(i<2?boxed_val(peer->reg[0])[1]==reasons[i]:peer->reg[0]==reasons[i]); lb_process_destroy(peer);
        if(i<2) {
            p=process(code); size_t steps=0;
            while(p->i[0]!=(i?op_case_end_x:op_badmatch_x) && steps++<100) CHECK(lb_process_run(p,1,100)==LB_PROCESS_YIELDED);
            CHECK(steps<100); fill(p,0); size_t live=m->live; m->fail=m->calls+1;
            CHECK(lb_process_run(p,1,100)==LB_PROCESS_NO_MEMORY && p->exception_value==NIL && m->live==live);
            CHECK(is_non_value(lb_process_exception(p)) && is_non_value(lb_process_exception_value(p)));
            m->fail=0; lb_process_destroy(p); p=process(code); CHECK(run(p)==LB_PROCESS_EXCEPTION); lb_process_destroy(p);
        }
        lb_code_entry_release(code);
    }
    /* Prepared extended data must not leak into a subsequent unrelated error. */
    LbCodeEntry *bif=entry(space,module,"bif"); LbProcess *p=process(bif);
    CHECK(lb_binary_error_prepare(p,LB_BSC_INTEGER,4,LB_EXC_SYSTEM_LIMIT,am_size,p->reg[0])==LB_ALLOC_OK);
    CHECK(is_non_value(lb_process_exception_info(p))); p->reg[0]=NIL;
    CHECK(run(p)==LB_PROCESS_EXCEPTION && p->freason==LB_EXC_BADARG && p->fvalue==NIL);
    CHECK(lb_process_exception_value(p)==am_badarg && is_non_value(lb_process_exception_info(p)));
    lb_process_destroy(p); lb_code_entry_release(bif);
}
static void execution(const char *path,int vectors)
{
    FILE *f=fopen(path,"rb"); CHECK(f && !fseek(f,0,SEEK_END)); long size=ftell(f); CHECK(size>0 && !fseek(f,0,SEEK_SET));
    unsigned char *data=malloc((size_t)size); CHECK(data && fread(data,1,(size_t)size,f)==(size_t)size && !fclose(f));
    Memory m={0}; LbSystemAllocator callbacks={&m,allocate,release}; LbEngine *engine; LbCodeSpace *space;
    LbCodeModule *module; LbBeamError error;
    CHECK(lb_engine_create(&callbacks,&engine)==LB_ENGINE_OK && lb_code_space_create(engine,&space)==LB_CODE_OK);
    CHECK(lb_code_load(space,data,(size_t)size,&module,&error)==LB_CODE_OK); memset(data,0xa5,(size_t)size); free(data);
    LbCodeEntry *identity=entry(space,module,"identity"); expansion(identity,&m); actual_errors(space,module,identity,&m,vectors);
    lb_code_entry_release(identity); CHECK(lb_code_unload(module)==LB_CODE_OK);
    CHECK(lb_code_space_destroy(space)==LB_CODE_OK && lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine); CHECK(!m.live);
    if(!vectors) puts("CORE_EXCEPTION_EXECUTION_OK encoded_reasons=true scalar_not_gc_root=true native_expansion=true real_instruction_errors=true error_root_gc=true copied_lifetime=true failure_retry=true binary_info_atomic=true stale_info_cleared=true exact_retirement=true stacktrace=false catch=false");
}
int main(int argc,char **argv)
{
    encodings();
    if(argc==3 && !strcmp(argv[1],"--reference")) { execution(argv[2],1); return 0; }
    if(argc==2) execution(argv[1],0); else CHECK(argc==1);
    puts("CORE_EXCEPTION_CODES_OK native_fields=true classes=3 shorthand_codes=23 invalid_refused=true identities_not_authority=true"); return 0;
}
