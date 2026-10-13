/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1996-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Native bif.c raise_3 boundary with fallible owned trace preparation.
 */
#include "process_internal.h"
#include "raise_trace.h"
/* Native error_2 accepts any argument term. The wrapper is retained for trace
 * construction; only the first element becomes the terminal error reason. */
Eterm lb_bif_error_2(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)pc; p->last_native_cost=0;
    if(args!=p->reg) { p->status=LB_PROCESS_INVALID; return THE_NON_VALUE; }
    if(!lb_work_domain_charge(p->entry_module->space->domain,4)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    p->last_native_cost=4; size_t need,before=p->collections;
    if(!lb_size_add(3,p->heap_reserved,&need)) { p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE; }
    if(!lb_heap_reserve(p,need,2)) {
        LbAllocStatus cause=lb_alloc_domain_failure(p->entry_module->space->domain);
        p->status=cause==LB_ALLOC_WORK_LIMIT?LB_PROCESS_WORK_LIMIT:
            cause==LB_ALLOC_NO_MEMORY?LB_PROCESS_NO_MEMORY:LB_PROCESS_LIMIT;
        return THE_NON_VALUE;
    }
    if(p->collections!=before) p->last_native_cost+=p->last_gc_cost;
    p->fvalue=TUPLE2(p->htop,args[0],args[1]); p->htop+=3; p->freason=LB_EXC_ERROR_2;
    return THE_NON_VALUE;
}
Eterm lb_bif_raise_3(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)pc; p->last_native_cost=0;
    /* This allocating regular BIF receives the real X-register root array. */
    if(args!=p->reg) { p->status=LB_PROCESS_INVALID; return THE_NON_VALUE; }
    Eterm trace;
    LbRaiseTraceStatus status=lb_raise_trace_prepare(p,args[0],args[2],&trace);
    switch(status) {
    case LB_RAISE_TRACE_BADARG:return am_badarg;
    case LB_RAISE_TRACE_WORK_LIMIT:p->status=LB_PROCESS_WORK_LIMIT; break;
    case LB_RAISE_TRACE_MEMORY_LIMIT:p->status=LB_PROCESS_LIMIT; break;
    case LB_RAISE_TRACE_NO_MEMORY:p->status=LB_PROCESS_NO_MEMORY; break;
    case LB_RAISE_TRACE_INVALID:p->status=LB_PROCESS_INVALID; break;
    case LB_RAISE_TRACE_OK: {
        LbRaisedTrace supplied;
        if(!lb_raise_trace_unpack(trace,&supplied)) abort();
        p->ftrace=trace; p->fvalue=args[1]; p->freason=supplied.reason;
        break;
    }
    }
    return THE_NON_VALUE;
}
