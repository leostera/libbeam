/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef LIBBEAM_CORE_ENGINE_H
#define LIBBEAM_CORE_ENGINE_H
#include "alloc.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct LbEngine LbEngine;
typedef enum {
    LB_ENGINE_OK, LB_ENGINE_INVALID, LB_ENGINE_NO_MEMORY,
    LB_ENGINE_BUSY, LB_ENGINE_CLOSED
} LbEngineStatus;

/* Serialized C ownership root for the actual execution catalog and allocation
 * substrate. Creates no world, worker, OTP service or process-wide claim.
 * Allocator callbacks/context outlive all retained children and this handle.
 * Internal allocator hooks may run on the worker under Engine serialization;
 * they must not reenter the Engine/domain. Public C++ uses the default allocator. */
LbEngineStatus lb_engine_create(const LbSystemAllocator *,LbEngine **);
/* Busy refusal is nonmutating. Success releases execution resources; a finite
 * closed control handle remains until release. Any lazily admitted executor
 * worker is stopped and joined before its synchronization storage is destroyed. */
LbEngineStatus lb_engine_shutdown(LbEngine *);
int lb_engine_is_open(const LbEngine *);
/* Consume the single host-owner reference. Existing children keep the substrate
 * alive; the last physical child release finishes cleanup if the host dropped it.
 * No owner resurrection, forced child free, retained singleton or host exit. */
void lb_engine_release(LbEngine *);
#ifdef __cplusplus
}
#endif
#endif
