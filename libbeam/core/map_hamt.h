/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef LIBBEAM_CORE_MAP_HAMT_H
#define LIBBEAM_CORE_MAP_HAMT_H
#include "alloc.h"
#include "term.h"
#include "process.h"
#include <stddef.h>
typedef enum { LB_HAMT_OK, LB_HAMT_MISSING, LB_HAMT_WORK_LIMIT, LB_HAMT_UNSUPPORTED,
               LB_HAMT_INVALID, LB_HAMT_NO_MEMORY } LbHamtStatus;
typedef struct { Eterm key,value; uint64_t hash; } LbHamtPair;
typedef Eterm *(*LbHamtAllocate)(void *context,size_t words);
/* Native OTP bitmap-HAMT layout for map sizes above the flatmap threshold.
 * This key profile hashes immediates and proper byte lists. Other key classes
 * and full-hash collisions fail closed. Build allocations belong to the
 * caller's unpublished arena; the caller must reclaim all prefixes on failure.
 */
int lb_hamt_is_root(Eterm);
int lb_hamt_is_node(Eterm);
size_t lb_hamt_size(Eterm);
size_t lb_hamt_boxed_words(const Eterm *);
LbHamtStatus lb_hamt_hash_key(Eterm,uint64_t *);
LbHamtStatus lb_hamt_build(LbHamtPair *,size_t,LbHamtAllocate,void *,Uint pointer_tag,Eterm *);
LbHamtStatus lb_hamt_get(LbProcess *,Eterm,Eterm,Eterm *);
#endif
