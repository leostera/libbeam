/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Normalized IR proof tests, not binary instruction execution.
 */
#include "code_internal.h"
#include <stdio.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"CHECK %s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while(0)
#define U(v) ((BeamOpArg){TAG_u,(Sint)(v)})
#define I(v) ((BeamOpArg){TAG_i,(Sint)(v)})
#define X(v) ((BeamOpArg){TAG_x,(Sint)(v)})
#define Y(v) ((BeamOpArg){TAG_y,(Sint)(v)})
#define F(v) ((BeamOpArg){TAG_f,(Sint)(v)})
#define A(v) ((BeamOpArg){TAG_a,(Sint)(v)})
#define N ((BeamOpArg){TAG_n,NIL})
#define P ((BeamOpArg){TAG_p,0})
typedef struct { BeamOp ops[80]; BeamOpArg args[80][24]; size_t count; } Program;
typedef struct { size_t calls,fail,live; } Memory;
static void *allocate(void *context,size_t size)
{
    Memory *m=context; if(++m->calls==m->fail) return NULL;
    void *p=malloc(size); if(p) ++m->live; return p;
}
static void release(void *context,void *p) { Memory *m=context; CHECK(m->live); --m->live; free(p); }
static BeamOp *add(Program *p,unsigned opcode,const BeamOpArg *args,size_t count)
{
    CHECK(p->count<80 && count<=24); size_t index=p->count++; BeamOp *op=&p->ops[index];
    memset(op,0,sizeof(*op)); op->op=opcode; op->arity=(unsigned)count; op->a=p->args[index];
    if(count) memcpy(op->a,args,count*sizeof(*args));
    if(index) p->ops[index-1].next=op; return op;
}
#define ADD(p,op,...) add((p),genop_##op,(BeamOpArg[]){__VA_ARGS__},sizeof((BeamOpArg[]){__VA_ARGS__})/sizeof(BeamOpArg))
static void ret(Program *p) { add(p,genop_return_0,NULL,0); }
static void init(Program *p) { add(p,genop_bs_init_writable_0,NULL,0); }
static void begin(Program *p)
{
    memset(p,0,sizeof(*p)); ADD(p,int_func_start_5,U(1),U(0),A(am_erlang),A(am_binary),U(2)); ADD(p,label_1,U(2));
}
static BeamOp *binary(Program *p,Eterm kind,BeamOpArg src,BeamOpArg dst,BeamOpArg fail,size_t live)
{
    return ADD(p,bs_create_bin_6,fail,U(3),U(live),U(1),dst,U(12),
        A(kind),U(0),U(1),N,src,A(am_all),A(am_integer),U(1),U(1),N,I(7),I(8));
}
static LbCodeStatus verify(LbCodeSpace *space,Program *input,LbCodeStatus expected)
{
    LbBeamProgram p={.ops=input->ops}; LoaderState st={.space=space,.program=&p,.label_count=12};
    LbCodeStatus status=lb_verify_program(&st);
    if(status!=expected) fprintf(stderr,"status=%d expected=%d stage=%s\n",status,expected,p.error.stage);
    CHECK(status==expected);
    if(status==LB_CODE_OK) for(size_t i=0;i<input->count;++i) for(size_t j=0;j<input->ops[i].arity;++j) {
        BeamOpArg a=input->ops[i].a[j]; if(a.type==TAG_x || a.type==TAG_y) CHECK((Uint)a.val<MAX_REG);
    }
    return status;
}
static void live_and_destinations(LbCodeSpace *space)
{
    Program p;
    for(unsigned mode=0;mode<8;++mode) {
        begin(&p); BeamOp *op=binary(&p,am_binary,X(0),X(0),P,2);
        if(mode==1) op->a[2]=U(0);
        if(mode==2) { op->a[2]=U(1); op->a[10]=X(1); }
        if(mode==3) { op->a[2]=U(1); op->a[17]=X(1); }
        if(mode==4) op->a[2]=U(MAX_REG+1);
        if(mode==5) op->a[4]=Y(0);
        if(mode==6) op->a[5]=U(11);
        if(mode==7) { op->a[10]=X(1<<10); op->a[16]=X(0); }
        ret(&p); verify(space,&p,mode==0 || mode==7?LB_CODE_OK:LB_CODE_FORMAT);
    }
    for(unsigned complete=0;complete<2;++complete) {
        begin(&p); ADD(&p,allocate_2,U(2),U(2)); ADD(&p,move_2,N,Y(0));
        if(complete) ADD(&p,move_2,X(0),Y(1));
        binary(&p,am_binary,Y(1),Y(0),P,2); ADD(&p,move_2,Y(0),X(0)); ADD(&p,deallocate_1,U(2)); ret(&p);
        verify(space,&p,complete?LB_CODE_OK:LB_CODE_FORMAT);
    }
    begin(&p); binary(&p,am_binary,X(0),X(0),F(3),2);
    ADD(&p,put_tuple2_2,X(0),U(2),X(0),X(1)); ret(&p); ADD(&p,label_1,U(3)); ret(&p); verify(space,&p,LB_CODE_OK);
    begin(&p); binary(&p,am_binary,X(0),X(0),F(3),2); ret(&p); ADD(&p,label_1,U(3));
    ADD(&p,put_tuple2_2,X(0),U(2),X(0),X(1)); ret(&p); verify(space,&p,LB_CODE_FORMAT);
    /* Failure preserves an ordinary destination's incoming fact, not the
     * successful binary result or extra heap credit. */
    begin(&p); ADD(&p,is_nonempty_list_2,F(1),X(1)); binary(&p,am_binary,X(0),X(1),F(3),2); ret(&p);
    ADD(&p,label_1,U(3)); ADD(&p,get_hd_2,X(1),X(0)); ret(&p); verify(space,&p,LB_CODE_OK);
}
static void consumption(LbCodeSpace *space)
{
    Program p;
    for(unsigned mode=0;mode<7;++mode) {
        begin(&p); if(mode!=1) init(&p);
        BeamOp *op=binary(&p,am_private_append,X(0),X(mode==2?1:0),P,1);
        if(mode==2) ADD(&p,move_2,X(0),X(1));
        if(mode==3) op->a[16]=X(0);
        if(mode==4) op->a[17]=X(1<<10);
        if(mode==5) { op->a[10]=A(am_false); }
        if(mode==6) { op->a[2]=U(2); }
        ret(&p); verify(space,&p,mode?LB_CODE_FORMAT:LB_CODE_OK);
    }
    begin(&p); init(&p); binary(&p,am_private_append,X(0),X(1),P,1);
    ADD(&p,move_2,X(1),X(0)); ret(&p); verify(space,&p,LB_CODE_OK);
    /* Consumed Y is still a valid physical NIL root during a collection, but
     * must not become an available logical value after test_heap. */
    for(unsigned reuse=0;reuse<2;++reuse) {
        begin(&p); ADD(&p,allocate_2,U(1),U(2)); ADD(&p,move_2,N,Y(0)); init(&p);
        ADD(&p,move_2,X(0),Y(0)); ADD(&p,move_2,I(0),X(0));
        binary(&p,am_private_append,Y(0),X(0),P,1); ADD(&p,test_heap_2,U(0),U(1));
        if(reuse) ADD(&p,move_2,Y(0),X(0));
        ADD(&p,deallocate_1,U(1)); ret(&p); verify(space,&p,reuse?LB_CODE_FORMAT:LB_CODE_OK);
    }
    /* A consumed X cannot silently become a callee argument or a return. */
    begin(&p); init(&p); binary(&p,am_private_append,X(0),X(1),P,1);
    ADD(&p,call_only_2,U(1),F(8));
    ADD(&p,int_func_start_5,U(7),U(0),A(am_erlang),A(am_binary),U(1)); ADD(&p,label_1,U(8)); ret(&p);
    verify(space,&p,LB_CODE_FORMAT);
    for(unsigned reset=0;reset<2;++reset) {
        begin(&p); init(&p); binary(&p,am_private_append,X(0),X(0),F(3),1); ret(&p);
        ADD(&p,label_1,U(3)); if(reset) ADD(&p,move_2,N,X(0)); ret(&p);
        verify(space,&p,reset?LB_CODE_OK:LB_CODE_FORMAT);
    }
}
static void joins_and_revocation(LbCodeSpace *space)
{
    Program p;
    for(unsigned consume=0;consume<2;++consume) {
        begin(&p); init(&p); ADD(&p,is_nil_2,F(3),X(0));
        if(consume) binary(&p,am_private_append,X(0),X(1),P,1);
        ADD(&p,jump_1,F(4)); ADD(&p,label_1,U(3));
        for(unsigned i=0;i<12;++i) ADD(&p,move_2,X(0),X(0));
        ADD(&p,label_1,U(4)); ret(&p); verify(space,&p,consume?LB_CODE_FORMAT:LB_CODE_OK);
    }
    for(unsigned overwrite=0;overwrite<2;++overwrite) {
        begin(&p); init(&p); ADD(&p,label_1,U(3)); binary(&p,am_private_append,X(0),X(0),P,1);
        if(overwrite) ADD(&p,move_2,N,X(0));
        ADD(&p,jump_1,F(3)); verify(space,&p,overwrite?LB_CODE_FORMAT:LB_CODE_OK);
    }
    /* Normal append can revoke the old header. Only its new destination keeps
     * the append representation fact, and an arbitrary call loses Y facts. */
    for(unsigned old=0;old<2;++old) {
        begin(&p); init(&p); binary(&p,am_append,X(0),X(1),P,1);
        binary(&p,am_private_append,X(old?0:1),X(0),P,2); ret(&p);
        verify(space,&p,old?LB_CODE_FORMAT:LB_CODE_OK);
    }
    begin(&p); ADD(&p,allocate_2,U(1),U(2)); ADD(&p,move_2,N,Y(0)); init(&p); ADD(&p,move_2,X(0),Y(0));
    ADD(&p,call_2,U(1),F(8)); binary(&p,am_private_append,Y(0),X(0),P,1); ADD(&p,deallocate_1,U(1)); ret(&p);
    ADD(&p,int_func_start_5,U(7),U(0),A(am_erlang),A(am_binary),U(1)); ADD(&p,label_1,U(8)); ret(&p);
    verify(space,&p,LB_CODE_FORMAT);
    /* This proof is NOT uniqueness. Runtime checks must still refuse another
     * surviving reference, including one hidden in a constructed compound. */
    begin(&p); init(&p); ADD(&p,move_2,X(0),X(1)); binary(&p,am_private_append,X(0),X(0),P,2); ret(&p);
    verify(space,&p,LB_CODE_OK);
}
static void fixture(LbCodeSpace *space,const char *path)
{
    FILE *f=fopen(path,"rb"); CHECK(f && !fseek(f,0,SEEK_END)); long size=ftell(f); CHECK(size>0 && !fseek(f,0,SEEK_SET));
    unsigned char *data=malloc((size_t)size); CHECK(data && fread(data,1,(size_t)size,f)==(size_t)size && !fclose(f));
    LbBeamProgram *p; LbBeamError error; LbCodeModule *module=NULL;
    CHECK(lb_beam_program_prepare(space->domain,space->atoms,data,(size_t)size,&p,&error)==LB_BEAM_OK);
    const LbBeamImageInfo *info=lb_beam_image_info(p->image); BeamFile_ImportEntry imports[2]; CHECK(info->import_count==2);
    for(size_t i=0;i<2;++i) {
        LbBeamImport imp; CHECK(lb_beam_image_import(p->image,i,&imp)==LB_BEAM_OK);
        imports[i]=(BeamFile_ImportEntry){p->file_atoms[imp.module],p->file_atoms[imp.function],imp.arity};
    }
    LoaderState st={.space=space,.program=p,.label_count=info->label_count};
    st.beam.imports.count=2; st.beam.imports.entries=imports;
    CHECK(lb_verify_program(&st)==LB_CODE_OK); BeamOp *op=p->ops;
    while(op && op->op!=genop_bs_create_bin_6) op=op->next; CHECK(op);
    BeamOpArg saved=op->a[2]; op->a[2]=U(0); CHECK(lb_verify_program(&st)==LB_CODE_FORMAT); op->a[2]=saved;
    saved=op->a[4]; op->a[4]=Y(0); CHECK(lb_verify_program(&st)==LB_CODE_FORMAT); op->a[4]=saved;
    saved=op->a[6]; op->a[6]=A(am_private_append); CHECK(lb_verify_program(&st)==LB_CODE_FORMAT); op->a[6]=saved;
    CHECK(lb_verify_program(&st)==LB_CODE_OK); lb_beam_program_destroy(p);
    CHECK(lb_code_load(space,data,(size_t)size,&module,&error)==LB_CODE_UNSUPPORTED && !module && !space->modules); free(data);
    puts("CORE_BINARY_FLOW_FIXTURE_OK compiler_input=true proof_passes=true mutations=3 same_space_retry=true execution_still_refused=true");
}
int main(int argc,char **argv)
{
    Memory m={0}; LbSystemAllocator callbacks={&m,allocate,release}; LbEngine *engine; LbCodeSpace *space;
    CHECK(lb_engine_create(&callbacks,&engine)==LB_ENGINE_OK && lb_code_space_create(engine,&space)==LB_CODE_OK);
    live_and_destinations(space); consumption(space); joins_and_revocation(space);
    Program p; begin(&p); init(&p); binary(&p,am_private_append,X(0),X(0),P,1); ret(&p);
    size_t live=m.live; m.fail=m.calls+1; verify(space,&p,LB_CODE_NO_MEMORY); CHECK(m.live==live);
    m.fail=0; verify(space,&p,LB_CODE_OK); CHECK(m.live==live);
    if(argc==2) fixture(space,argv[1]); else CHECK(argc==1);
    CHECK(!lb_instruction_supported(op_i_bs_create_bin_jIWdW) && !lb_instruction_supported(op_bs_init_writable));
    CHECK(lb_code_space_destroy(space)==LB_CODE_OK && lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine); CHECK(!m.live);
    puts("CORE_BINARY_FLOW_OK live_roots=true late_destination=true private_representation=true consumed_slots=true failure_edges=true poisoned_joins=true backedges=true call_arguments=true revocation=true physical_y_roots=true runtime_uniqueness_still_required=true execution=false");
    return 0;
}
