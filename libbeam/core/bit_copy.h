/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1999-2025. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef LIBBEAM_CORE_BIT_COPY_H
#define LIBBEAM_CORE_BIT_COPY_H
#include <stddef.h>
/* Native forward bit copy, MSB-first within each byte. Callers supply valid
 * byte spans. Bounds and bit-range overlap are checked before any write;
 * invalid requests return 0 and leave the destination unchanged. Disjoint bit
 * intervals may share a boundary byte; source bits and all bits outside the
 * destination interval survive. Exact self-copy is an allocation-free no-op;
 * all other overlapping bit intervals are refused. Partial destination bytes
 * must be initialized.
 * Zero-length copies may use NULL empty spans. No allocation/GC/ownership
 * transfer. This is not validation of arbitrary pointer/address provenance. */
int lb_bits_copy(const unsigned char *source,size_t source_bytes,size_t source_bit,
                 unsigned char *destination,size_t destination_bytes,size_t destination_bit,
                 size_t bits);
#endif
