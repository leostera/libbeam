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
#include "pid_table.h"
#include "fun.h"
#include "message.h"
#include "registry.h"
#include "receive_marker.h"
#include "monitor.h"
#include "link.h"
#include "work_budget.h"
#include "process_dict.h"
#include "capability.h"
#define F_TRAP_EXIT (1u<<0)
#define F_INSLPQUEUE (1u<<1)
#define F_TIMO (1u<<2)
#define F_DELAY_GC (1u<<13) /* pinned native matching/schedule-out flag */
struct LbProcess {
    LbCodeModule *entry_module;
    LbWorld *world; /* borrowed C task owner; task retirement precedes world destruction */
    LbWorkBudget *work_birth,*work_active,*work_previous; /* owned stable metadata leases */
    LbMessage *work_candidate; /* borrowed queue node, never a moving term */
    LbPidEntry identity; /* stable allocation, never a moving-heap pointer */
    LbMonitorNode *monitors_origin,*monitors_target;
    LbLinkHalf *links; /* symmetric metadata, detached before either process dies */
    Uint bitdata; /* native matching accumulator survives host-budget safepoints */
    LbMessage *exit_signal; /* owned first terminal signal, including sender-causal credit */
    LbRegisteredProcess *registered; /* borrowed association owned by code space */
    LbFunLease *fun_leases; /* one lease per defining module, represented by a stable entry */
    LbMessageQueue messages; /* native inner queue; immutable fragments own their code/offheap roots */
    int waiting; /* serialized scheduler state, not a terminal result */
    Uint flags;
    Sint fcalls;
    int receive_timed_out;
    uint64_t receive_deadline;
    const BeamInstr *receive_timeout_pc;
    LbMessage *receive_message; /* borrowed selected node; cleared before unlink/advance */
    Eterm receive_root; /* actor-owned matching view; registered even across scheduler yields */
    const LbMFA *current;
    LbMFA local_mfa; /* owned copy of a private function's native word header */
    const BeamInstr *i;
    Eterm reg[MAX_REG];
    Eterm *heap, *htop, *stop, *hend;
    LbOffHeap off_heap, writable;
    uint64_t bin_vheap_sz; /* owned soft collection threshold, in words */
    LbProcDict *dictionary; /* independent owned metadata; data[] are moving roots */
    Eterm error_handler; /* atom-only process setting; defaults independently on spawn */
    Eterm group_leader; /* local PID; NIL until a root's own identity is observed */
    Eterm construction_root; /* explicit temporary root during fallible append */
    Eterm native_time_root; /* intermediate native time term across unit conversion GC */
    LbDecimal *decimal; /* sole pending native continuation; independent scratch */
    Uint freason; /* Native encoded scalar, never a GC root. */
    Eterm fvalue,exception_value; /* Pending payload and expanded terminal value. */
    Eterm ftrace; /* Owned opaque supplied trace; moving root, never a raw code pointer. */
    const BeamInstr *native_callsite,*native_trace_previous; /* owned code leases, not heap aliases */
    Uint arity, last_gc_cost, last_native_cost;
    size_t collections, heap_reserved;
    size_t last_binary_bytes; /* zero/copy work reported by one storage operation */
    LbProcessStatus status;
    const LbCapabilityFailure *capability_failure; /* static diagnostic, not a term */
    int running;
};
LbProcessStatus lb_process_identity(LbProcess *,Eterm *);
LbProcessStatus lb_process_group_leader(LbProcess *,Eterm *);
/* Charge before denial; failure must bypass guest handlers and guards. */
Eterm lb_process_deny_capability(LbProcess *,const LbCapabilityFailure *);
/* Prepare an unpublished local-fun/0 child with copied environment and PID.
 * Domain/namespace serialization is required; queue publication is separate. */
LbProcessStatus lb_process_create_fun(LbProcess *,Eterm,size_t,LbProcess **);
/* Parsed MFA start: namespace atoms and a proper list of exactly arity terms.
 * The caller keeps arguments rooted; preparation never collects its heap. */
typedef struct LbSpawnMfa { Eterm module,function,arguments; unsigned arity; } LbSpawnMfa;
LbProcessStatus lb_process_create_mfa(LbProcess *,const LbSpawnMfa *,size_t,LbProcess **);
int lb_process_collect_live(LbProcess *,size_t,size_t);
int lb_heap_reserve(LbProcess *,size_t,size_t);
int lb_heap_reserve_binary(LbProcess *,size_t need,size_t incoming_bytes,size_t live);
/* Conservative native-term alias check, including dead allocated heap objects.
 * Unknown object families abort rather than silently hiding possible roots. */
int lb_process_has_other_reference(const LbProcess *,Eterm,const Eterm *except);
int lb_flat_size(LbAllocDomain *,Eterm,size_t *);
/* Fallible only on binary reference/overhead exhaustion after size/reserve.
 * Failure rolls back the new offheap prefix and heap top, never shared source.
 * For fun-containing terms the caller must first retain every defining entry
 * in the destination process with lb_fun_retain. This primitive copies term
 * storage only; it neither acquires nor transfers code leases. */
Eterm lb_copy_flat(Eterm,Eterm **,LbOffHeap *);
extern const BeamInstr lb_host_return[1];
#endif
