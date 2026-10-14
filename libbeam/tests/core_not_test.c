/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#include "process_internal.h"
#include "world_internal.h"
#include "work_budget_internal.h"
#include "alloc_internal.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while(0)
typedef enum { TRUE_VALUE, FALSE_VALUE, ATOM_VALUE, NIL_VALUE, SMALL_VALUE, FLOAT_VALUE, TUPLE_VALUE, LIST_VALUE, BIG_VALUE, BINARY_VALUE, KIND_COUNT } Kind;
static Eterm atom(LbWorld *w,const char *name)
{ Eterm value; CHECK(lb_atoms_find(w->space->atoms,name,strlen(name),&value)==LB_ATOM_OK); return value; }
static LbCodeModule *load(LbWorld *w,const char *path)
{
    FILE *file=fopen(path,"rb"); CHECK(file && !fseek(file,0,SEEK_END));
    long length=ftell(file); CHECK(length>0 && !fseek(file,0,SEEK_SET)); unsigned char *bytes=malloc((size_t)length);
    CHECK(bytes && fread(bytes,1,(size_t)length,file)==(size_t)length && !fclose(file));
    LbCodeModule *module; LbBeamError error; LbCodeStatus status=lb_code_load(w->space,bytes,(size_t)length,&module,&error);
    if(status!=LB_CODE_OK) fprintf(stderr,"load=%d stage=%s offset=%zu\n",status,error.stage,error.offset);
    CHECK(status==LB_CODE_OK); free(bytes); return module;
}
static Eterm input(LbWorld *w,LbProcess *p,Kind kind)
{
    Eterm *hp=p->htop,result;
    switch(kind) {
    case TRUE_VALUE: return am_true;
    case FALSE_VALUE: return am_false;
    case ATOM_VALUE: return atom(w,"invalid");
    case NIL_VALUE: return NIL;
    case SMALL_VALUE: return make_small(0);
    case FLOAT_VALUE: {
        double value=1.0; hp[0]=_make_header(1,FLOAT_SUBTAG); memcpy(hp+1,&value,sizeof(value));
        p->htop+=2; return make_boxed(hp);
    }
    case TUPLE_VALUE: result=TUPLE1(hp,make_small(1)); p->htop+=2; return result;
    case LIST_VALUE: result=CONS(hp,am_true,NIL); p->htop+=2; return result;
    case BIG_VALUE: hp[0]=_make_header(2,POS_BIG_SUBTAG); hp[1]=0; hp[2]=1; p->htop+=3; return make_boxed(hp);
    case BINARY_VALUE:
        CHECK(lb_bitstring_build(w->space->domain,&p->off_heap,&p->htop,(const unsigned char *)"data",32,0,&result)==LB_ALLOC_OK); return result;
    default: abort();
    }
}
static LbProcess *create_values(LbWorld *w,LbCodeModule *module,const char *function,const Kind *kinds,size_t arity)
{
    LbCodeEntry *entry; LbProcess *p; Eterm args[5]={NIL,NIL,NIL,NIL,NIL}; CHECK(arity && arity<=5);
    CHECK(lb_code_entry_acquire(w->space,module->name,atom(w,function),arity,&entry)==LB_CODE_OK);
    CHECK(lb_process_create(entry,args,arity,512,&p)==LB_PROCESS_READY);
    for(size_t i=0;i<arity;++i) p->reg[i]=input(w,p,kinds[i]);
    lb_code_entry_release(entry); return p;
}
static LbProcess *create(LbWorld *w,LbCodeModule *module,Kind kind)
{ return create_values(w,module,"negate",&kind,1); }
static LbProcessStatus run(LbProcess *p)
{
    LbProcessStatus status=LB_PROCESS_READY;
    for(size_t i=0;i<1000 && (status==LB_PROCESS_READY || status==LB_PROCESS_YIELDED);++i) status=lb_process_run(p,1,100);
    return status;
}
static LbProcessStatus expected_status(Kind kind,int guard)
{ return guard || kind<=FALSE_VALUE?LB_PROCESS_DONE:LB_PROCESS_EXCEPTION; }
static void outcome(LbWorld *w,LbProcess *p,Kind kind,int guard)
{
    CHECK(p->status==expected_status(kind,guard));
    if(p->status==LB_PROCESS_EXCEPTION) CHECK(p->exception_value==am_badarg);
    else CHECK(lb_process_result(p)==(kind==TRUE_VALUE?am_false:kind==FALSE_VALUE?am_true:atom(w,"invalid")));
}
static void *deny(void *context,size_t bytes) { (void)context; (void)bytes; return NULL; }
static void exercise(LbWorld *w,LbCodeModule *module,int guard)
{
    LbAllocDomain *domain=w->space->domain; size_t baseline=domain->current;
    for(Kind kind=TRUE_VALUE;kind<KIND_COUNT;++kind) {
        LbProcess *p=create(w,module,kind); CHECK(run(p)==expected_status(kind,guard)); outcome(w,p,kind,guard); lb_process_destroy(p);
        for(unsigned mode=0;mode<2 && (guard || kind<=FALSE_VALUE);++mode) {
            p=create(w,module,kind);
            BeamInstr boundary=guard?op_i_bif1_sfbd:op_i_bif1_body_sbd;
            for(unsigned step=0;step<100 && p->i[0]!=boundary;++step) CHECK(lb_process_run(p,1,100)==LB_PROCESS_YIELDED);
            CHECK(p->i[0]==boundary);
            while(p->htop<p->stop-S_RESERVED) *p->htop++=NIL;
            Eterm *heap=p->htop; size_t live=domain->current,limit=domain->limit; LbSystemAllocator system=domain->system;
            if(mode) domain->system.allocate=deny; else domain->limit=live;
            CHECK(run(p)==LB_PROCESS_DONE && p->htop==heap && !p->collections && domain->current==live);
            domain->limit=limit; domain->system=system; outcome(w,p,kind,guard); lb_process_destroy(p);
        }
        uint64_t required=0;
        for(uint64_t pass=0;pass<=required+1;++pass) {
            p=create(w,module,kind); LbWorkBudget *budget=lb_work_budget_create(w); CHECK(budget);
            uint64_t credit=pass?pass-1:1000000; budget->limit=budget->remaining=credit; lb_work_domain_borrow(domain,budget);
            LbProcessStatus status=run(p);
            if(!pass) { CHECK(status==expected_status(kind,guard)); required=credit-budget->remaining; CHECK(required>1 && required<10000); }
            else if(credit==required) CHECK(status==expected_status(kind,guard) && budget->remaining==0);
            else CHECK(status==LB_PROCESS_WORK_LIMIT);
            if(status==expected_status(kind,guard)) outcome(w,p,kind,guard);
            lb_work_domain_borrow(domain,NULL); lb_work_domain_clear_failure(domain); lb_alloc_domain_begin_operation(domain);
            lb_process_destroy(p); lb_work_budget_release(budget); CHECK(domain->current==baseline);
        }
    }
}
static void guard_boundary(LbProcess *p)
{
    for(unsigned step=0;step<100 && p->i[0]!=op_i_bif2_ssfbd;++step) CHECK(lb_process_run(p,1,100)==LB_PROCESS_YIELDED);
    CHECK(p->i[0]==op_i_bif2_ssfbd);
}
static void binary_operators(LbWorld *w,LbCodeModule *module,LbCodeModule *assembly)
{
    typedef enum { BODY, GUARD, DYNAMIC, ASSEMBLY_GUARD } Invocation;
    const char *names[3][4]={{"conjunction","guard_and","dynamic","conjunction"},{"disjunction","guard_or","dynamic","disjunction"},{"exclusive","guard_xor","dynamic","exclusive"}};
    const Eterm operations[3]={am_and,am_or,am_xor};
    const Eterm truth[3][2][2]={{{am_true,am_false},{am_false,am_false}},{{am_true,am_true},{am_true,am_false}},{{am_false,am_true},{am_true,am_false}}};
    LbAllocDomain *domain=w->space->domain; size_t baseline=domain->current;
    for(unsigned op=0;op<3;++op) for(Invocation form=BODY;form<=ASSEMBLY_GUARD;++form)
    for(Kind left=TRUE_VALUE;left<KIND_COUNT;++left) for(Kind right=TRUE_VALUE;right<KIND_COUNT;++right) {
        Kind kinds[3]={left,right,NIL_VALUE}; int valid=left<=FALSE_VALUE && right<=FALSE_VALUE;
        if(form==DYNAMIC) { kinds[0]=ATOM_VALUE; kinds[1]=left; kinds[2]=right; }
        LbCodeModule *selected=form==ASSEMBLY_GUARD?assembly:module;
        LbProcessStatus expected=form==GUARD || form==ASSEMBLY_GUARD || valid?LB_PROCESS_DONE:LB_PROCESS_EXCEPTION;
        Eterm value=valid?truth[op][left][right]:atom(w,"invalid"); uint64_t cost=0;
        for(unsigned mode=0;mode<4;++mode) {
            LbProcess *p=create_values(w,selected,names[op][form],kinds,form==DYNAMIC?3:2);
            if(form==DYNAMIC) p->reg[0]=operations[op];
            LbWorkBudget *budget=lb_work_budget_create(w); CHECK(budget);
            uint64_t credit=mode==0?1000000:mode==1?cost:mode==2?cost-1:0;
            budget->limit=budget->remaining=credit; lb_work_domain_borrow(domain,budget);
            LbProcessStatus status=run(p);
            if(mode<2) {
                CHECK(status==expected);
                if(!mode) { cost=credit-budget->remaining; CHECK(cost>1 && cost<10000); } else CHECK(!budget->remaining);
                if(status==LB_PROCESS_DONE) CHECK(lb_process_result(p)==value); else CHECK(p->exception_value==am_badarg);
            } else CHECK(status==LB_PROCESS_WORK_LIMIT);
            lb_work_domain_borrow(domain,NULL); lb_work_domain_clear_failure(domain); lb_alloc_domain_begin_operation(domain);
            lb_process_destroy(p); lb_work_budget_release(budget); CHECK(domain->current==baseline);
        }
        if(form!=DYNAMIC && (form==GUARD || form==ASSEMBLY_GUARD || valid)) for(unsigned mode=0;mode<2;++mode) {
            LbProcess *p=create_values(w,selected,names[op][form],kinds,2);
            if(form==ASSEMBLY_GUARD) guard_boundary(p);
            while(p->htop<p->stop-S_RESERVED) *p->htop++=NIL;
            Eterm *top=p->htop; size_t used=domain->current,limit=domain->limit; LbSystemAllocator system=domain->system;
            if(mode) domain->system.allocate=deny; else domain->limit=used;
            CHECK(run(p)==LB_PROCESS_DONE && lb_process_result(p)==value && p->htop==top && !p->collections && domain->current==used);
            domain->limit=limit; domain->system=system; lb_process_destroy(p); CHECK(domain->current==baseline);
        }
        if(form==ASSEMBLY_GUARD) {
            uint64_t step_cost=0;
            for(unsigned pass=0;pass<2;++pass) {
                LbProcess *p=create_values(w,selected,names[op][form],kinds,2); guard_boundary(p);
                LbWorkBudget *budget=lb_work_budget_create(w); CHECK(budget);
                uint64_t credit=pass?step_cost-1:1000000; budget->limit=budget->remaining=credit;
                lb_work_domain_borrow(domain,budget); LbProcessStatus status=lb_process_run(p,1,100);
                if(!pass) { CHECK(status==LB_PROCESS_YIELDED); step_cost=credit-budget->remaining; CHECK(step_cost>1); }
                else CHECK(status==LB_PROCESS_WORK_LIMIT && p->freason==LB_EXC_NULL && !budget->remaining);
                lb_work_domain_borrow(domain,NULL); lb_work_domain_clear_failure(domain); lb_alloc_domain_begin_operation(domain);
                lb_process_destroy(p); lb_work_budget_release(budget); CHECK(domain->current==baseline);
            }
        }
    }
    Kind window[5]={TRUE_VALUE,FALSE_VALUE,TUPLE_VALUE,LIST_VALUE,BINARY_VALUE};
    LbProcess *p=create_values(w,module,"window5",window,5); Eterm *old_heap=p->heap;
    CHECK(run(p)==LB_PROCESS_DONE && p->collections && p->heap!=old_heap);
    Eterm result=lb_process_result(p); CHECK(is_tuple(result) && tuple_val(result)[0]==make_arityval(5));
    Eterm *tuple=tuple_val(result); CHECK(tuple[1]==am_true && tuple[2]==am_false);
    CHECK(is_tuple(tuple[3]) && tuple_val(tuple[3])[0]==make_arityval(1) && tuple_val(tuple[3])[1]==make_small(1));
    CHECK(is_list(tuple[4]) && CAR(list_val(tuple[4]))==am_true && is_nil(CDR(list_val(tuple[4]))));
    LbBitstringView view; CHECK(lb_bitstring_view(tuple[5],&view) && view.bit_size==32 && !memcmp(view.data,"data",4));
    lb_process_destroy(p); CHECK(domain->current==baseline);
    puts("CORE_BOOLEAN_BINARY_OK operations=3 operand_pairs=100 body=true guards=true dynamic=true exact_work=true one_below=true zero=true strict_not_short_circuit=true allocation_free=true window5_gc=true balanced=true");
}
int main(int argc,char **argv)
{
    CHECK(argc==5); LbEngine *engine; LbWorld *world;
    CHECK(lb_engine_create(NULL,&engine)==LB_ENGINE_OK && lb_world_create(engine,&world)==LB_WORLD_OK); lb_engine_enter(engine);
    LbCodeModule *body=load(world,argv[1]),*guard=load(world,argv[2]); exercise(world,body,0); exercise(world,guard,1);
    LbCodeModule *binary=load(world,argv[3]),*binary_guard=load(world,argv[4]);
    binary_operators(world,binary,binary_guard);
    lb_engine_leave(engine); LbReclamation *reclamation;
    CHECK(lb_world_stop(world,&reclamation)==LB_WORLD_OK && lb_reclamation_poll(reclamation)==LB_WORLD_OK);
    CHECK(lb_reclamation_consume(reclamation)==LB_WORLD_OK); lb_reclamation_release(reclamation); lb_world_release(world);
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine);
    puts("CORE_NOT_OK strict_boolean=true badarg=true y_guard=true allocation_free=true work_prefixes=true balanced=true"); return 0;
}
