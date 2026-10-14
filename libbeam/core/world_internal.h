/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef LIBBEAM_CORE_WORLD_INTERNAL_H
#define LIBBEAM_CORE_WORLD_INTERNAL_H
#include "world.h"
#include "code_internal.h"
#include "executor.h"
#include "alloc_internal.h"
#include "work_budget.h"
typedef enum { LB_WORLD_LOADING, LB_WORLD_RUNNING, LB_WORLD_STOPPING, LB_WORLD_RECLAIMED } LbWorldPhase;
struct LbWorld {
    LbEngine *engine;
    LbAllocDomain domain;
    LbResourceLimits limits;
    LbCodeSpace *space;
    LbWorld *next;
    LbCall *calls;
    LbWorkBudget *budgets;
    size_t references, outstanding, queued_bytes;
    LbWorldPhase phase;
    int owner_live;
};
struct LbCall {
    LbWorld *world;
    LbCall *next;
    LbTask *task;
    LbWorkBudget *budget;
    unsigned char *bytes;
    size_t size, charge;
    LbWorldStatus status;
    const LbCapabilityFailure *capability_failure; /* static, survives physical reclamation */
    int owner_live, slot_held, consumed;
};
struct LbReclamation { LbWorld *world; int consumed; };
/* Serialized host-control variants, shared with the public operations. Require
 * an active non-reentrant Engine borrow; they never interpret guest code.
 * Keeping publication and cancellation in one borrow exposes the genuinely
 * queued state without adding scheduler gates or timing-dependent tests. */
LbWorldStatus lb_world_start_locked(LbWorld *,LbBytesView,LbBytesView,LbBytesView,LbCall **);
LbWorldStatus lb_call_cancel_locked(LbCall *);
LbWorldStatus lb_world_stop_locked(LbWorld *,LbReclamation **);
/* These helpers run with the Engine mutex held, including on its worker.
 * Completion is an owned runtime operation, never an arbitrary host callback. */
void lb_call_complete(LbCall *,LbProcess *,LbTaskStatus);
void lb_call_discard_completed(LbCall *);
int lb_world_retire_step(LbEngine *);
LbEngineStatus lb_task_create_call_locked(LbEngine *,const LbCodeEntry *,LbBytesView,LbCall *,LbTask **);
typedef struct LbMonitor LbMonitor;
typedef struct LbSpawnMfa LbSpawnMfa;
LbProcessStatus lb_task_spawn_mfa_monitored_locked(LbWorld *,LbProcess *,const LbSpawnMfa *,LbMonitor *,Eterm *);
LbProcessStatus lb_task_spawn_fun_monitored_locked(LbWorld *,LbProcess *,Eterm,LbMonitor *,Eterm *);
LbProcessStatus lb_task_spawn_fun_locked(LbWorld *,LbProcess *,Eterm,Eterm *);
/* Internal component seam: use a worker-produced retained closure, without
 * exposing the source process or interpreting on the host control thread. */
LbProcessStatus lb_task_spawn_result_locked(LbWorld *,LbTask *,Eterm *);
void lb_task_cancel_world_children_locked(LbWorld *);
void lb_task_cancel_family_locked(LbWorkBudget *,LbTaskStatus);
void lb_world_close_for_limit_locked(LbWorld *);
void lb_task_cancel_locked(LbTask *);
void lb_task_release_locked(LbTask *);
#endif
