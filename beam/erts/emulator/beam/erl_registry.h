/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef ERL_REGISTRY_H__
#define ERL_REGISTRY_H__
#include "sys.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ErtsRegistry ErtsRegistry;
struct ErtsIsolateNamespaceState;
ErtsRegistry *erts_registry_create(struct ErtsIsolateNamespaceState *);
/* Exclusive unpublished owner, no entries. Borrowed namespace children must
 * not be independently destroyed. Bound execution registries are refused. */
int erts_registry_can_discard(ErtsRegistry *);
int erts_registry_discard(ErtsRegistry *);
Uint erts_registry_count(ErtsRegistry *);
#ifdef __cplusplus
}
#endif
#endif
