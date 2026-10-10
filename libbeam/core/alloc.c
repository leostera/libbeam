/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#include "alloc.h"
#include <stdint.h>
#include <stdlib.h>

/* Newly authored bootstrap code, not a transplanted BEAM allocator. The linked
 * list is intentionally for a small construction graph, not term allocations.
 */
typedef union LbBlock {
    union LbBlock *next;
    max_align_t alignment;
} LbBlock;
struct LbAllocDomain {
    LbSystemAllocator system;
    LbBlock *blocks;
};
static void *system_allocate(void *context, size_t size)
{
    (void)context;
    return malloc(size);
}
static void system_release(void *context, void *allocation)
{
    (void)context;
    free(allocation);
}

LbAllocStatus lb_alloc_domain_create(const LbSystemAllocator *allocator,
                                    LbAllocDomain **out)
{
    const LbSystemAllocator defaults = {NULL, system_allocate, system_release};
    LbAllocDomain *domain;
    if (!out) return LB_ALLOC_INVALID;
    *out = NULL;
    if (!allocator) allocator = &defaults;
    if (!allocator->allocate || !allocator->release) return LB_ALLOC_INVALID;
    domain = allocator->allocate(allocator->context, sizeof(*domain));
    if (!domain) return LB_ALLOC_NO_MEMORY;
    domain->system = *allocator;
    domain->blocks = NULL;
    *out = domain;
    return LB_ALLOC_OK;
}

LbAllocStatus lb_alloc_domain_allocate(LbAllocDomain *domain, size_t size, void **out)
{
    LbBlock *block;
    if (!out) return LB_ALLOC_INVALID;
    *out = NULL;
    if (!domain || !size) return LB_ALLOC_INVALID;
    if (size > SIZE_MAX - sizeof(*block)) return LB_ALLOC_NO_MEMORY;
    block = domain->system.allocate(domain->system.context, sizeof(*block) + size);
    if (!block) return LB_ALLOC_NO_MEMORY;
    block->next = domain->blocks;
    domain->blocks = block;
    *out = block + 1;
    return LB_ALLOC_OK;
}

LbAllocStatus lb_alloc_domain_release(LbAllocDomain *domain, void *allocation)
{
    LbBlock **link, *block;
    if (!domain || !allocation) return LB_ALLOC_INVALID;
    for (link = &domain->blocks; (block = *link) != NULL; link = &block->next) {
        if ((void *)(block + 1) == allocation) {
            *link = block->next;
            domain->system.release(domain->system.context, block);
            return LB_ALLOC_OK;
        }
    }
    return LB_ALLOC_INVALID;
}

LbAllocStatus lb_alloc_domain_destroy(LbAllocDomain *domain)
{
    LbSystemAllocator system;
    if (!domain) return LB_ALLOC_INVALID;
    if (domain->blocks) return LB_ALLOC_BUSY;
    system = domain->system;
    system.release(system.context, domain);
    return LB_ALLOC_OK;
}
