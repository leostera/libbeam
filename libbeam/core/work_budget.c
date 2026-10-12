/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#include "work_budget_internal.h"
#include "world_internal.h"
LbWorkBudget *lb_work_budget_create(LbWorld *world)
{
    void *memory;
    if(!world || world->phase>=LB_WORLD_STOPPING || !world->limits.invocation_work) return NULL;
    assert(world->engine->control_borrow); // Only an independently admitted host invocation creates credit.
    if(lb_alloc_domain_allocate(&world->domain,sizeof(LbWorkBudget),&memory)!=LB_ALLOC_OK) return NULL;
    LbWorkBudget *budget=memory; memset(budget,0,sizeof(*budget));
    budget->world=world; budget->references=1;
    budget->limit=budget->remaining=world->limits.invocation_work;
    budget->next=world->budgets; world->budgets=budget; return budget;
}
LbAllocStatus lb_work_budget_retain(LbWorkBudget *budget)
{
    if(!budget) return LB_ALLOC_OK;
    if(!budget->references) abort();
    if(budget->references==SIZE_MAX) return LB_ALLOC_LIMIT;
    ++budget->references; return LB_ALLOC_OK;
}
void lb_work_budget_release(LbWorkBudget *budget)
{
    if(!budget) return;
    if(!budget->references) abort();
    if(--budget->references) return;
    LbWorld *world=budget->world; LbWorkBudget **link;
    for(link=&world->budgets;*link && *link!=budget;link=&(*link)->next) {}
    if(!*link) abort(); *link=budget->next;
    lb_release(&world->domain,budget);
}
int lb_work_budget_charge(LbWorkBudget *budget,uint64_t units)
{
    if(!budget) return 1;
    if(budget->exhausted) return 0;
    if(units>budget->remaining) {
        budget->remaining=0; budget->denied_charge=units; budget->exhausted=1; return 0;
    }
    budget->remaining-=units; return 1;
}
void lb_work_budget_escape(LbWorkBudget *budget) { if(budget) budget->escaped=1; }
LbWorkUsage lb_work_budget_usage(const LbWorkBudget *budget)
{ return (LbWorkUsage){budget->limit,budget->remaining,budget->denied_charge,budget->exhausted,budget->escaped}; }
uint64_t lb_work_bulk_units(uint64_t quantity) { return quantity/16+(quantity%16!=0); }
