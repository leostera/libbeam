/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1997-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * erl_bits.c erts_bs_start_match_3 and erl_sub_bits_init, owned heap boundary.
 */
#include "process_internal.h"
#include "binary_match.h"
#include <string.h>
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

int lb_bs_match_string(LbSubBits *sub,const unsigned char *literal,size_t bits)
{
    if(!sub || !lb_is_match_context(sub) || sub->start>sub->end ||
       (bits && !literal)) return -1;
    if(bits>sub->end-sub->start) return 0;
    const unsigned char *base=(const unsigned char *)(sub->base_flags&~(Uint)LB_SUB_WRITABLE);
    size_t offset=sub->start,whole=bits/8,tail=bits&7;
    if(!base) return -1;
    if(!(offset&7)) {
        const unsigned char *actual=base+offset/8;
        if(whole && memcmp(actual,literal,whole)) return 0;
        if(tail && ((actual[whole]^literal[whole])&(0xffu<<(8-tail)))) return 0;
    } else {
        size_t byte=offset/8,shift=offset&7;
        for(size_t i=0;i<whole;++i) {
            unsigned actual=(unsigned)base[byte+i]<<shift | base[byte+i+1]>>(8-shift);
            if((unsigned char)actual!=literal[i]) return 0;
        }
        if(tail) {
            unsigned actual=(unsigned)base[byte+whole]<<shift;
            if(shift+tail>8) actual|=base[byte+whole+1]>>(8-shift);
            if(((actual^literal[whole])&(0xffu<<(8-tail)))) return 0;
        }
    }
    sub->start+=bits;
    return 1;
}
