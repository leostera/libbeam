/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1997-2025. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef LIBBEAM_CORE_PROCESS_DICT_H
#define LIBBEAM_CORE_PROCESS_DICT_H
#include "term.h"
/* Native ProcDict field order and bucket representation: NIL, {Key,Value}, or
 * a proper collision list of pairs. This profile retains eight buckets rather
 * than incremental rehashing, and admits only native fast-hash atom/small keys.
 * Pair/list terms live on the process heap; this stable array is a GC root. */
#define LB_PD_SLOTS 8u
typedef struct {
    unsigned int sizeMask,usedSlots,arraySize,splitPosition;
    Uint numElements;
    Eterm data[LB_PD_SLOTS];
} LbProcDict;
_Static_assert(offsetof(LbProcDict,data)==4*sizeof(unsigned int)+sizeof(Uint),"native ProcDict prefix");
#endif
