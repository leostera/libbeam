/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef ERL_ISOLATE_STATE_H__
#define ERL_ISOLATE_STATE_H__
#include "erl_embed.h"
#include "erl_atom_namespace.h"
#include "erl_module_table.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Internal unpublished namespace state, not a runnable/public Isolate. Owns
 * private atom storage and one module metadata table. Exports, code indices,
 * execution, quotas and recoverable allocator failure are not implemented. */
typedef struct ErtsIsolateNamespaceState ErtsIsolateNamespaceState;
ErtsIsolateNamespaceState *erts_isolate_namespace_create(ErtsEngine *, int atom_limit, int module_limit);
/* Borrowed children: do not discard separately or retain beyond their parent. */
ErtsAtomNamespace *erts_isolate_namespace_atoms(ErtsIsolateNamespaceState *);
ErtsModuleTable *erts_isolate_namespace_modules(ErtsIsolateNamespaceState *);
/* Exclusive unpublished owner, no live terms, code or other borrowers.
 * Refuses without releasing atoms/state if the module table cannot be discarded. */
int erts_isolate_namespace_discard(ErtsIsolateNamespaceState *);
#ifdef __cplusplus
}
#endif
#endif
