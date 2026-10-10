/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef ERL_EXPORT_LITERALS_H__
#define ERL_EXPORT_LITERALS_H__
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Namespace-owned external-fun literal areas. Not an executable export table.
 * Requires initialized ERTS allocators; allocation failure remains fatal. */
typedef struct ErtsExportLiterals ErtsExportLiterals;
ErtsExportLiterals *erts_export_literals_create(void);
size_t erts_export_literals_count(ErtsExportLiterals *);
/* Once published to execution, this slice retains ownership permanently. */
void erts_export_literals_bind(ErtsExportLiterals *);
int erts_export_literals_can_discard(ErtsExportLiterals *);
/* Exclusive unpublished owner only; no live terms or borrowed pointers. */
int erts_export_literals_discard(ErtsExportLiterals *);
#ifdef __cplusplus
}
#endif
#endif
