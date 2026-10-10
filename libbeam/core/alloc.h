/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef LIBBEAM_CORE_ALLOC_H
#define LIBBEAM_CORE_ALLOC_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Internal, serialized bootstrap ownership primitive; not a guest allocator,
 * Engine, GC, or concurrent API. Callback/context lifetime includes domain
 * destruction. Allocations must have malloc-equivalent alignment; callbacks
 * must not reenter the domain. A NULL allocator selects malloc/free.
 */
typedef struct {
    void *context;
    void *(*allocate)(void *context, size_t size);
    void (*release)(void *context, void *allocation);
} LbSystemAllocator;
typedef struct LbAllocDomain LbAllocDomain;
typedef enum {
    LB_ALLOC_OK,
    LB_ALLOC_INVALID,
    LB_ALLOC_NO_MEMORY,
    LB_ALLOC_BUSY
} LbAllocStatus;

/* Valid output pointers are set to NULL on failure. Zero-size allocation is
 * invalid. Allocation results are max_align_t-aligned, not initialized.
 */
LbAllocStatus lb_alloc_domain_create(const LbSystemAllocator *, LbAllocDomain **);
LbAllocStatus lb_alloc_domain_allocate(LbAllocDomain *, size_t, void **);
/* Exact live allocation from this domain only. Does not dereference an unknown
 * input pointer. This is ownership checking, not protection against stale-pointer
 * address reuse; callers must obey allocation lifetimes.
 */
LbAllocStatus lb_alloc_domain_release(LbAllocDomain *, void *);
/* Refuses nonempty domains without mutation; never force-frees live users.
 * On success the domain pointer is invalid and may not be used again.
 */
LbAllocStatus lb_alloc_domain_destroy(LbAllocDomain *);
#ifdef __cplusplus
}
#endif
#endif
