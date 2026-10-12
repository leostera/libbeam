/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Ledger/lease tests only; production instruction/helper wiring is separate.
 */
#include "world_internal.h"
#include "work_budget_internal.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"CHECK %d: %s\n",__LINE__,#c); abort(); } } while(0)
static void retire(LbWorld *world)
{
    LbReclamation *reclamation;
    CHECK(lb_world_stop(world,&reclamation)==LB_WORLD_OK && lb_reclamation_poll(reclamation)==LB_WORLD_OK);
    CHECK(lb_reclamation_consume(reclamation)==LB_WORLD_OK);
    lb_reclamation_release(reclamation); lb_world_release(world);
}
int main(void)
{
    LbEngine *engine; LbWorld *world,*maximum;
    CHECK(lb_engine_create(NULL,&engine)==LB_ENGINE_OK);
    LbResourceOverrides limits={0,1,0,5};
    CHECK(lb_world_create_with_overrides(engine,&limits,&world)==LB_WORLD_OK);
    limits.invocation_work=UINT64_MAX;
    CHECK(lb_world_create_with_overrides(engine,&limits,&maximum)==LB_WORLD_OK);
    lb_engine_enter(engine);
    size_t before=lb_alloc_domain_usage(&world->domain).current;
    LbWorkBudget *a=lb_work_budget_create(world),*b=lb_work_budget_create(world);
    CHECK(a && b && a!=b && lb_work_budget_usage(a).remaining==5);
    LbWorkProcessLease prepared;
    CHECK(lb_work_process_prepare(a,&prepared)==LB_ALLOC_OK && a->references==3);
    lb_work_process_abort(&prepared);
    CHECK(a->references==1 && !prepared.birth && !prepared.active);
    size_t owned=lb_alloc_domain_usage(&world->domain).current; CHECK(owned>before);
    CHECK(lb_work_budget_retain(a)==LB_ALLOC_OK); // A child/message lease, not a renewed allowance.
    a->references=SIZE_MAX;
    CHECK(lb_work_budget_retain(a)==LB_ALLOC_LIMIT && a->references==SIZE_MAX);
    a->references=2;
    CHECK(lb_work_budget_charge(a,2) && lb_work_budget_charge(a,0));
    CHECK(lb_work_budget_usage(a).remaining==3 && lb_work_budget_usage(b).remaining==5);
    CHECK(lb_work_budget_charge(a,3) && !lb_work_budget_usage(a).exhausted);
    CHECK(!lb_work_budget_charge(a,1) && lb_work_budget_usage(a).exhausted);
    CHECK(!lb_work_budget_charge(a,0) && lb_work_budget_usage(a).denied_charge==1);
    CHECK(lb_work_budget_charge(b,2) && lb_work_budget_usage(b).remaining==3);
    lb_work_budget_escape(a); CHECK(lb_work_budget_usage(a).escaped);
    lb_work_budget_release(a); CHECK(lb_alloc_domain_usage(&world->domain).current==owned);
    lb_work_budget_release(a); lb_work_budget_release(b);
    CHECK(!world->budgets && lb_alloc_domain_usage(&world->domain).current==before);
    LbWorkBudget *full=lb_work_budget_create(maximum); CHECK(full);
    CHECK(lb_work_budget_charge(full,UINT64_MAX) && lb_work_budget_usage(full).remaining==0);
    CHECK(!lb_work_budget_charge(full,1)); lb_work_budget_release(full);
    CHECK(lb_work_bulk_units(0)==0 && lb_work_bulk_units(1)==1 && lb_work_bulk_units(16)==1);
    CHECK(lb_work_bulk_units(17)==2 && lb_work_bulk_units(UINT64_MAX)==UINT64_MAX/16+1);
    CHECK(lb_work_budget_charge(NULL,UINT64_MAX)); // Explicit management/component sentinel only.
    lb_engine_leave(engine);
    retire(world); retire(maximum); CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine);
    puts("CORE_WORK_LEDGER_OK fresh_owners=true inherited_credit=true no_retry_reset=true checked_threshold=true leases_accounted=true physical_release=true runtime_enforcement=false");
}
