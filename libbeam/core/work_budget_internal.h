/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef LIBBEAM_CORE_WORK_BUDGET_INTERNAL_H
#define LIBBEAM_CORE_WORK_BUDGET_INTERNAL_H
#include "work_budget.h"
struct LbWorkBudget {
    LbWorld *world;
    LbWorkBudget *next;
    size_t references;
    uint64_t limit,remaining,denied_charge;
    int exhausted,escaped;
};
#endif
