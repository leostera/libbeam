/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef LIBBEAM_CORE_BINARY_SIZE_H
#define LIBBEAM_CORE_BINARY_SIZE_H
#include "binary_error.h"
/* Borrowed, initialized register spans. Y includes the continuation slot at
 * index zero; descriptors cannot read that slot. Bounds do not prove active
 * frame shape, must-definition, private-source consumption or uniqueness. */
typedef struct {
    const Eterm *x; size_t x_count;
    const Eterm *y; size_t y_count;
} LbBinaryRegisters;
typedef struct {
    LbBinarySegmentKind kind;
    Uint segment;
    Eterm reason,info,value;
} LbBinaryFailure;
typedef enum {
    LB_BINARY_SIZE_OK,
    LB_BINARY_SIZE_INVALID,
    LB_BINARY_SIZE_GUEST_ERROR,
    LB_BINARY_SIZE_LIMIT
} LbBinarySizeStatus;
typedef struct {
    LbBinarySizeStatus status;
    union { size_t bits; LbBinaryFailure failure; } as;
} LbBinarySizeResult;
/* Native flat64 sizing pass over nonempty, five-word emitted descriptors.
 * Operands must be valid owned terms in one namespace. Code and register spans
 * must remain alive, stable and serialized throughout this call. This is not
 * validation of arbitrary words/pointer provenance or execution admission.
 * No allocation, collection, mutation, retention or writer work occurs here.
 * The returned failure VALUE is borrowed: root it before collection (the
 * binary-error payload factory does that for its explicit value argument).
 * Append prefixes are excluded, as in native sizing. UTF8/16 estimates defer
 * scalar validation to construction; UTF32 is checked now. A caller ceiling
 * is infrastructure policy, not guest system_limit. LIMIT is reported only
 * after all native sizing checks, so it cannot mask a later guest error.
 * Read the union only for OK or GUEST_ERROR respectively. */
LbBinarySizeResult lb_binary_size(LbBinaryRegisters,const Uint *descriptors,
                                  size_t word_count,size_t maximum_bits);
#endif
