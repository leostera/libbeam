/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Normalized-operation unit tests for the proof boundary, not an executor.
 * Actual compiler output and malformed image admission live in core_shape_test.
 */
#include "code_internal.h"
#include <stdio.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"CHECK %s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while(0)
#define U(v) {TAG_u,(v)}
#define X(v) {TAG_x,(v)}
#define Y(v) {TAG_y,(v)}
#define F(v) {TAG_f,(v)}
#define A(v) {TAG_a,make_atom(v)}
#define N {TAG_n,0}
typedef struct { BeamOp ops[64]; size_t count; } Program;
static void add(Program *p,unsigned opcode,const BeamOpArg *args,size_t arity)
{
    BeamOp *op;
    CHECK(p->count<64); op=&p->ops[p->count++]; memset(op,0,sizeof(*op));
    CHECK(arity<=sizeof(op->def_args)/sizeof(op->def_args[0]));
    op->op=opcode; op->arity=(unsigned)arity; op->a=op->def_args;
    if(arity) memcpy(op->a,args,arity*sizeof(*args));
    if(p->count>1) p->ops[p->count-2].next=op;
}
#define ADD(p,op,...) add((p),genop_##op,(BeamOpArg[]){__VA_ARGS__},sizeof((BeamOpArg[]){__VA_ARGS__})/sizeof(BeamOpArg))
static void ret(Program *p) { add(p,genop_return_0,NULL,0); }
static void begin(Program *p)
{
    memset(p,0,sizeof(*p));
    ADD(p,int_func_start_5,U(1),U(0),A(0),A(1),U(2)); ADD(p,label_1,U(2));
}
static void verify(LbCodeSpace *space,Program *input,LbCodeStatus expected)
{
    LbBeamProgram program={0}; LoaderState st={0}; LbCodeStatus status;
    program.ops=input->ops; st.program=&program; st.space=space; st.label_count=10;
    status=lb_verify_program(&st);
    if(status!=expected) fprintf(stderr,"status=%d expected=%d stage=%s\n",status,expected,program.error.stage);
    CHECK(status==expected);
    if(status==LB_CODE_OK) {
        BeamOp *op; size_t arg;
        for(op=program.ops;op;op=op->next) for(arg=0;arg<op->arity;++arg)
            if(op->a[arg].type==TAG_x || op->a[arg].type==TAG_y) CHECK((Uint)op->a[arg].val<MAX_REG);
    }
}
static void joins(LbCodeSpace *space)
{
    Program p; unsigned mode,i;
    for(mode=0;mode<2;++mode) {
        begin(&p); ADD(&p,is_nil_2,F(3),X(1)); ADD(&p,is_nonempty_list_2,F(1),X(0)); ADD(&p,jump_1,F(4));
        ADD(&p,label_1,U(3));
        /* Delay the weak predecessor until after the strong path was visited. */
        for(i=0;i<12;++i) ADD(&p,move_2,X(1),X(1));
        if(mode) ADD(&p,is_nonempty_list_2,F(1),X(0));
        ADD(&p,label_1,U(4)); ADD(&p,get_hd_2,X(0),X(0)); ret(&p);
        verify(space,&p,mode?LB_CODE_OK:LB_CODE_FORMAT);
    }
    for(mode=0;mode<2;++mode) {
        begin(&p); ADD(&p,is_tuple_2,F(1),X(0)); ADD(&p,is_nil_2,F(3),X(1));
        ADD(&p,test_arity_3,F(1),X(0),U(3)); ADD(&p,jump_1,F(4)); ADD(&p,label_1,U(3));
        for(i=0;i<12;++i) ADD(&p,move_2,X(1),X(1));
        ADD(&p,test_arity_3,F(1),X(0),U(2)); ADD(&p,label_1,U(4));
        ADD(&p,get_tuple_element_3,X(0),U(mode?2:1),X(0)); ret(&p);
        verify(space,&p,mode?LB_CODE_FORMAT:LB_CODE_OK);
    }
    for(mode=0;mode<2;++mode) {
        begin(&p); ADD(&p,is_nonempty_list_2,F(1),X(0)); ADD(&p,label_1,U(3));
        ADD(&p,get_hd_2,X(0),X(1));
        if(mode) ADD(&p,move_2,N,X(0));
        ADD(&p,jump_1,F(3)); verify(space,&p,mode?LB_CODE_FORMAT:LB_CODE_OK);
    }
}
static void transfers(LbCodeSpace *space)
{
    Program p; unsigned mode;
    begin(&p); ADD(&p,is_nonempty_list_2,F(3),X(0)); ret(&p); ADD(&p,label_1,U(3));
    ADD(&p,get_hd_2,X(0),X(0)); ret(&p); verify(space,&p,LB_CODE_FORMAT);
    for(mode=0;mode<2;++mode) {
        begin(&p); ADD(&p,is_nonempty_list_2,F(1),X(0));
        if(mode) ADD(&p,move_2,N,X(0)); else ADD(&p,move_2,X(0),X(0));
        ADD(&p,get_hd_2,X(0),X(0)); ret(&p); verify(space,&p,mode?LB_CODE_FORMAT:LB_CODE_OK);
        begin(&p); ADD(&p,is_nonempty_list_2,F(1),X(0)); ADD(&p,swap_2,X(0),X(1));
        ADD(&p,get_hd_2,X(mode?0:1),X(0)); ret(&p); verify(space,&p,mode?LB_CODE_FORMAT:LB_CODE_OK);
        begin(&p); ADD(&p,test_heap_2,U(3),U(2));
        ADD(&p,put_tuple2_2,X(0),U(2),X(0),X(1)); ADD(&p,get_tuple_element_3,X(0),U(mode?2:1),X(0)); ret(&p);
        verify(space,&p,mode?LB_CODE_FORMAT:LB_CODE_OK);
    }
    begin(&p); ADD(&p,test_heap_2,U(2),U(2)); ADD(&p,put_list_3,X(0),X(1),X(0));
    ADD(&p,get_tl_2,X(0),X(0)); ret(&p); verify(space,&p,LB_CODE_OK);
    /* Without hint removal, native succ() could fuse these destinations into
     * x1023 and the out-of-bounds x1024, instead of distinct x1023 and x0. */
    begin(&p); ADD(&p,test_heap_2,U(3),U(2)); ADD(&p,put_tuple2_2,X(1),U(2),X(0),X(0));
    ADD(&p,get_tuple_element_3,X(1),U(0),X(1023)); ADD(&p,get_tuple_element_3,X(1),U(1),X(1<<10)); ret(&p);
    verify(space,&p,LB_CODE_OK); CHECK(p.ops[4].a[2].val==1023 && p.ops[5].a[2].val==0);
    /* Call results are unknown, while an initialized Y root retains its fact. */
    for(mode=0;mode<2;++mode) {
        begin(&p); ADD(&p,is_nonempty_list_2,F(1),X(0)); ADD(&p,allocate_2,U(1),U(2));
        ADD(&p,move_2,X(0),Y(0)); ADD(&p,call_2,U(0),F(8));
        if(mode) ADD(&p,get_hd_2,X(0),X(0)); else ADD(&p,get_hd_2,Y(0),X(0));
        ADD(&p,deallocate_1,U(1)); ret(&p);
        ADD(&p,int_func_start_5,U(7),U(0),A(0),A(0),U(0)); ADD(&p,label_1,U(8));
        ADD(&p,move_2,N,X(0)); ret(&p); verify(space,&p,mode?LB_CODE_FORMAT:LB_CODE_OK);
    }
}
int main(void)
{
    LbEngine *engine; LbCodeSpace *space;
    CHECK(lb_engine_create(NULL,&engine)==LB_ENGINE_OK && lb_code_space_create(engine,&space)==LB_CODE_OK);
    joins(space); transfers(space);
    CHECK(lb_code_space_destroy(space)==LB_CODE_OK && lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine);
    puts("CORE_SHAPE_PROOF_OK failure_edges=true late_joins=true backedges=true bounds=true alias_moves=true call_clobbers=true y_facts=true hints_not_authority=true execution=false");
    return 0;
}
