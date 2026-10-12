/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef LIBBEAM_CORE_WORK_BUDGET_H
#define LIBBEAM_CORE_WORK_BUDGET_H
#include "alloc.h"
#include <stdint.h>
typedef struct LbWorld LbWorld;
typedef struct LbWorkBudget LbWorkBudget;
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
#endif
