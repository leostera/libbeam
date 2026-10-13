/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1996-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Owned process_info_bif/ERROR_HANDLER slice. All other information selectors
 * remain explicit profile refusals, not invented process statistics.
 */
#include "process_internal.h"
#include "pid_table.h"

typedef enum { INFO_SINGLE, INFO_LIST } InfoShape;
typedef struct { InfoShape shape; size_t count; } InfoRequest;
static int charge(LbProcess *p,Uint units)
{
    if(!lb_work_domain_charge(p->entry_module->space->domain,units)) { p->status=LB_PROCESS_WORK_LIMIT; return 0; }
    p->last_native_cost+=units; return 1;
}
static int item(LbProcess *p,Eterm selector)
{
    if(selector==am_error_handler) return 1;
    /* Other atom/tuple selector families are not admitted, including unknown
     * names. Do not mislabel an unimplemented valid selector as badarg. */
    if(is_atom(selector) || is_tuple(selector)) p->status=LB_PROCESS_REFUSED;
    else p->freason=LB_EXC_BADARG;
    return 0;
}
static int parse(LbProcess *p,Eterm selector,InfoRequest *out)
{
    if(is_atom(selector) || is_tuple(selector)) {
        if(!item(p,selector)) return 0;
        *out=(InfoRequest){INFO_SINGLE,1}; return 1;
    }
    size_t count=0;
    while(is_list(selector)) {
        if(!charge(p,1) || !item(p,list_val(selector)[0])) return 0;
        if(count==SIZE_MAX) { p->status=LB_PROCESS_LIMIT; return 0; }
        ++count; selector=list_val(selector)[1];
    }
    if(!is_nil(selector)) { p->freason=LB_EXC_BADARG; return 0; }
    *out=(InfoRequest){INFO_LIST,count}; return 1;
}
static Eterm allocation_failure(LbProcess *p)
{
    switch(lb_alloc_domain_failure(p->entry_module->space->domain)) {
    case LB_ALLOC_LIMIT:p->status=LB_PROCESS_LIMIT; break;
    case LB_ALLOC_WORK_LIMIT:p->status=LB_PROCESS_WORK_LIMIT; break;
    default:p->status=LB_PROCESS_NO_MEMORY; break;
    }
    return THE_NON_VALUE;
}
Eterm lb_bif_process_info_2(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    InfoRequest request; (void)pc; p->last_native_cost=0;
    if(!charge(p,1) || !parse(p,args[1],&request)) return THE_NON_VALUE;
    if(!is_internal_pid(args[0])) { p->freason=LB_EXC_BADARG; return THE_NON_VALUE; }
    if(!charge(p,1)) return THE_NON_VALUE;
    LbProcess *target=lb_pid_lookup(p->entry_module->space->pids,args[0]);
    if(!target || (target->status!=LB_PROCESS_READY && target->status!=LB_PROCESS_YIELDED)) return am_undefined;
    /* All processes are serialized by the Engine. Read the atom setting before
     * collecting the caller; no borrowed target heap pointer survives GC. */
    Eterm handler=target->error_handler;
    if(!is_atom(handler)) { p->status=LB_PROCESS_INVALID; return THE_NON_VALUE; }
    if(!request.count) return NIL;
    size_t words,need,collections=p->collections;
    if(!lb_size_mul(request.count,request.shape==INFO_SINGLE?3:5,&words) ||
       !lb_size_add(words,p->heap_reserved,&need)) { p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE; }
    if(!charge(p,request.count+lb_work_bulk_units(words))) return THE_NON_VALUE;
    if(!lb_heap_reserve(p,need,MAX_REG)) return allocation_failure(p);
    if(p->collections!=collections) p->last_native_cost+=p->last_gc_cost;
    if(request.shape==INFO_SINGLE) {
        Eterm result=TUPLE2(p->htop,am_error_handler,handler); p->htop+=3; return result;
    }
    Eterm result=NIL; Eterm *hp=p->htop;
    for(size_t i=0;i<request.count;++i) {
        Eterm tuple=TUPLE2(hp,am_error_handler,handler); hp+=3;
        result=CONS(hp,tuple,result); hp+=2;
    }
    p->htop=hp; return result;
}
