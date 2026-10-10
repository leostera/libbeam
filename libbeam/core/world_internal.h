/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef LIBBEAM_CORE_WORLD_INTERNAL_H
#define LIBBEAM_CORE_WORLD_INTERNAL_H
#include "world.h"
#include "code_internal.h"
#include "executor.h"
typedef enum { LB_WORLD_LOADING, LB_WORLD_RUNNING, LB_WORLD_STOPPING, LB_WORLD_RECLAIMED } LbWorldPhase;
struct LbWorld {
    LbEngine *engine;
    LbCodeSpace *space;
    LbWorld *next;
    LbCall *calls;
    size_t references, outstanding, queued_bytes;
    LbWorldPhase phase;
    int owner_live;
};
struct LbCall {
    LbWorld *world;
    LbCall *next;
    LbTask *task;
    unsigned char *bytes;
    size_t size, charge;
    LbWorldStatus status;
    int owner_live, slot_held, consumed;
};
struct LbReclamation { LbWorld *world; int consumed; };
/* These helpers run with the Engine mutex held, including on its worker.
 * Completion is an owned runtime operation, never an arbitrary host callback. */
void lb_call_complete(LbCall *,LbProcess *,LbTaskStatus);
void lb_call_discard_completed(LbCall *);
int lb_world_retire_step(LbEngine *);
LbEngineStatus lb_task_create_call_locked(LbEngine *,const LbCodeEntry *,LbBytesView,LbCall *,LbTask **);
void lb_task_cancel_locked(LbTask *);
void lb_task_release_locked(LbTask *);
#endif
