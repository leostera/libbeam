/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1997-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * erl_bits.c erts_bs_start_match_3 and erl_sub_bits_init, owned heap boundary.
 */
#include "process_internal.h"
#include "binary_match.h"
LbSubBits *lb_bs_start_match(LbProcess *p,Eterm value)
{
    /* Whole native start-match dispatch reserves and reloads before entry.
     * Pinning is infallible: revoke a writable backing before borrowing base. */
    if(!lb_bitstring_pin(value)) abort();
    LbBitstringView view;
    if(!lb_bitstring_view(value,&view)) abort();
    Eterm orig=value;
    if(*boxed_val(value)==LB_HEADER_SUB_BITS) orig=((LbSubBits *)boxed_val(value))->orig;
    LbSubBits *sub=(LbSubBits *)p->htop; p->htop+=LB_SUB_BITS_WORDS;
    sub->thing_word=LB_HEADER_SUB_BITS;
    sub->base_flags=(Uint)view.data|LB_SUB_MUTABLE;
    sub->start=view.bit_offset; sub->end=view.bit_offset+view.bit_size;
    sub->orig=orig;
    return sub;
}
