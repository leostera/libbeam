/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#include "process_internal.h"
#include "raise_trace.h"
#include "alloc_internal.h"
#include "world_internal.h"
#include "work_budget_internal.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while(0)
static void *deny(void *context,size_t bytes)
{ (void)context; (void)bytes; return NULL; }
static size_t length(Eterm list)
{
    size_t count=0;
    while(is_list(list)) { ++count; list=list_val(list)[1]; }
    CHECK(is_nil(list)); return count;
}
static void work_boundary(void)
{
    LbEngine *engine; LbWorld *world; LbReclamation *reclamation;
    CHECK(lb_engine_create(NULL,&engine)==LB_ENGINE_OK);
    CHECK(lb_world_create(engine,&world)==LB_WORLD_OK); lb_engine_enter(engine);
    LbAllocDomain *domain=&world->domain;
    size_t baseline=lb_alloc_domain_usage(domain).current;
    LbCodeSpace space={.domain=domain}; LbCodeModule module={.space=&space};
    LbProcess p={.entry_module=&module,.status=LB_PROCESS_READY};
    CHECK(lb_alloc_domain_allocate(domain,256*sizeof(Eterm),(void **)&p.heap)==LB_ALLOC_OK);
    p.htop=p.heap; p.stop=p.hend=p.heap+256; p.construction_root=NIL;
    Eterm frame=TUPLE3(p.htop,am_erlang,am_error,make_small(1)); p.htop+=4;
    Eterm trace=NIL,out;
    for(size_t i=0;i<10;++i) { trace=CONS(p.htop,frame,trace); p.htop+=2; }
    Eterm *checkpoint=p.htop;
    LbWorkBudget *budget=lb_work_budget_create(world); CHECK(budget);
    uint64_t initial=budget->remaining;
    lb_work_domain_borrow(domain,budget);
    CHECK(lb_raise_trace_prepare(&p,am_error,trace,&out)==LB_RAISE_TRACE_OK);
    uint64_t required=initial-budget->remaining; CHECK(required>10);
    lb_work_domain_borrow(domain,NULL); lb_work_budget_release(budget); p.htop=checkpoint;
    for(uint64_t credit=0;credit<=required;++credit) {
        budget=lb_work_budget_create(world); CHECK(budget); budget->limit=budget->remaining=credit;
        lb_work_domain_borrow(domain,budget); out=make_small(123);
        LbRaiseTraceStatus status=lb_raise_trace_prepare(&p,am_error,trace,&out);
        if(credit==required) CHECK(status==LB_RAISE_TRACE_OK && budget->remaining==0);
        else CHECK(status==LB_RAISE_TRACE_WORK_LIMIT && out==make_small(123) && p.htop==checkpoint);
        CHECK(p.construction_root==NIL);
        lb_work_domain_borrow(domain,NULL); lb_work_domain_clear_failure(domain);
        lb_alloc_domain_begin_operation(domain); lb_work_budget_release(budget); p.htop=checkpoint;
    }
    CHECK(lb_alloc_domain_release(domain,p.heap)==LB_ALLOC_OK);
    CHECK(lb_alloc_domain_usage(domain).current==baseline);
    lb_engine_leave(engine);
    CHECK(lb_world_stop(world,&reclamation)==LB_WORLD_OK && lb_reclamation_poll(reclamation)==LB_WORLD_OK);
    CHECK(lb_reclamation_consume(reclamation)==LB_WORLD_OK);
    lb_reclamation_release(reclamation); lb_world_release(world);
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine);
}
int main(void)
{
    LbAllocDomain *domain; CHECK(lb_alloc_domain_create(NULL,&domain)==LB_ALLOC_OK);
    LbCodeSpace space={.domain=domain}; LbCodeModule module={.space=&space};
    LbProcess p={.entry_module=&module,.status=LB_PROCESS_READY};
    CHECK(lb_alloc_domain_allocate(domain,128*sizeof(Eterm),(void **)&p.heap)==LB_ALLOC_OK);
    p.htop=p.heap; p.hend=p.stop=p.heap+128;
    p.construction_root=p.receive_root=NIL; p.fvalue=make_small(7); p.exception_value=make_small(8); p.freason=LB_EXC_BADARG;
    for(size_t i=0;i<MAX_REG;++i) p.reg[i]=NIL;
    Eterm out=make_small(123),*before=p.htop;
    CHECK(lb_raise_trace_prepare(&p,am_true,NIL,&out)==LB_RAISE_TRACE_BADARG && out==make_small(123) && before==p.htop);
    CHECK(lb_raise_trace_prepare(&p,am_error,am_false,&out)==LB_RAISE_TRACE_BADARG && out==make_small(123));
    Eterm frame=TUPLE3(p.htop,am_erlang,am_error,am_true); p.htop+=4;
    Eterm trace=NIL;
    for(size_t i=0;i<10;++i) { trace=CONS(p.htop,frame,trace); p.htop+=2; }
    /* Validation must inspect the suffix even though only eight frames survive. */
    Eterm *last=list_val(trace);
    while(is_list(last[1])) last=list_val(last[1]);
    last[1]=am_false;
    CHECK(lb_raise_trace_prepare(&p,am_error,trace,&out)==LB_RAISE_TRACE_BADARG && out==make_small(123));
    last[1]=NIL; last[0]=make_small(42);
    CHECK(lb_raise_trace_prepare(&p,am_error,trace,&out)==LB_RAISE_TRACE_BADARG && out==make_small(123));
    last[0]=frame;
    p.heap_reserved=512; /* force GC while the input is rooted by the helper */
    CHECK(lb_raise_trace_prepare(&p,am_throw,trace,&out)==LB_RAISE_TRACE_OK);
    CHECK(p.collections==1 && p.construction_root==NIL);
    CHECK(p.freason==LB_EXC_BADARG && p.fvalue==make_small(7) && p.exception_value==make_small(8));
    p.heap_reserved=0; p.reg[0]=out;
    CHECK(lb_process_collect_live(&p,1,1)); out=p.reg[0];
    Eterm *opaque=tuple_val(out); CHECK(arityval(opaque[0])==3 && opaque[3]==NIL);
    Eterm *saved=boxed_val(opaque[1]); CHECK(saved[0]==_make_header(4,NEG_BIG_SUBTAG));
    CHECK(saved[1]==(LB_EXC_THROWN&~LB_EXF_SAVETRACE));
    CHECK(saved[2]==0 && saved[3]==0 && saved[4]==0);
    CHECK(length(opaque[2])==8);
    for(Eterm tail=opaque[2];is_list(tail);tail=list_val(tail)[1]) {
        Eterm *item=tuple_val(list_val(tail)[0]);
        CHECK(arityval(item[0])==4 && item[1]==am_erlang && item[2]==am_error && item[3]==am_true && item[4]==NIL);
    }
    CHECK(lb_raise_trace_prepare(&p,am_exit,NIL,&out)==LB_RAISE_TRACE_OK);
    CHECK(tuple_val(out)[2]==NIL && boxed_val(tuple_val(out)[1])[1]==(LB_EXC_EXIT&~LB_EXF_SAVETRACE));
    /* Allocation refusals leave pending state, heap and output untouched. */
    p.heap_reserved=4096; before=p.htop; out=make_small(123);
    size_t old_limit=domain->limit; domain->limit=domain->current;
    CHECK(lb_raise_trace_prepare(&p,am_error,NIL,&out)==LB_RAISE_TRACE_MEMORY_LIMIT);
    CHECK(out==make_small(123) && p.htop==before && p.construction_root==NIL);
    domain->limit=old_limit; lb_alloc_domain_begin_operation(domain);
    LbSystemAllocator original=domain->system; domain->system.allocate=deny;
    CHECK(lb_raise_trace_prepare(&p,am_error,NIL,&out)==LB_RAISE_TRACE_NO_MEMORY);
    CHECK(out==make_small(123) && p.htop==before && p.construction_root==NIL);
    domain->system=original; lb_alloc_domain_begin_operation(domain);
    CHECK(p.freason==LB_EXC_BADARG && p.fvalue==make_small(7) && p.exception_value==make_small(8));
    CHECK(lb_alloc_domain_release(domain,p.heap)==LB_ALLOC_OK);
    CHECK(lb_alloc_domain_destroy(domain)==LB_ALLOC_OK);
    work_boundary();
    puts("CORE_RAISE_TRACE_OK syntax=true full_suffix=true normalization=true truncated_depth=true moving_roots=true pending_unchanged=true allocation_failure_atomic=true work_threshold=true execution_admitted=false");
    return 0;
}
