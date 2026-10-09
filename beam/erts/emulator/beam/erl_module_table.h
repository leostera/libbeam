/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef ERL_MODULE_TABLE_H__
#define ERL_MODULE_TABLE_H__

/* Internal namespace component, NOT a public Isolate API. Requires initialized
 * ERTS allocation infrastructure. All access is externally serialized. Keys are
 * untagged atom indices supplied by the owning namespace, not globally resolved
 * names. This component does not intern atoms or load/publish executable code.
 * Allocation failure still follows ERTS fatal allocation semantics.
 */
#ifdef __cplusplus
extern "C" {
#endif

typedef struct ErtsModuleTable ErtsModuleTable;
struct erl_module;
ErtsModuleTable *erts_module_table_create(int limit);
struct erl_module *erts_module_table_find(ErtsModuleTable *, int atom_index);
/* NULL for an invalid index or a full table; existing entries remain retrievable. */
struct erl_module *erts_module_table_put(ErtsModuleTable *, int atom_index);
int erts_module_table_count(const ErtsModuleTable *);
int erts_module_table_capacity(const ErtsModuleTable *);
/* Caller must own an UNPUBLISHED table and have released all borrowed pointers.
 * Returns 1 without freeing anything if code/on_load/NIF roots remain. This is
 * not executable-code retirement, concurrent reclamation, or isolate shutdown. */
int erts_module_table_discard_unpublished(ErtsModuleTable *);

#ifdef __cplusplus
}
#endif
#endif
