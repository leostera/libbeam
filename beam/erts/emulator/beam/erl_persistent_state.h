/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef ERL_PERSISTENT_STATE_H__
#define ERL_PERSISTENT_STATE_H__
#include "sys.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ErtsIsolateNamespaceState ErtsIsolateNamespaceState;
typedef struct ErtsPersistentTermState ErtsPersistentTermState;
ErtsPersistentTermState *erts_persistent_state_create(ErtsIsolateNamespaceState *);
/* Exclusive unpublished disposal only; empty table, no queued work or users. */
int erts_persistent_state_can_discard(ErtsPersistentTermState *);
int erts_persistent_state_discard(ErtsPersistentTermState *);
#ifdef __cplusplus
}
#endif
#endif
