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
typedef struct LbWorld LbWorld;

typedef Uint BeamInstr;
typedef struct LbProcess LbProcess;
typedef Eterm (*LbBifFn)(LbProcess *,Eterm *,const BeamInstr *);
typedef enum { BIF_KIND_REGULAR, BIF_KIND_HEAVY, BIF_KIND_GUARD } BifKind;
typedef struct { Eterm module,name; int arity; LbBifFn f; BifKind kind; } BifEntry;
#define LB_NATIVE_COUNT 125 /* 97 local implementations, 27 denials, one compatibility exception */
#define LB_NATIVE_EXTENSION_COUNT 19u
struct LbEngine {
    LbAllocDomain *domain;
    LbResourceLimits limits;
    Eterm empty_tuple[2]; /* immutable after construction; native read-ahead word */
    const BifEntry *bifs; /* immutable catalog, owned until shutdown */
    void *numeric_locale; /* Engine-owned C numeric locale for exact text parsing */
    unsigned native_ids[LB_NATIVE_COUNT];
    size_t spaces;
    uint64_t reference_counter; /* serialized native reference-number generator */
    int owner_live, closed;
    pthread_mutex_t mutex;
    pthread_cond_t wake;
    pthread_t worker;
    int sync_ready, worker_started, worker_stop, control_borrow;
    LbTask *tasks, *cursor;
    LbWorld *worlds;
};
LbEngineStatus lb_executor_init(LbEngine *);
void lb_executor_shutdown(LbEngine *); /* no tasks; caller does not hold mutex */
/* Inspect under the mutex (or before a worker exists). Dispose only on host
 * control after unlock, once no remaining child can mutate the ownership graph. */
int lb_engine_is_detached(const LbEngine *);
void lb_engine_release_detached(LbEngine *);
/* Publication follows complete construction. Release follows physical child
 * destruction, never logical stop. Caller obeys serialized/no-reentry contract. */
void lb_engine_space_published(LbEngine *);
void lb_engine_space_released(LbEngine *);
#endif
