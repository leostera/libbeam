/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef ERL_ALLOCATOR_DOMAIN_H__
#define ERL_ALLOCATOR_DOMAIN_H__

#include "erl_alloc.h"
struct ErtsEngine;
typedef struct ErtsAllocatorDomain ErtsAllocatorDomain;
/* Requires initialized platform threading. No VM allocation or world creation.
 * This creates allocator control storage, not initialized backing allocators. */
ErtsAllocatorDomain *erts_allocator_domain_create(void);
/* Exclusive ownership. Refuses published or resource-bearing domains. */
int erts_allocator_domain_discard(ErtsAllocatorDomain *);
/* Bootstrap storage is independent of VM allocators, whose own metadata uses
 * it. Release is unpublished-only and requires disconnected/destroyed contents.
 * Bound backends must first gain coordinated shutdown; never force-free them. */
void *erts_allocator_bootstrap_alloc(ErtsAllocatorDomain *, UWord size, UWord alignment);
int erts_allocator_bootstrap_release(ErtsAllocatorDomain *, void *);

typedef struct {
    UWord control_bytes;
    UWord backing_bytes;
    UWord started_instances;
    UWord bootstrap_blocks;
    UWord permanent_blocks;
    UWord permanent_bytes;
} ErtsAllocatorOwnership;
int erts_allocator_ownership(struct ErtsEngine *, ErtsAllocatorOwnership *);
/* Release an exact aligned allocation after its owner has destroyed contained
 * resources and disconnected all users. Wrong engine/type/interior pointers
 * are refused. Never bulk-release reachable engine structures. */
int erts_allocator_release_permanent(struct ErtsEngine *, ErtsAlcType_t, void *);
#endif
