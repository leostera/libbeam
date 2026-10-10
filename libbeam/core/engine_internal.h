/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1996-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Shared implementation catalog; private mutable exports remain in code spaces.
 */
#ifndef LIBBEAM_CORE_ENGINE_INTERNAL_H
#define LIBBEAM_CORE_ENGINE_INTERNAL_H
#include "engine.h"
#include "term.h"
#include <pthread.h>
typedef struct LbTask LbTask;

typedef Uint BeamInstr;
typedef struct LbProcess LbProcess;
typedef Eterm (*LbBifFn)(LbProcess *,Eterm *,const BeamInstr *);
typedef enum { BIF_KIND_REGULAR, BIF_KIND_HEAVY, BIF_KIND_GUARD } BifKind;
typedef struct { Eterm module,name; int arity; LbBifFn f; BifKind kind; } BifEntry;
#define LB_NATIVE_COUNT 2
struct LbEngine {
    LbAllocDomain *domain;
    const BifEntry *bifs; /* immutable catalog, owned until shutdown */
    unsigned native_ids[LB_NATIVE_COUNT];
    size_t spaces;
    int owner_live, closed;
    pthread_mutex_t mutex;
    pthread_cond_t wake;
    pthread_t worker;
    int sync_ready, worker_started, worker_stop, control_borrow;
    LbTask *tasks, *cursor;
};
LbEngineStatus lb_executor_init(LbEngine *);
void lb_executor_shutdown(LbEngine *); /* no tasks; caller does not hold mutex */
void lb_engine_release_if_detached(LbEngine *);
/* Publication follows complete construction. Release follows physical child
 * destruction, never logical stop. Caller obeys serialized/no-reentry contract. */
void lb_engine_space_published(LbEngine *);
void lb_engine_space_released(LbEngine *);
#endif
