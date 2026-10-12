/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef LIBBEAM_CORE_RESOURCE_POLICY_H
#define LIBBEAM_CORE_RESOURCE_POLICY_H
#include <stddef.h>
#include <stdint.h>
#define LB_DEFAULT_MEMORY_BYTES (256u * 1024u * 1024u)
#define LB_DEFAULT_INVOCATION_WORK UINT64_C(10000000)
typedef struct { size_t memory_bytes; uint64_t invocation_work; } LbResourceLimits;
/* Explicit presence permits zero to be rejected, never treated as unlimited. */
typedef struct {
    int has_memory,has_work;
    size_t memory_bytes;
    uint64_t invocation_work;
} LbResourceOverrides;
static inline LbResourceLimits lb_resource_defaults(void)
{ LbResourceLimits limits={LB_DEFAULT_MEMORY_BYTES,LB_DEFAULT_INVOCATION_WORK}; return limits; }
static inline int lb_resource_limits_valid(LbResourceLimits limits)
{ return limits.memory_bytes!=0 && limits.invocation_work!=0; }
#endif
