/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>.
 */
#include "process_internal.h"
#include "work_budget.h"

Eterm lb_bif_lists_reverse_2(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    Eterm input,tail,result;
    size_t count=0,words,need,before;
    LbAllocDomain *domain=p->entry_module->space->domain;
    (void)pc;
    p->last_native_cost=0;
    if(args!=p->reg) { p->status=LB_PROCESS_INVALID; return THE_NON_VALUE; }
    if(!lb_work_domain_charge(domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    p->last_native_cost=1;
    input=args[0];
    while(is_list(input)) {
        if(count==SIZE_MAX) { p->freason=LB_EXC_SYSTEM_LIMIT; return THE_NON_VALUE; }
        if(!lb_work_domain_charge(domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
        ++p->last_native_cost;
        ++count;
        input=CDR(list_val(input));
    }
    if(!is_nil(input)) { p->fvalue=args[0]; p->freason=LB_EXC_BADARG; return THE_NON_VALUE; }
    if(!count) return args[1];
    if(!lb_size_mul(count,2,&words) || !lb_size_add(words,p->heap_reserved,&need)) {
        p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE;
    }
    if(!lb_work_domain_charge(domain,(uint64_t)count)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    p->last_native_cost+=(uint64_t)count;
    before=p->collections;
    if(!lb_heap_reserve(p,need,MAX_REG)) {
        LbAllocStatus failure=lb_alloc_domain_failure(domain);
        p->status=failure==LB_ALLOC_WORK_LIMIT?LB_PROCESS_WORK_LIMIT:
            failure==LB_ALLOC_NO_MEMORY?LB_PROCESS_NO_MEMORY:LB_PROCESS_LIMIT;
        return THE_NON_VALUE;
    }
    if(p->collections!=before) p->last_native_cost+=p->last_gc_cost;
    input=p->reg[0]; tail=p->reg[1]; result=tail;
    while(is_list(input)) {
        Eterm *cell=p->htop;
        result=CONS(cell,CAR(list_val(input)),result);
        p->htop+=2;
        input=CDR(list_val(input));
    }
    return result;
}
