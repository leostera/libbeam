/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 2002-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * erl_gc.c do_next_vheap_size/next_vheap_size, with explicit owned policy and
 * deterministic binary64 arithmetic instead of ambient floating-point state.
 */
#include "binary_vheap.h"
#if !defined(__SIZEOF_INT128__) || __SIZEOF_INT128__ != 16
#error "Binary virtual-heap arithmetic requires unsigned compiler 128-bit integers"
#endif
typedef unsigned __int128 Wide;
static Wide round_binary64(Wide value)
{
    unsigned bits=0,shift; Wide high,rest,half;
    for(Wide scan=value;scan;scan>>=1) ++bits;
    if(bits<=53) return value;
    shift=bits-53; high=value>>shift;
    rest=value-(high<<shift); half=(Wide)1<<(shift-1);
    if(rest>half || (rest==half && (high&1))) ++high;
    return high<<shift;
}
static uint64_t grow(uint64_t current)
{
    /* 1.618 is exactly 0x1.9e353f7ced917p+0 in binary64. Round the
     * Uint64->double conversion AND the multiplication before truncation.
     * Products occupy at most 118 bits, including rounding carries. */
    Wide product=round_binary64(round_binary64(current)*UINT64_C(0x19e353f7ced917));
    Wide next=product>>52;
    /* Native detects a wrapped conversion after growth. Avoid C's undefined
     * out-of-range floating conversion while preserving its stop-growing
     * intent. Keep the previous threshold; never wrap to a tiny value. */
    return next>UINT64_MAX?current:(uint64_t)next;
}
uint64_t lb_binary_vheap_next(uint64_t used,uint64_t current)
{
    if(current<LB_BINARY_VHEAP_MIN_WORDS) current=LB_BINARY_VHEAP_MIN_WORDS;
    if(used/3>current/4) {
        while(used/3>current/4) {
            uint64_t next=grow(current);
            if(next<=current) return current;
            current=next;
        }
        return current;
    }
    if(used<current/4) current>>=1;
    return current<LB_BINARY_VHEAP_MIN_WORDS?LB_BINARY_VHEAP_MIN_WORDS:current;
}
int lb_binary_vheap_pressure(uint64_t used,uint64_t incoming,uint64_t threshold)
{
    return used>=threshold || incoming>=threshold-used;
}
