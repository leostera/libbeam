/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef LIBBEAM_CORE_ALLOC_INTERNAL_H
#define LIBBEAM_CORE_ALLOC_INTERNAL_H
#include "alloc.h"
typedef union LbBlock LbBlock;
typedef struct LbWorkBudget LbWorkBudget;
/* Stable C metadata, embedded in a world's host control. Child backing is not
 * simultaneously charged to the shared Engine. Parent callback lifetime is
 * retained until the embedded child and every allocation have been released. */
struct LbAllocDomain {
    LbSystemAllocator system;
    LbBlock *blocks;
    LbAllocDomain *parent;
    size_t children,current,peak,limit;
    size_t block_count; /* exact live registry length, including allocation headers */
    LbAllocStatus failure;
    LbWorkBudget *work; /* borrowed only within the serialized operation's owned lease */
    /* First denied owner stays leased until cleanup; later quota/OOM results
     * cannot overwrite causal failure attribution. */
    LbWorkBudget *failed_work;
};
/* Fresh zeroed storage only. A child is destroyed before its containing control.
 * Init performs no callback/allocation; destroy refuses live allocations/users. */
LbAllocStatus lb_alloc_scope_init(LbAllocDomain *parent,LbAllocDomain *storage,size_t limit);
/* Trusted internal release for exact live pointers returned by this domain. */
LbAllocStatus lb_alloc_domain_release_owned(LbAllocDomain *,void *);
#endif
