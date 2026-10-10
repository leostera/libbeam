/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef LIBBEAM_CORE_WORLD_H
#define LIBBEAM_CORE_WORLD_H
#include "engine.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct LbWorld LbWorld;
typedef struct LbCall LbCall;
typedef struct LbReclamation LbReclamation;
typedef enum {
    LB_WORLD_OK, LB_WORLD_PENDING, LB_WORLD_INVALID, LB_WORLD_INVALID_STATE,
    LB_WORLD_CLOSED, LB_WORLD_FULL, LB_WORLD_LIMIT, LB_WORLD_NO_MEMORY,
    LB_WORLD_BAD_BEAM, LB_WORLD_UNSUPPORTED, LB_WORLD_NOT_FOUND,
    LB_WORLD_EXCEPTION, LB_WORLD_CANCELLED, LB_WORLD_BAD_RESULT
} LbWorldStatus;
typedef struct { const unsigned char *data; size_t size; } LbBytesView;
#define LB_CALL_BYTES_LIMIT (64u * 1024u)
#define LB_CALL_COUNT_LIMIT 64u
#define LB_CALL_QUEUE_LIMIT (1024u * 1024u)

/* Serialized host control. Actual guest execution occurs only on the Engine
 * worker. Opaque handles retain finite control state, not reclaimed VM storage. */
LbWorldStatus lb_world_create(LbEngine *,LbWorld **);
LbWorldStatus lb_world_load(LbWorld *,LbBytesView);
LbWorldStatus lb_world_start(LbWorld *,LbBytesView module,LbBytesView function,LbBytesView input,LbCall **);
LbWorldStatus lb_world_call(LbWorld *,LbBytesView module,LbBytesView function,LbBytesView input,LbCall **);
LbWorldStatus lb_world_stop(LbWorld *,LbReclamation **);
/* Drop closes admission and physically drains this selected world. Pending calls
 * become cancelled; completed results survive in their own host controls. */
void lb_world_release(LbWorld *);
/* Poll never runs BEAM. The result view is borrowed until consume/release.
 * A host-copy allocation failure can leave a ready completion unconsumed. */
LbWorldStatus lb_call_poll(LbCall *,LbBytesView *);
LbWorldStatus lb_call_consume(LbCall *);
void lb_call_release(LbCall *);
LbWorldStatus lb_reclamation_poll(LbReclamation *);
LbWorldStatus lb_reclamation_consume(LbReclamation *);
void lb_reclamation_release(LbReclamation *);
#ifdef __cplusplus
}
#endif
#endif
