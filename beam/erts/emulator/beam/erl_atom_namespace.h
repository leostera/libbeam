/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef ERL_ATOM_NAMESPACE_H__
#define ERL_ATOM_NAMESPACE_H__
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ErtsAtomNamespace ErtsAtomNamespace;
/* Internal, unpublished namespace storage. Requires initialized ERTS allocators.
 * Seeds the immutable predefined vocabulary at its required indices. OOM still
 * follows fatal ERTS allocation semantics. Not a public Isolate constructor. */
ErtsAtomNamespace *erts_atom_namespace_create(int limit);
/* UTF-8 only, no truncation: -1 encoding, -2 length, -3 capacity errors. */
int erts_atom_namespace_put(ErtsAtomNamespace *, const unsigned char *, size_t);
/* Copies the name into caller storage. Returns byte length, or -1 on invalid
 * index/insufficient capacity. No borrowed name pointers escape this API. */
int erts_atom_namespace_name(ErtsAtomNamespace *, int, unsigned char *, size_t);
int erts_atom_namespace_count(ErtsAtomNamespace *);
size_t erts_atom_namespace_text_bytes(ErtsAtomNamespace *);
/* Exclusive unpublished owner only; no concurrent readers or live terms/code. */
void erts_atom_namespace_discard_unpublished(ErtsAtomNamespace *);
#ifdef __cplusplus
}
#endif
#endif
