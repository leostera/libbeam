/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef LIBBEAM_CORE_BINARY_INTERNAL_H
#define LIBBEAM_CORE_BINARY_INTERNAL_H
#include "binary.h"
/* Unpublished reference, refcount one. Caller must release or attach to a real
 * BinRef. Payload bytes are uninitialized; no term is published by allocation. */
LbBinary *lb_binary_allocate(LbAllocDomain *,size_t);
void lb_sub_bits_init(LbSubBits *,Uint,Eterm,const void *,Uint,Uint);
/* Transfer one initialized, unpublished immutable backing into a BinRef/SubBits pair.
 * Caller reserves LB_REFC_BITS_WORDS and checks offheap capacity accounting.
 * No allocation/failure; flags are 0 or TAG_LITERAL_PTR. */
Eterm lb_binary_wrap(LbOffHeap *,Eterm **,LbBinary *,size_t bits,Uint flags);
/* Full-copy GC only, after normal sweep. No allocation/shrink after forwarding. */
void lb_writable_sweep(LbOffHeap *,LbOffHeap *);
#endif
