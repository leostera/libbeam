/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef ERL_EXPORT_NAMESPACE_H__
#define ERL_EXPORT_NAMESPACE_H__
#include "erl_export_literals.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ErtsExportNamespace ErtsExportNamespace;
struct export_;
int erts_export_namespace_default_limit(void);
/* Borrows the owning state's literal pool. ERTS allocation failure is fatal. */
ErtsExportNamespace *erts_export_namespace_create(ErtsExportLiterals *, int limit);
/* Atom indices are namespace-local. Caller validates membership in its atoms.
 * A returned export/lambda is borrowed, not an executable public handle.
 * Mutation and disposal require externally serialized namespace ownership. */
const struct export_ *erts_export_namespace_find(ErtsExportNamespace *, int module,
                                                int function, unsigned arity, unsigned slot);
struct export_ *erts_export_namespace_put(ErtsExportNamespace *, int module,
                                         int function, unsigned arity, unsigned slot);
int erts_export_namespace_count(ErtsExportNamespace *, unsigned slot);
/* Unique export blobs, not slot references or separately owned literal areas. */
size_t erts_export_namespace_entry_bytes(ErtsExportNamespace *);
/* Copies active dispatch/table entries into an inactive slot. Does NOT publish
 * code indices or implement whole-code-space commit/abort. Returns 1 unchanged
 * for invalid state, incompatible entries or insufficient destination capacity. */
int erts_export_namespace_start_staging(ErtsExportNamespace *, unsigned source, unsigned destination);
int erts_export_namespace_end_staging(ErtsExportNamespace *, unsigned destination);
/* Exclusive unpublished owner, no borrowed pointers/terms/readers. */
int erts_export_namespace_can_discard(ErtsExportNamespace *);
int erts_export_namespace_discard(ErtsExportNamespace *);
#ifdef __cplusplus
}
#endif
#endif
