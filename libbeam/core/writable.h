/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef LIBBEAM_CORE_WRITABLE_H
#define LIBBEAM_CORE_WRITABLE_H
#include "process.h"
#include "bit_writer.h"
typedef enum {
    LB_WRITABLE_OK, LB_WRITABLE_INVALID, LB_WRITABLE_TYPE, LB_WRITABLE_UNIT,
    LB_WRITABLE_LIMIT, LB_WRITABLE_NO_MEMORY, LB_WRITABLE_NOT_UNIQUE
} LbWritableStatus;
/* Internal serialized construction boundaries, not guest opcode admission.
 * Sources belong to this process's heap or its retained literals; valid owned
 * representations are required, not arbitrary/foreign words. Output slots must
 * be non-overlapping and survive moving GC (not pointers into the process heap).
 * Fallible work precedes publication/revocation; failure leaves outputs and
 * source ownership unchanged. Successful collection can move all heap roots.
 * The returned term must be rooted before another allocation/collection.
 * Capacity is in bytes; init creates an empty writable bitstring. */
LbWritableStatus lb_writable_init(LbProcess *,size_t capacity,Eterm *out);
/* Fresh native heap/refcounted bitstring, not an append-writable term. Storage
 * (including heap-word slack) is initialized. The returned span is exclusively
 * borrowed for construction: finish exactly bits before guest publication or
 * sharing, and never write after that. It expires on collection/destruction.
 * Reload any heap operands after this call: successful reservation may collect.
 * Free heap covers max(pending heap credit, extra_words); credit is unchanged.
 * Failure preserves outputs, old heap/roots and offheap chains. The byte span
 * includes at most seven padding bits; caller must not write beyond bits.
 * No instruction reduction or virtual-binary-heap policy is implemented here. */
LbWritableStatus lb_binary_begin(LbProcess *,size_t bits,size_t extra_words,
                                 LbBitWriter *,Eterm *out);
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
/* A real VM root location, never a raw pointer that could move during GC. */
typedef enum { LB_BINARY_SOURCE_X, LB_BINARY_SOURCE_Y } LbBinarySourceKind;
typedef struct { LbBinarySourceKind kind; size_t index; } LbBinarySource;
/* Checked linear source boundary for native private append. The source must be
 * a nonliteral, zero-start SubBits stored only in this slot: other X/Y/exception
 * roots or stored heap terms cause NOT_UNIQUE before mutation. Dead allocated
 * terms count conservatively too; this is not a compiler uniqueness proof.
 * Success consumes the source slot (sets NIL) before assigning *out. The caller
 * must not retain unrooted C aliases. Output may be an X register or C temporary,
 * not a moving heap slot. Writer lifetime/end rules match normal append.
 * No guest private-append instruction is admitted by this interface. */
LbWritableStatus lb_writable_private_append(LbProcess *,LbBinarySource,size_t build_bits,
                                           LbBitWriter *,Eterm *out);
#endif
