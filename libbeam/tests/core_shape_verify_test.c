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
static void verify_using_bif(LbCodeSpace *space,Program *input,LbCodeStatus expected,Eterm *literals,size_t count,unsigned id)
{
    LbBeamProgram program={0}; LoaderState st={0}; LbCodeStatus status;
    const BifEntry *bif=&space->engine->bifs[id];
    BeamFile_ImportEntry import={bif->module,bif->name,(unsigned)bif->arity};
    LbCodeModule module={0}; Export entry={0}; Export *imports[]={&entry};
    module.imports=imports; st.module_code=&module;
    program.ops=input->ops; program.atoms=space->atoms; program.literals=literals; program.literal_count=count;
    st.program=&program; st.space=space; st.label_count=10;
    st.beam.imports.count=1; st.beam.imports.entries=&import; st.bif_imports=&bif;
    status=lb_verify_program(&st);
    if(status!=expected) fprintf(stderr,"status=%d expected=%d stage=%s\n",status,expected,program.error.stage);
    CHECK(status==expected);
    if(status==LB_CODE_OK) {
        BeamOp *op; size_t arg;
        for(op=program.ops;op;op=op->next) for(arg=0;arg<op->arity;++arg)
            if(op->a[arg].type==TAG_x || op->a[arg].type==TAG_y) CHECK((Uint)op->a[arg].val<MAX_REG);
    }
}
static void verify_literals(LbCodeSpace *space,Program *input,LbCodeStatus expected,Eterm *literals,size_t count)
{ verify_using_bif(space,input,expected,literals,count,BIF_splus_2); }
static void verify(LbCodeSpace *space,Program *input,LbCodeStatus expected)
{ verify_literals(space,input,expected,NULL,0); }
static void forwarding_returns(LbCodeSpace *space)
{
    for(unsigned mode=0;mode<10;++mode) {
        Program p; begin(&p); ADD(&p,allocate_2,U(0),U(2));
        unsigned id=mode==2?BIF_make_tuple_2:mode==5?BIF_splus_2:BIF_list_to_tuple_1;
        unsigned arity=(unsigned)space->engine->bifs[id].arity;
        if(mode==1) ADD(&p,call_ext_2,U(arity),U(0));
        else ADD(&p,call_2,U(2),F(6));
        if(mode==7) ADD(&p,move_2,X(1),X(0)); /* Calls still clobber non-result X facts. */
        if(mode!=8) ADD(&p,test_arity_3,F(3),X(0),U(2));
        ADD(&p,get_tuple_element_3,X(0),U(1),X(0)); ADD(&p,deallocate_1,U(0)); ret(&p);
        ADD(&p,label_1,U(3)); ADD(&p,move_2,A(0),X(0)); ADD(&p,deallocate_1,U(0)); ret(&p);
        ADD(&p,int_func_start_5,U(5),U(0),A(0),A(2),U(2)); ADD(&p,label_1,U(6));
        if(mode==3) ADD(&p,is_nil_2,F(7),X(0)); /* A reachable non-tuple return below. */
        if(mode==4) ret(&p);
        else if(mode==9) ADD(&p,call_only_2,U(2),F(6)); /* Tail calls retain the proven external tuple result. */
        else ADD(&p,call_ext_only_2,U(mode==6?2:arity),U(0));
        if(mode==3) { ADD(&p,label_1,U(7)); ADD(&p,move_2,A(0),X(0)); ret(&p); }
        verify_using_bif(space,&p,(mode<=5 || mode==8 || mode==9)?LB_CODE_OK:LB_CODE_FORMAT,NULL,0,id);
    }
}
static void terminal_frame_joins(LbCodeSpace *space)
{
    for(unsigned kind=0;kind<8;++kind) {
        Program p; begin(&p); ADD(&p,allocate_2,U(2),U(2));
        ADD(&p,move_2,N,Y(0));
        if(kind!=3) ADD(&p,move_2,N,Y(1));
        ADD(&p,is_nil_2,F(3),X(0));
        if(kind==4) ADD(&p,move_2,X(0),X(2));
        ADD(&p,trim_2,U(1),U(1)); ADD(&p,jump_1,F(3));
        ADD(&p,label_1,U(3)); ADD(&p,line_1,U(0));
        if(kind==2) { ADD(&p,deallocate_1,U(1)); ret(&p); }
        else {
            if(kind==6) ADD(&p,move_2,X(0),X(1));
            if(kind==7) { ADD(&p,move_2,Y(0),X(0)); ret(&p); }
            else if(kind==1) ADD(&p,case_end_1,X(0));
            else if(kind==4) ADD(&p,badmatch_1,X(2));
            else if(kind==5) ADD(&p,badmatch_1,Y(0));
            else ADD(&p,badmatch_1,X(0));
        }
        /* A terminal badmatch may read one initialized Y slot shared by
         * differently sized frames. Normal Y reads, deallocation, undefined
         * operands, and all normal continuations retain strict frame proof. */
        verify(space,&p,(kind<2 || kind==5)?LB_CODE_OK:LB_CODE_FORMAT);
    }
}
static void apply_boundaries(LbCodeSpace *space)
{
    for(unsigned kind=0;kind<6;++kind) {
        Program p; begin(&p); ADD(&p,allocate_2,U(0),U(2));
        if(kind!=1) ADD(&p,move_2,A(0),X(2));
        if(kind==2) ADD(&p,apply_1,U(MAX_ARG+1));
        else if(kind==3) ADD(&p,apply_last_2,U(1),U(1));
        else if(kind==4) ADD(&p,apply_last_2,U(1),U(0));
        else ADD(&p,apply_1,U(1));
        if(kind==5) ADD(&p,move_2,X(1),X(0)); /* all but the result were clobbered */
        ADD(&p,deallocate_1,U(0)); ret(&p);
        verify(space,&p,(kind==0 || kind==4)?LB_CODE_OK:LB_CODE_FORMAT);
    }
}
static void exception_handler_calls(LbCodeSpace *space)
{
    for(unsigned raises=0;raises<2;++raises) {
        Program p; begin(&p); ADD(&p,allocate_2,U(1),U(1));
        ADD(&p,try_2,Y(0),F(3)); ADD(&p,call_2,U(0),F(6));
        ADD(&p,label_1,U(3)); ADD(&p,try_case_1,Y(0));
        ADD(&p,is_eq_exact_3,F(4),X(1),N);
        ADD(&p,move_2,X(1),X(0)); ADD(&p,deallocate_1,U(1)); ret(&p);
        ADD(&p,label_1,U(4)); ADD(&p,move_2,X(1),X(0)); ADD(&p,deallocate_1,U(1)); ret(&p);
        ADD(&p,int_func_start_5,U(5),U(0),A(0),A(2),U(0)); ADD(&p,label_1,U(6));
        if(raises) {
            ADD(&p,move_2,N,X(0)); ADD(&p,call_ext_only_2,U(1),U(0));
        } else {
            ADD(&p,move_2,N,X(0)); ret(&p);
        }
        verify_using_bif(space,&p,raises?LB_CODE_OK:LB_CODE_FORMAT,NULL,0,BIF_exit_1);
    }
}
static void predicate_scratch(LbCodeSpace *space)
{
    Eterm big[]={_make_header(2,POS_BIG_SUBTAG),0,1};
    Eterm floating[2]={_make_header(1,FLOAT_SUBTAG),0}; double value=1.0;
    memcpy(floating+1,&value,sizeof(value));
    Eterm cons[]={make_small(1),NIL};
    Eterm binary[]={_make_header(2,HEAP_BITS_SUBTAG),8,0};
    for(unsigned kind=0;kind<3;++kind) for(unsigned failure=0;failure<2;++failure) for(unsigned unsafe=0;unsafe<2;++unsafe) {
        Eterm literal=failure?make_boxed(floating):kind==1?make_list(cons):kind==2?make_boxed(binary):make_boxed(big); Program p; begin(&p);
        ADD(&p,test_heap_2,U(2),U(2)); ADD(&p,put_tuple2_2,X(MAX_REG-1),U(1),X(0));
        if(kind==1) ADD(&p,is_list_2,F(3),((BeamOpArg){TAG_q,0}));
        else if(kind==2) ADD(&p,is_binary_2,F(3),((BeamOpArg){TAG_q,0}));
        else ADD(&p,is_integer_2,F(3),((BeamOpArg){TAG_q,0}));
        if(!failure) {
            if(unsafe) ADD(&p,get_tuple_element_3,X(MAX_REG-1),U(0),X(0));
            else ADD(&p,move_2,X(MAX_REG-1),X(0));
        }
        ret(&p); ADD(&p,label_1,U(3));
        if(failure) {
            if(unsafe) ADD(&p,get_tuple_element_3,X(MAX_REG-1),U(0),X(0));
            else ADD(&p,move_2,X(MAX_REG-1),X(0));
        }
        ret(&p); verify_literals(space,&p,unsafe?LB_CODE_FORMAT:LB_CODE_OK,&literal,1);
    }
}
static void list_is_not_cons(LbCodeSpace *space)
{
    for(unsigned nonempty=0;nonempty<2;++nonempty) {
        Program p; begin(&p);
        if(nonempty) ADD(&p,is_nonempty_list_2,F(1),X(0));
        ADD(&p,is_list_2,F(1),X(0)); ADD(&p,get_hd_2,X(0),X(0)); ret(&p);
        verify(space,&p,LB_CODE_OK); /* is_list includes NIL; get_hd rejects NIL at runtime. */
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
        verify(space,&p,LB_CODE_OK); /* get_hd checks the cons tag at runtime. */
    }
    for(mode=0;mode<2;++mode) {
        begin(&p); ADD(&p,is_tuple_2,F(1),X(0)); ADD(&p,is_nil_2,F(3),X(1));
        ADD(&p,test_arity_3,F(1),X(0),U(3)); ADD(&p,jump_1,F(4)); ADD(&p,label_1,U(3));
        for(i=0;i<12;++i) ADD(&p,move_2,X(1),X(1));
        ADD(&p,test_arity_3,F(1),X(0),U(2)); ADD(&p,label_1,U(4));
        ADD(&p,get_tuple_element_3,X(0),U(mode?2:1),X(0)); ret(&p);
        verify(space,&p,LB_CODE_OK); /* Runtime tuple guards handle an unproven field range. */
    }
    for(mode=0;mode<2;++mode) {
        begin(&p); ADD(&p,is_nonempty_list_2,F(1),X(0)); ADD(&p,label_1,U(3));
        ADD(&p,get_hd_2,X(0),X(1));
        if(mode) ADD(&p,move_2,N,X(0));
        ADD(&p,jump_1,F(3)); verify(space,&p,LB_CODE_OK); /* Runtime cons checks guard every iteration. */
    }
}
static void transfers(LbCodeSpace *space)
{
    Program p; unsigned mode;
    begin(&p); ADD(&p,is_nonempty_list_2,F(3),X(0)); ret(&p); ADD(&p,label_1,U(3));
    ADD(&p,get_hd_2,X(0),X(0)); ret(&p); verify(space,&p,LB_CODE_OK); /* Runtime badarg on non-cons. */
    for(mode=0;mode<2;++mode) {
        begin(&p); ADD(&p,is_nonempty_list_2,F(1),X(0));
        if(mode) ADD(&p,move_2,N,X(0)); else ADD(&p,move_2,X(0),X(0));
        ADD(&p,get_hd_2,X(0),X(0)); ret(&p); verify(space,&p,LB_CODE_OK); /* Runtime badarg on non-cons. */
        begin(&p); ADD(&p,is_nonempty_list_2,F(1),X(0)); ADD(&p,swap_2,X(0),X(1));
        ADD(&p,get_hd_2,X(mode?0:1),X(0)); ret(&p); verify(space,&p,LB_CODE_OK); /* Runtime badarg on non-cons. */
        begin(&p); ADD(&p,test_heap_2,U(3),U(2));
        ADD(&p,put_tuple2_2,X(0),U(2),X(0),X(1)); ADD(&p,get_tuple_element_3,X(0),U(mode?2:1),X(0)); ret(&p);
        verify(space,&p,LB_CODE_OK); /* Tuple accesses are guarded by the executor. */
    }
    begin(&p); ADD(&p,test_heap_2,U(2),U(2)); ADD(&p,put_list_3,X(0),X(1),X(0));
    ADD(&p,get_tl_2,X(0),X(0)); ret(&p); verify(space,&p,LB_CODE_OK);
    begin(&p); ADD(&p,is_nonempty_list_2,F(1),X(0)); ADD(&p,allocate_2,U(2),U(2));
    ADD(&p,move_2,X(0),Y(1)); ADD(&p,trim_2,U(1),U(1)); ADD(&p,get_hd_2,Y(0),X(0));
    ADD(&p,deallocate_1,U(1)); ret(&p); verify(space,&p,LB_CODE_OK);
    p.ops[5].a[1].val=2; verify(space,&p,LB_CODE_FORMAT);
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
        ADD(&p,move_2,N,X(0)); ret(&p);
        verify(space,&p,LB_CODE_OK); /* The runtime guard handles unknown call results. */
    }
}
static void arithmetic_edges(LbCodeSpace *space)
{
    Program p;
    /* A successful arithmetic destination loses its old list fact. The
     * failure edge retains that fact, but not registers beyond Live. */
    begin(&p); ADD(&p,is_nonempty_list_2,F(1),X(0));
    ADD(&p,gc_bif2_6,F(3),U(2),U(0),X(0),X(1),X(0));
    ADD(&p,get_hd_2,X(0),X(0)); ret(&p); ADD(&p,label_1,U(3)); ret(&p);
    verify(space,&p,LB_CODE_OK); /* The dynamic BIF result is guarded by get_hd. */
    begin(&p); ADD(&p,is_nonempty_list_2,F(1),X(0));
    ADD(&p,gc_bif2_6,F(3),U(2),U(0),X(0),X(1),X(0));
    ret(&p); ADD(&p,label_1,U(3)); ADD(&p,get_hd_2,X(0),X(0)); ret(&p);
    verify(space,&p,LB_CODE_OK);
    begin(&p); ADD(&p,is_nonempty_list_2,F(1),X(1));
    ADD(&p,gc_bif2_6,F(3),U(1),U(0),X(0),X(1),X(0));
    ret(&p); ADD(&p,label_1,U(3)); ADD(&p,get_hd_2,X(1),X(0)); ret(&p);
    verify(space,&p,LB_CODE_FORMAT);
}
static void tested_tuple_copy(LbCodeSpace *space)
{
    for(unsigned mode=0;mode<4;++mode) {
        Program p; begin(&p);
        ADD(&p,allocate_2,U(1),U(2)); ADD(&p,move_2,X(0),Y(0));
        if(mode==1) ADD(&p,move_2,X(1),Y(0));
        if(mode==2) ADD(&p,label_1,U(7));
        if(mode==3) ADD(&p,move_2,X(1),X(0));
        ADD(&p,is_tuple_2,F(3),X(0)); ADD(&p,test_arity_3,F(3),X(0),U(2));
        ADD(&p,get_tuple_element_3,Y(0),U(1),X(0)); ADD(&p,deallocate_1,U(1)); ret(&p);
        ADD(&p,label_1,U(3)); ADD(&p,move_2,N,X(0)); ADD(&p,deallocate_1,U(1)); ret(&p);
        verify(space,&p,LB_CODE_OK); /* The runtime tuple guard protects unproven aliases. */
    }
}
static void proven_match_start(LbCodeSpace *space)
{
    Eterm empty[2]={_make_header(1,HEAP_BITS_SUBTAG),0};
    Eterm literals[]={make_boxed(empty),make_small(0)};
    for(unsigned mode=0;mode<4;++mode) {
        Program p; begin(&p);
        if(mode!=1) ADD(&p,move_2,((BeamOpArg){TAG_q,mode==2?1:0}),X(0));
        ADD(&p,bs_start_match4_4,((BeamOpArg){TAG_a,mode==3?am_resume:am_no_fail}),U(2),X(0),X(1));
        ADD(&p,move_2,N,X(0)); ret(&p);
        verify_literals(space,&p,mode==3?LB_CODE_FORMAT:LB_CODE_OK,literals,2); /* Input shape is checked at runtime. */
    }
}
static void interprocedural_match_context_restart(LbCodeSpace *space)
{
    Eterm empty[2]={_make_header(1,HEAP_BITS_SUBTAG),0},literals[]={make_boxed(empty)};
    for(unsigned mode=0;mode<3;++mode) {
        Program p={0}; LbBeamProgram program={0}; LoaderState st={0}; LbCodeStatus status;
        LbCodeModule module={0};
        /* The first caller seeds the callee's parameter fact with a context.
         * Its self-call receives an unclassified result, so only an immediate
         * no-fail match start can prove the value is safely revalidated. */
        ADD(&p,int_func_start_5,U(1),U(0),A(0),A(10),U(1)); ADD(&p,label_1,U(2));
        ADD(&p,allocate_2,U(0),U(1)); ADD(&p,move_2,((BeamOpArg){TAG_q,0}),X(0));
        ADD(&p,bs_start_match4_4,((BeamOpArg){TAG_a,am_no_fail}),U(1),X(0),X(1));
        ADD(&p,move_2,X(1),X(0)); ADD(&p,call_2,U(1),F(6));
        ADD(&p,deallocate_1,U(0)); ret(&p);
        ADD(&p,int_func_start_5,U(5),U(0),A(0),A(11),U(1)); ADD(&p,label_1,U(6));
        if(mode==1) {
            ADD(&p,bs_get_position_3,X(0),X(1),U(1));
            ADD(&p,bs_get_tail_3,X(0),X(0),U(1));
        } else if(mode==2) {
            ADD(&p,bs_start_match3_4,F(10),X(0),U(1),X(1));
            ADD(&p,bs_get_tail_3,X(1),X(0),U(1));
        } else {
            ADD(&p,bs_start_match4_4,((BeamOpArg){TAG_a,am_no_fail}),U(1),X(0),X(1));
            ADD(&p,bs_get_tail_3,X(1),X(0),U(1));
        }
        ADD(&p,allocate_2,U(0),U(1));
        ADD(&p,call_2,U(1),F(9)); ADD(&p,deallocate_1,U(0)); ADD(&p,call_only_2,U(1),F(6));
        if(mode==2) { ADD(&p,label_1,U(10)); ADD(&p,move_2,N,X(0)); ret(&p); }
        ADD(&p,int_func_start_5,U(8),U(0),A(0),A(12),U(1)); ADD(&p,label_1,U(9));
        ADD(&p,move_2,X(0),X(0)); ret(&p);
        program.ops=p.ops; program.atoms=space->atoms; program.literals=literals; program.literal_count=1;
        st.program=&program; st.space=space; st.label_count=11; st.module_code=&module;
        status=lb_verify_program(&st);
        LbCodeStatus expected=mode==1?LB_CODE_FORMAT:LB_CODE_OK;
        if(status!=expected) fprintf(stderr,"match-context restart status=%d expected=%d stage=%s offset=%zu\n",
                                     status,expected,program.error.stage,program.error.offset);
        CHECK(status==expected);
        if(mode==1) CHECK(!strcmp(program.error.stage,"match-context callsite argument shape is unknown"));
    }
}
static void list_can_root_match_context(LbCodeSpace *space)
{
    Program p; begin(&p);
    ADD(&p,bs_start_match3_4,F(3),X(0),U(1),X(1));
    ADD(&p,test_heap_2,U(2),U(2));
    ADD(&p,put_list_3,X(0),X(1),X(0)); ret(&p);
    ADD(&p,label_1,U(3)); ADD(&p,move_2,A(0),X(0)); ret(&p);
    verify(space,&p,LB_CODE_OK);
}
static void if_end_frame_join(LbCodeSpace *space)
{
    Program p; begin(&p);
    ADD(&p,is_nil_2,F(3),X(0));
    ADD(&p,allocate_2,U(1),U(1)); ADD(&p,move_2,X(0),Y(0));
    ADD(&p,is_nil_2,F(3),X(0)); ADD(&p,deallocate_1,U(1)); ret(&p);
    ADD(&p,label_1,U(3)); ADD(&p,line_1,U(0)); ADD(&p,if_end_0);
    verify(space,&p,LB_CODE_OK);
}
static void set_tuple_element_proof(LbCodeSpace *space)
{
    for(unsigned mode=0;mode<3;++mode) {
        Program p; begin(&p); ADD(&p,allocate_2,U(1),U(2));
        ADD(&p,move_2,X(0),Y(0)); ADD(&p,move_2,Y(0),X(2));
        ADD(&p,move_2,((BeamOpArg){TAG_i,2}),X(0));
        ADD(&p,call_ext_2,U(3),U(0));
        if(mode==1) ADD(&p,move_2,X(0),X(1));
        ADD(&p,set_tuple_element_3,Y(0),((BeamOpArg){TAG_x,mode==1?1:0}),U(mode==2?2:0));
        ADD(&p,deallocate_1,U(1)); ret(&p);
        verify_using_bif(space,&p,mode?LB_CODE_FORMAT:LB_CODE_OK,NULL,0,BIF_setelement_3);
    }
}
int main(void)
{
    LbEngine *engine; LbCodeSpace *space;
    CHECK(lb_engine_create(NULL,&engine)==LB_ENGINE_OK && lb_code_space_create(engine,&space)==LB_CODE_OK);
    tested_tuple_copy(space); proven_match_start(space); interprocedural_match_context_restart(space); list_can_root_match_context(space); if_end_frame_join(space); set_tuple_element_proof(space); joins(space); transfers(space); arithmetic_edges(space); forwarding_returns(space); apply_boundaries(space); exception_handler_calls(space); predicate_scratch(space); list_is_not_cons(space); terminal_frame_joins(space);
    CHECK(lb_code_space_destroy(space)==LB_CODE_OK && lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine);
    puts("CORE_SHAPE_PROOF_OK failure_edges=true late_joins=true backedges=true bounds=true alias_moves=true call_clobbers=true y_facts=true hints_not_authority=true arithmetic_edges=true predicate_scratch=true list_not_cons=true trim_facts=true execution=false");
    return 0;
}
