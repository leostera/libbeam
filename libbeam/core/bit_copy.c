/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1999-2025. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *     http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Native erts_copy_bits_fwd from erl_bits.c; see otp/bit-copy-sources.json.
 * The algorithm is unchanged. A checked span/overlap boundary precedes it.
 */
#include "bit_copy.h"
#include <limits.h>
#include <stdint.h>
#include <string.h>

_Static_assert(CHAR_BIT==8,"Native bit copying requires eight-bit bytes");
#define MAKE_MASK(n) (((size_t)1<<(n))-1)
#define MASK_BITS(src,dst,mask) (((src)&(mask))|((dst)&~(mask)))

static void copy_forward(const unsigned char *src,size_t soffs,
                         unsigned char *dst,size_t doffs,size_t n)
{
    size_t lmask,rmask,count,deoffs;
    if(n==0) return;
    src+=soffs/8; dst+=doffs/8;
    soffs&=7; doffs&=7; deoffs=(doffs+n)&7;
    lmask=doffs?MAKE_MASK(8-doffs):0;
    rmask=deoffs?(MAKE_MASK(deoffs)<<(8-deoffs)):0;

    /* All destination bits fit in one byte. */
    if(doffs+n<8) {
        lmask=(lmask&rmask)?(lmask&rmask):(lmask|rmask);
        if(soffs==doffs) {
            *dst=(unsigned char)MASK_BITS(*src,*dst,lmask);
        } else if(soffs>doffs) {
            size_t bits=(size_t)*src<<(soffs-doffs);
            if(soffs+n>8) { ++src; bits|=*src>>(8-(soffs-doffs)); }
            *dst=(unsigned char)MASK_BITS(bits,*dst,lmask);
        } else {
            *dst=(unsigned char)MASK_BITS(*src>>(doffs-soffs),*dst,lmask);
        }
        return;
    }

    count=(lmask?(n-(8-doffs)):n)>>3;
    if(soffs==doffs) {
        /* Same alignment: only the first and last bytes need masking. */
        if(lmask) {
            *dst=(unsigned char)MASK_BITS(*src,*dst,lmask);
            ++dst; ++src;
        }
        memcpy(dst,src,count);
        if(rmask) {
            dst+=count; src+=count;
            *dst=(unsigned char)MASK_BITS(*src,*dst,rmask);
        }
    } else {
        size_t bits,bits1,rshift,lshift;
        if(soffs>doffs) {
            lshift=soffs-doffs; rshift=8-lshift;
            bits=*src;
            if(soffs+n>8) ++src;
        } else {
            rshift=doffs-soffs; lshift=8-rshift; bits=0;
        }
        if(lmask) {
            bits1=bits<<lshift; bits=*src++;
            bits1|=bits>>rshift;
            *dst=(unsigned char)MASK_BITS(bits1,*dst,lmask);
            ++dst;
        }
        while(count--) {
            bits1=bits<<lshift; bits=*src++;
            *dst++=(unsigned char)(bits1|(bits>>rshift));
        }
        if(rmask) {
            bits1=bits<<lshift;
            if((rmask<<rshift)&0xff) { bits=*src; bits1|=bits>>rshift; }
            *dst=(unsigned char)MASK_BITS(bits1,*dst,rmask);
        }
    }
}

static int contains_bits(size_t bytes,size_t offset,size_t bits)
{
    return bytes<=SIZE_MAX/8 && offset<=bytes*8 && bits<=bytes*8-offset;
}

/* Compare normalized bit positions without multiplying a pointer by eight. */
static int at_or_before(uintptr_t left,size_t left_bit,uintptr_t right,size_t right_bit)
{
    return left<right || (left==right && left_bit<=right_bit);
}

int lb_bits_copy(const unsigned char *source,size_t source_bytes,size_t source_bit,
                 unsigned char *destination,size_t destination_bytes,size_t destination_bit,
                 size_t bits)
{
    uintptr_t src,dst; size_t source_count,destination_count,source_end,destination_end;
    if((!source && source_bytes) || (!destination && destination_bytes) ||
       !contains_bits(source_bytes,source_bit,bits) ||
       !contains_bits(destination_bytes,destination_bit,bits)) return 0;
    if(!bits) return 1;
    /* Bounds above prove these sums fit. Check address arithmetic before
     * comparing normalized bit intervals, including their partial edge bytes. */
    src=(uintptr_t)(source+source_bit/8);
    dst=(uintptr_t)(destination+destination_bit/8);
    source_count=((source_bit&7)+bits+7)/8;
    destination_count=((destination_bit&7)+bits+7)/8;
    if(src>UINTPTR_MAX-source_count || dst>UINTPTR_MAX-destination_count) return 0;
    if(src==dst && (source_bit&7)==(destination_bit&7)) return 1;
    source_end=(source_bit&7)+bits; destination_end=(destination_bit&7)+bits;
    if(!at_or_before(src+source_end/8,source_end&7,dst,destination_bit&7) &&
       !at_or_before(dst+destination_end/8,destination_end&7,src,source_bit&7)) return 0;
    /* Disjoint bit intervals can share one boundary byte. Native edge masks
     * preserve the source bits there; full-byte memcpy interiors never overlap.
     * Exact identity returned above, avoiding C's overlapping-memcpy UB. */
    copy_forward(source,source_bit,destination,destination_bit,bits);
    return 1;
}
