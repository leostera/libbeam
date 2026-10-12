/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef LIBBEAM_CORE_ALLOC_INTERNAL_H
#define LIBBEAM_CORE_ALLOC_INTERNAL_H
#include "alloc.h"
typedef union LbBlock LbBlock;
/* Stable C metadata, embedded in a world's host control. Child backing is not
 * simultaneously charged to the shared Engine. Parent callback lifetime is
 * retained until the embedded child and every allocation have been released. */
struct LbAllocDomain {
    LbSystemAllocator system;
    LbBlock *blocks;
    LbAllocDomain *parent;
    size_t children,current,peak,limit;
    LbAllocStatus failure;
};
/* Fresh zeroed storage only. A child is destroyed before its containing control.
 * Init performs no callback/allocation; destroy refuses live allocations/users. */
LbAllocStatus lb_alloc_scope_init(LbAllocDomain *parent,LbAllocDomain *storage,size_t limit);
#endif
