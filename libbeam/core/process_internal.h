/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1996-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef LIBBEAM_CORE_PROCESS_INTERNAL_H
#define LIBBEAM_CORE_PROCESS_INTERNAL_H
#include "process.h"
#include "exception.h"
#include "code_internal.h"
#include "decimal.h"
#include "binary_vheap.h"
struct LbProcess {
    LbCodeModule *entry_module;
    const LbMFA *current;
    LbMFA local_mfa; /* owned copy of a private function's native word header */
    const BeamInstr *i;
    Eterm reg[MAX_REG];
    Eterm *heap, *htop, *stop, *hend;
    LbOffHeap off_heap, writable;
    uint64_t bin_vheap_sz; /* owned soft collection threshold, in words */
    Eterm construction_root; /* explicit temporary root during fallible append */
    LbDecimal *decimal; /* sole pending native continuation; independent scratch */
    Uint freason; /* Native encoded scalar, never a GC root. */
    Eterm fvalue,exception_value; /* Pending payload and expanded terminal value. */
    Uint arity, last_gc_cost, last_native_cost;
    size_t collections, heap_reserved;
    LbProcessStatus status;
    int running;
};
int lb_process_collect_live(LbProcess *,size_t,size_t);
int lb_heap_reserve(LbProcess *,size_t,size_t);
int lb_heap_reserve_binary(LbProcess *,size_t need,size_t incoming_bytes,size_t live);
/* Conservative native-term alias check, including dead allocated heap objects.
 * Unknown object families abort rather than silently hiding possible roots. */
int lb_process_has_other_reference(const LbProcess *,Eterm,const Eterm *except);
int lb_flat_size(LbAllocDomain *,Eterm,size_t *);
/* Fallible only on binary reference/overhead exhaustion after size/reserve.
 * Failure rolls back the new offheap prefix and heap top, never shared source. */
Eterm lb_copy_flat(Eterm,Eterm **,LbOffHeap *);
extern const BeamInstr lb_host_return[1];
#endif
