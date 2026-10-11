/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1996-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef LIBBEAM_CORE_DECIMAL_H
#define LIBBEAM_CORE_DECIMAL_H
#include "alloc.h"
#include "term.h"
typedef struct LbDecimal LbDecimal;
typedef struct { const unsigned char *data; size_t size; } LbDecimalView;
#define LB_DECIMAL_SMALL_BYTES 21
/* Native Sint_to_buf base-10 path; caller owns the output storage. */
LbDecimalView lb_decimal_small(Sint,unsigned char output[LB_DECIMAL_SMALL_BYTES]);
/* Valid owned canonical bignum. Copies its digits; no term/code/heap pointers
 * survive construction. One allocation, NULL output on failure. */
LbAllocStatus lb_decimal_create(LbAllocDomain *,Eterm,LbDecimal **);
/* Native write_big_simple/D_div, yielding only between complete division
 * passes. A positive budget may overrun by at most one native limb vector.
 * No allocation or GC. Returns true when complete; reports actual limb work. */
int lb_decimal_step(LbDecimal *,size_t word_budget,size_t *work);
/* Borrowed bytes only when complete, otherwise {NULL,0}. */
LbDecimalView lb_decimal_view(const LbDecimal *);
void lb_decimal_destroy(LbDecimal *);
#endif
