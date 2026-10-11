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
typedef enum { LB_BITS_OK, LB_BITS_BADARG, LB_BITS_NO_SPACE, LB_BITS_INVALID } LbBitWriteStatus;
/* Pure native segment writers. Valid owned input terms; the output span must
 * not overlap their storage during nonempty writes. No allocation, collection
 * or input-term retention; the caller's writer borrows the output span. No
 * instruction/BIF admission. Every refusal preserves the cursor and bytes;
 * successful writes preserve bits outside the segment, including its tail.
 * The caller owns/protects both terms and initialized output throughout. */
LbBitWriteStatus lb_bit_writer_init(LbBitWriter *,unsigned char *,size_t);
LbBitWriteStatus lb_bits_put_integer(LbBitWriter *,Eterm,size_t,LbBitEndian);
LbBitWriteStatus lb_bits_put_utf8(LbBitWriter *,Eterm);
LbBitWriteStatus lb_bits_put_utf16(LbBitWriter *,Eterm,LbBitEndian);
LbBitWriteStatus lb_bits_put_utf32(LbBitWriter *,Eterm,LbBitEndian);
#endif
