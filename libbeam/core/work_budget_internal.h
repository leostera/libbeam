/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef LIBBEAM_CORE_WORK_BUDGET_INTERNAL_H
#define LIBBEAM_CORE_WORK_BUDGET_INTERNAL_H
#include "work_budget.h"
#include "capability.h"
struct LbWorkBudget {
    LbWorld *world;
    LbAllocDomain *domain;
    struct LbCall *call; /* borrowed pending completion; cleared before it retires */
    LbWorkBudget *previous,*next;
    size_t references;
    uint64_t limit,remaining,denied_charge;
    int exhausted,escaped;
    const LbCapabilityFailure *capability_failure; /* first causal denial; static storage */
};
#endif
