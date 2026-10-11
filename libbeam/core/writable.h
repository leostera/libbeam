/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef LIBBEAM_CORE_WRITABLE_H
#define LIBBEAM_CORE_WRITABLE_H
#include "process.h"
#include "bit_writer.h"
typedef enum {
    LB_WRITABLE_OK, LB_WRITABLE_INVALID, LB_WRITABLE_TYPE, LB_WRITABLE_UNIT,
    LB_WRITABLE_LIMIT, LB_WRITABLE_NO_MEMORY
} LbWritableStatus;
/* Internal serialized construction boundaries, not guest opcode admission.
 * Sources belong to this process's heap or its retained literals; valid owned
 * representations are required, not arbitrary/foreign words. Output slots must
 * survive moving GC (not pointers into the process heap).
 * Fallible work precedes publication/revocation; failure leaves outputs and
 * source ownership unchanged. Successful collection can move all heap roots.
 * The returned term must be rooted before another allocation/collection.
 * Capacity is in bytes; init creates an empty writable bitstring. */
LbWritableStatus lb_writable_init(LbProcess *,size_t capacity,Eterm *out);
/* Native non-private append, including immutable-source copying. No uniqueness
 * assumption about the source header. After construction, free space covers
 * max(pending heap credit, extra_words); the credit counter itself is unchanged.
 * Zero build returns the source and an empty writer.
 * Writer borrows initialized bytes until collection/growth/destruction; its
 * byte capacity includes at most seven padding bits beyond the term's end.
 * Caller must finish exactly build_bits before publishing to guest code.
 * No reduction charging here: future dispatch owns work accounting. */
LbWritableStatus lb_writable_append(LbProcess *,Eterm source,size_t build_bits,
                                   size_t extra_words,unsigned unit,
                                   LbBitWriter *,Eterm *out);
#endif
