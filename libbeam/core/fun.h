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
    Eterm function;
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
static inline int lb_fun_is_external(const LbFun *fun) { return (int)(fun->thing_word>>LB_FUN_KIND_SHIFT); }
/* External funs have no environment; their export is resolved before code
 * publication. Local fun entries must already belong to the owning code space. */
static inline const LbFun *lb_fun(Eterm value)
{
    if(!is_boxed(value) || (*boxed_val(value)&_TAG_HEADER_MASK)!=FUN_SUBTAG) return NULL;
    const LbFun *fun=(const LbFun *)boxed_val(value);
    if((fun->thing_word&0xc0u) || lb_fun_is_external(fun)>1 ||
       (lb_fun_is_external(fun) ? lb_fun_free(fun)!=0 : !fun->entry.fun)) abort();
    return fun;
}
typedef struct LbFunLease {
    struct LbFunLease *next;
    const LbFunEntry *entry;
    LbCodeModule *owner;
    int marked;
} LbFunLease;
/* Retain before publishing a fun or copying one into the process; no borrowed
 * entry survives module retirement. The fallible step does not move the heap. */
LbAllocStatus lb_fun_retain(LbProcess *,const LbFunEntry *);
LbAllocStatus lb_fun_retain_export(LbCodeSpace *,LbFunLease **,const Export *,Uint *cost);
void lb_fun_mark_export(LbProcess *,const Export *);
/* Valid owned terms only. Prepare all embedded defining modules before copy.
 * A failure may leave additional destination leases; destroy the unpublished
 * destination to roll back. cost is incremented by actual visited terms. */
LbAllocStatus lb_fun_retain_term(LbProcess *,Eterm,Uint *cost);
/* Independent term owners (native message fragments) retain code without a
 * fake process or a second runtime graph. Failure leaves owned prefix leases
 * for exact rollback through release_roots. */
LbAllocStatus lb_fun_retain_roots(LbCodeSpace *,LbFunLease **,Eterm,Uint *cost);
/* Same owned term walk, pinning nested writable backing before message copy. */
LbAllocStatus lb_fun_retain_message_roots(LbCodeSpace *,LbFunLease **,Eterm,Uint *cost);
void lb_fun_release_roots(LbCodeSpace *,LbFunLease **);
void lb_fun_release_prefix(LbCodeSpace *,LbFunLease **,LbFunLease *stop);
void lb_fun_leases_clear(LbProcess *);
/* Infallible GC commit: reset marks, mark each copied live fun, sweep afterward.
 * Every live entry must already have a lease. Missing leases are invariants. */
void lb_fun_marks_clear(LbProcess *);
void lb_fun_mark(LbProcess *,const LbFunEntry *);
Uint lb_fun_mark_code(LbProcess *); /* in-flight PC and saved continuations */
void lb_fun_sweep(LbProcess *);
const BeamInstr *lb_fun_call(LbProcess *,unsigned arity);
LbAllocStatus lb_fun_prepare_instruction(LbProcess *,const BeamInstr *,Uint *cost);
int lb_fun_is_arity(Eterm,Uint);
Eterm lb_fun_test_arity(LbProcess *,Eterm,Eterm);
Eterm lb_bif_fun_info_1(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_fun_info_2(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_fun_info_mfa_1(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_fun_to_list_1(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_make_fun_3(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_is_function_1(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_is_function_2(LbProcess *,Eterm *,const BeamInstr *);
#endif
