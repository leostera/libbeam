/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef LIBBEAM_CORE_TIME_UNIT_H
#define LIBBEAM_CORE_TIME_UNIT_H
#include <stdint.h>
/* Owned monotonic/deadline clock values are nanoseconds, not host raw ticks. */
#define LB_NATIVE_TIME_UNITS_PER_SECOND UINT64_C(1000000000)
#endif
