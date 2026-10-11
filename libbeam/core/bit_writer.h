/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1999-2025. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef LIBBEAM_CORE_BIT_WRITER_H
#define LIBBEAM_CORE_BIT_WRITER_H
#include "term.h"
typedef struct {
    unsigned char *data;
    size_t capacity; /* Bytes in the caller-owned initialized writable span. */
    size_t offset;   /* Next bit, MSB-first within each byte. */
} LbBitWriter;
typedef enum { LB_BITS_BIG_ENDIAN, LB_BITS_LITTLE_ENDIAN } LbBitEndian;
typedef enum { LB_BITS_OK, LB_BITS_BADARG, LB_BITS_NO_SPACE, LB_BITS_INVALID, LB_BITS_ENVIRONMENT } LbBitWriteStatus;
typedef enum { LB_FLOAT_NONE, LB_FLOAT_TYPE, LB_FLOAT_NO_FLOAT, LB_FLOAT_SIZE } LbBitFloatError;
typedef struct { LbBitWriteStatus status; LbBitFloatError error; } LbBitFloatResult;
/* Native segment writers. Valid owned input terms; output must not overwrite
 * input-term metadata or numeric storage. Binary/string data windows follow
 * the bit-range sharing rules below. No allocation, collection
 * or input-term retention; the caller's writer borrows the output span. No
 * instruction/BIF admission. Every refusal preserves the cursor and bytes;
 * successful writes preserve bits outside the segment, including its tail.
 * The caller owns/protects both terms and initialized output throughout. */
LbBitWriteStatus lb_bit_writer_init(LbBitWriter *,unsigned char *,size_t);
LbBitWriteStatus lb_bits_put_integer(LbBitWriter *,Eterm,size_t,LbBitEndian);
LbBitWriteStatus lb_bits_put_utf8(LbBitWriter *,Eterm);
LbBitWriteStatus lb_bits_put_utf16(LbBitWriter *,Eterm,LbBitEndian);
LbBitWriteStatus lb_bits_put_utf32(LbBitWriter *,Eterm,LbBitEndian);
/* Byte strings and binary windows may share a boundary byte with disjoint
 * destination bits, or be the identical bit range (no-op plus cursor advance).
 * All other overlapping bit ranges are refused before mutation.
 * binary_all accepts native language units 1..256, requiring exact divisibility.
 * No process reductions are charged here: future dispatch owns that accounting. */
LbBitWriteStatus lb_bits_put_string(LbBitWriter *,const unsigned char *,size_t);
LbBitWriteStatus lb_bits_put_binary(LbBitWriter *,Eterm,size_t);
LbBitWriteStatus lb_bits_put_binary_all(LbBitWriter *,Eterm,unsigned);
/* Native 16/32/64-bit float fields. Narrowing overflow encodes infinity (it is
 * not an Erlang float result). Input floats must be finite. Scoped default IEEE
 * environment is restored before publication; environment failure never writes
 * and is distinct from badarg. Failed restoration cannot promise restored fenv. */
LbBitFloatResult lb_bits_put_float(LbBitWriter *,Eterm,size_t,LbBitEndian);
#endif
