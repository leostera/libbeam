/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1996-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef LIBBEAM_CORE_BIG_H
#define LIBBEAM_CORE_BIG_H
#include "term.h"
/* Selected native flat-64 bignum primitives. Valid owned integer terms only.
 * These functions neither allocate nor collect. Callers own the output words;
 * no new BIF/instruction admission follows merely from linking these helpers. */
#define LB_BIG_ARITY_MAX ((1u<<16)-1)
/* Covers the bounded 64-bit MSB scan and sticky-bit loop in big_to_double. */
#define LB_BIG_TO_DOUBLE_FIXED_WORK 8u
static inline int lb_is_big(Eterm value)
{
    return is_boxed(value) && ((*boxed_val(value)&_TAG_HEADER_MASK)==POS_BIG_SUBTAG || (*boxed_val(value)&_TAG_HEADER_MASK)==NEG_BIG_SUBTAG);
}
static inline int lb_is_integer(Eterm value)
{ return is_small(value) || lb_is_big(value); }
static inline size_t lb_big_size(Eterm value) { return (size_t)(*boxed_val(value)>>_HEADER_ARITY_OFFS); }
/* Two output words. Also used for temporary conversion of small operands. */
Eterm lb_small_to_big(Sint value,Eterm *output);
/* Both operands are bignums (including temporary small_to_big values). Output
 * capacity >= max(operand arities)+2; separate from both input buffers.
 * Result may normalize to a small. NIL means native bignum size overflow. */
Eterm lb_big_plus(Eterm left,Eterm right,Eterm *output);
Eterm lb_big_minus(Eterm left,Eterm right,Eterm *output);
/* Independent output capacity >= sum(operand arities)+1 words. Same result
 * normalization and NIL size-overflow contract, including temporary smalls. */
Eterm lb_big_times(Eterm left,Eterm right,Eterm *output);
typedef struct { Eterm quotient,remainder; } LbBigDivision;
/* Integer magnitudes, including temporary small_to_big operands. Quotient
 * backing needs left arity+1 words; remainder needs right arity+1. Independent
 * scratch needs left arity+right arity+1. Zero divisor/insufficient scratch
 * returns 0 without publishing output. Remainder has the dividend's sign. */
int lb_big_div_rem(Eterm left,Eterm right,Eterm *quotient,Eterm *remainder,
                   Eterm *scratch,size_t scratch_words,LbBigDivision *output);
/* Native arithmetic shift of a bignum into caller-owned words. Returns NIL on
 * capacity/profile overflow; right shifts round negative values toward -inf. */
Eterm lb_big_shift(Eterm value,int left,size_t bits,Eterm *output,size_t output_words);
/* Exact two's-complement bitwise AND; scratch holds both normalized operands
 * and the returned result. */
Eterm lb_big_band(Eterm left,Eterm right,Eterm *scratch,size_t scratch_words);
Eterm lb_big_bitwise(Eterm left,Eterm right,unsigned operation,Eterm *scratch,size_t scratch_words);
Eterm lb_big_bnot(Eterm value,Eterm *scratch,size_t scratch_words);
/* Canonical non-small bignum; default nearest/ties-even IEEE environment
 * required. -1 for nonfinite
 * conversion, leaving *output untouched; native nearest/ties-even rounding. */
int lb_big_to_double(Eterm value,double *output);
#endif
