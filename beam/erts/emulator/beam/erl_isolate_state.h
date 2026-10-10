/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef ERL_ISOLATE_STATE_H__
#define ERL_ISOLATE_STATE_H__
#include "erl_embed.h"
#include "erl_atom_namespace.h"
#include "erl_module_table.h"
#include "erl_export_literals.h"
#include "erl_export_namespace.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Internal unpublished namespace state, not a runnable/public Isolate. Owns
 * private atoms, modules, exports/literals, funs, records, catches, ranges and
 * coordinated code-index table transactions. Loader/process context propagation,
 * execution retirement, quotas and recoverable OOM remain pending. */
typedef struct ErtsIsolateNamespaceState ErtsIsolateNamespaceState;
typedef struct ErtsCodeTable ErtsCodeTable;
typedef struct ErtsCodeSpace ErtsCodeSpace;
ErtsCodeSpace *erts_isolate_namespace_code_space(ErtsIsolateNamespaceState *);
typedef struct ErtsCatchNamespace ErtsCatchNamespace;
typedef struct ErtsRangeNamespace ErtsRangeNamespace;
typedef struct ErtsModuleNamespace ErtsModuleNamespace;
ErtsModuleNamespace *erts_isolate_namespace_module_state(ErtsIsolateNamespaceState *);
ErtsRangeNamespace *erts_isolate_namespace_ranges(ErtsIsolateNamespaceState *);
ErtsCatchNamespace *erts_isolate_namespace_catches(ErtsIsolateNamespaceState *);
ErtsCodeTable *erts_isolate_namespace_funs(ErtsIsolateNamespaceState *);
ErtsCodeTable *erts_isolate_namespace_records(ErtsIsolateNamespaceState *);
ErtsIsolateNamespaceState *erts_isolate_namespace_create(ErtsEngine *, int atom_limit, int module_limit);
/* Diagnostic initializer: only inside native preparation AFTER allocator setup.
 * Uses the same state/storage implementation; permanently retained until a real
 * execution-reference retirement protocol exists. Not the public Engine path. */
ErtsIsolateNamespaceState *erts_isolate_namespace_create_diagnostic(ErtsEngine *, int, int, int);
/* Borrowed children: do not discard separately or retain beyond their parent. */
ErtsAtomNamespace *erts_isolate_namespace_atoms(ErtsIsolateNamespaceState *);
ErtsExportLiterals *erts_isolate_namespace_export_literals(ErtsIsolateNamespaceState *);
ErtsExportNamespace *erts_isolate_namespace_exports(ErtsIsolateNamespaceState *);
ErtsModuleTable *erts_isolate_namespace_modules(ErtsIsolateNamespaceState *); /* slot zero */
ErtsModuleTable *erts_isolate_namespace_module_at(ErtsIsolateNamespaceState *, unsigned);
/* Borrowed fixed slot array for diagnostic adapter initialization only. */
ErtsModuleTable **erts_isolate_namespace_module_slots(ErtsIsolateNamespaceState *);
/* Exclusive unpublished owner, no live terms, code or other borrowers.
 * Preflights all children; refuses without partially releasing the state. */
int erts_isolate_namespace_discard(ErtsIsolateNamespaceState *);
#ifdef __cplusplus
}
#endif
#endif
