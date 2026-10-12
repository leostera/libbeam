/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#include "work_budget_internal.h"
#include "world_internal.h"
#include "process_internal.h"
LbWorkBudget *lb_work_budget_create(LbWorld *world)
{
    void *memory;
    if(!world || world->phase>=LB_WORLD_STOPPING || !world->limits.invocation_work) return NULL;
    assert(world->engine->control_borrow); // Only an independently admitted host invocation creates credit.
    if(lb_alloc_domain_allocate(&world->domain,sizeof(LbWorkBudget),&memory)!=LB_ALLOC_OK) return NULL;
    LbWorkBudget *budget=memory; memset(budget,0,sizeof(*budget));
    budget->world=world; budget->domain=&world->domain; budget->references=1;
    budget->limit=budget->remaining=world->limits.invocation_work;
    budget->next=world->budgets;
    if(budget->next) budget->next->previous=budget;
    world->budgets=budget; return budget;
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
    LbWorld *world=budget->world;
    if(budget->call || world->domain.work==budget) abort();
    if(budget->previous) budget->previous->next=budget->next;
    else if(world->budgets==budget) world->budgets=budget->next;
    else abort();
    if(budget->next) budget->next->previous=budget->previous;
    lb_release(&world->domain,budget);
}
void lb_work_budget_escape(LbWorkBudget *budget) { if(budget) budget->escaped=1; }
LbWorkUsage lb_work_budget_usage(const LbWorkBudget *budget)
{ return (LbWorkUsage){budget->limit,budget->remaining,budget->denied_charge,budget->exhausted,budget->escaped}; }
LbAllocStatus lb_work_process_prepare(LbWorkBudget *budget,LbWorkProcessLease *out)
{
    if(!out) return LB_ALLOC_INVALID;
    *out=(LbWorkProcessLease){0};
    LbAllocStatus status=lb_work_budget_retain(budget);
    if(status!=LB_ALLOC_OK) return status;
    status=lb_work_budget_retain(budget);
    if(status!=LB_ALLOC_OK) { lb_work_budget_release(budget); return status; }
    out->birth=out->active=budget; return LB_ALLOC_OK;
}
void lb_work_process_abort(LbWorkProcessLease *lease)
{
    lb_work_budget_release(lease->birth); lb_work_budget_release(lease->active);
    *lease=(LbWorkProcessLease){0};
}
void lb_work_process_publish(LbProcess *process,LbWorkProcessLease *lease)
{
    assert(!process->work_birth && !process->work_active && !process->work_previous && !process->work_candidate);
    assert(!lease->birth || process->entry_module->space==lease->birth->world->space);
    process->work_birth=lease->birth; process->work_active=lease->active;
    *lease=(LbWorkProcessLease){0};
}
void lb_work_process_release(LbProcess *process)
{
    lb_work_budget_release(process->work_birth); lb_work_budget_release(process->work_active);
    lb_work_budget_release(process->work_previous);
    process->work_birth=process->work_active=process->work_previous=NULL; process->work_candidate=NULL;
}
void lb_work_domain_clear_failure(LbAllocDomain *domain)
{
    assert(!domain->work);
    LbWorkBudget *failed=domain->failed_work; domain->failed_work=NULL;
    lb_work_budget_release(failed);
}
void lb_work_cross_effect(LbWorkBudget *source,LbWorkBudget *target)
{
    if(source!=target) { lb_work_budget_escape(source); lb_work_budget_escape(target); }
}
int lb_work_receive_begin(LbProcess *process,LbMessage *candidate)
{
    if(!candidate || process->work_candidate==candidate) return 1;
    if(process->work_candidate) abort();
    if(lb_work_budget_retain(candidate->budget)!=LB_ALLOC_OK) {
        process->status=LB_PROCESS_LIMIT; return 0;
    }
    process->work_previous=process->work_active; // Transfer, not an additional lease.
    process->work_active=candidate->budget; process->work_candidate=candidate;
    lb_work_domain_borrow(process->entry_module->space->domain,process->work_active);
    return 1;
}
void lb_work_receive_end(LbProcess *process,int consumed)
{
    if(!process->work_candidate) return; // Explicit unmetered raw component cursor.
    LbWorkBudget *released=consumed?process->work_previous:process->work_active;
    if(!consumed) process->work_active=process->work_previous;
    process->work_previous=NULL; process->work_candidate=NULL;
    lb_work_domain_borrow(process->entry_module->space->domain,process->work_active);
    lb_work_budget_release(released);
}
