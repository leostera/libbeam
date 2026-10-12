/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef LIBBEAM_CORE_WORK_BUDGET_H
#define LIBBEAM_CORE_WORK_BUDGET_H
#include "alloc.h"
#include <stdint.h>
typedef struct LbWorld LbWorld;
typedef struct LbWorkBudget LbWorkBudget;
typedef struct LbProcess LbProcess;
struct LbMessage;
typedef struct { LbWorkBudget *birth,*active; } LbWorkProcessLease;
typedef struct {
    uint64_t limit,remaining,denied_charge;
    int exhausted,escaped;
} LbWorkUsage;
/* Serialized, stable owned metadata; never a moving term/PID capability.
 * Creation uses the resolved world policy, not a newly replenished world counter.
 * Each task/message/call lease must be retained before its effect is published. */
LbWorkBudget *lb_work_budget_create(LbWorld *);
LbAllocStatus lb_work_budget_retain(LbWorkBudget *);
void lb_work_budget_release(LbWorkBudget *);
/* NULL denotes an explicitly unmetered component/management operation, not a
 * permitted production invocation. Zero cost never resets an existing owner. */
int lb_work_budget_charge(LbWorkBudget *,uint64_t);
void lb_work_budget_escape(LbWorkBudget *);
LbWorkUsage lb_work_budget_usage(const LbWorkBudget *);
uint64_t lb_work_bulk_units(uint64_t); /* ceil(bytes or words / 16), no overflow */
/* Prepare leases before PID/runnable publication; publishing is infallible.
 * Abort consumes any prepared prefix. Process teardown consumes published leases. */
LbAllocStatus lb_work_process_prepare(LbWorkBudget *,LbWorkProcessLease *);
void lb_work_process_abort(LbWorkProcessLease *);
void lb_work_process_publish(LbProcess *,LbWorkProcessLease *);
void lb_work_process_release(LbProcess *);
void lb_work_domain_borrow(LbAllocDomain *,LbWorkBudget *);
LbWorkBudget *lb_work_domain_owner(const LbAllocDomain *);
int lb_work_domain_charge(LbAllocDomain *,uint64_t);
void lb_work_domain_clear_failure(LbAllocDomain *);
int lb_work_receive_begin(LbProcess *,struct LbMessage *);
void lb_work_receive_end(LbProcess *,int consumed);
void lb_work_cross_effect(LbWorkBudget *,LbWorkBudget *);
#endif
