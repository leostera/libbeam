/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1996-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Native local-fun layouts from erl_fun.h and erl_term.h.
 */
#ifndef LIBBEAM_CORE_FUN_H
#define LIBBEAM_CORE_FUN_H
#include "code_internal.h"
#define LB_FUN_ARITY_SHIFT 8u
#define LB_FUN_ENV_SHIFT 16u
#define LB_FUN_KIND_SHIFT 24u
#define LB_FUN_WORDS 2u
#define LB_FUN_HEADER(arity,free) (FUN_SUBTAG|((Uint)(arity)<<LB_FUN_ARITY_SHIFT)|((Uint)(free)<<LB_FUN_ENV_SHIFT))
typedef struct LbFunEntry {
    LbDispatchable dispatch;
    const BeamInstr *pend_purge_address;
    Eterm module;
    unsigned char uniq[16];
    int arity,index,old_uniq,old_index;
    /* Owned sidecar after the native entry prefix. */
    LbCodeModule *owner;
} LbFunEntry;
typedef struct {
    Eterm thing_word;
    union { const LbFunEntry *fun; const Export *exp; } entry;
    Eterm env[];
} LbFun;
_Static_assert(sizeof(LbFun)==LB_FUN_WORDS*sizeof(Eterm),"native fun prefix");
_Static_assert(offsetof(LbFunEntry,owner)==9*sizeof(Eterm),"native local entry prefix before owner sidecar");
static inline size_t lb_fun_free(const LbFun *fun) { return (fun->thing_word>>LB_FUN_ENV_SHIFT)&255u; }
static inline unsigned lb_fun_arity(const LbFun *fun) { return (unsigned)((fun->thing_word>>LB_FUN_ARITY_SHIFT)&255u); }
/* Only valid owned local funs. External-fun recognition/admission is separate. */
static inline const LbFun *lb_fun(Eterm value)
{
    if(!is_boxed(value) || (*boxed_val(value)&_TAG_HEADER_MASK)!=FUN_SUBTAG) return NULL;
    const LbFun *fun=(const LbFun *)boxed_val(value);
    if((fun->thing_word>>LB_FUN_KIND_SHIFT) || (fun->thing_word&0xc0u) || !fun->entry.fun) abort();
    return fun;
}
typedef struct LbFunLease {
    struct LbFunLease *next;
    const LbFunEntry *entry;
    int marked;
} LbFunLease;
/* Retain before publishing a fun or copying one into the process; no borrowed
 * entry survives module retirement. The fallible step does not move the heap. */
LbAllocStatus lb_fun_retain(LbProcess *,const LbFunEntry *);
void lb_fun_leases_clear(LbProcess *);
/* Infallible GC commit: reset marks, mark each copied live fun, sweep afterward.
 * Every live entry must already have a lease. Missing leases are invariants. */
void lb_fun_marks_clear(LbProcess *);
void lb_fun_mark(LbProcess *,const LbFunEntry *);
Uint lb_fun_mark_code(LbProcess *); /* in-flight PC and saved continuations */
void lb_fun_sweep(LbProcess *);
const BeamInstr *lb_fun_call(LbProcess *,unsigned arity);
LbAllocStatus lb_fun_prepare_instruction(LbProcess *,const BeamInstr *,Uint *cost);
#endif
