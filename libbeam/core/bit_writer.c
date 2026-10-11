/* SPDX-License-Identifier: Apache-2.0
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
 * Native integer/Unicode segment writers from erl_bits.c.
 * See otp/bit-writer-sources.json for bounds and C-language adaptations.
 */
#include "bit_writer.h"
#include "bit_copy.h"
#include "big.h"
#include <assert.h>
#include <stdlib.h>
#define ASSERT(x) assert(x)
#define ERTS_UNLIKELY(x) (x)
#define BYTE_OFFSET(x) ((x)/8)
#define BIT_OFFSET(x) ((x)&7)
#define NBYTES(x) (((x)+7)/8)
#define MAKE_MASK(n) ((((Uint)1)<<(n))-1)
#define MASK_BITS(src,dst,mask) (((src)&(mask))|((dst)&~(mask)))
#define MIN(a,b) ((a)<(b)?(a):(b))
#define is_big lb_is_big
#define is_not_small(value) (!is_small(value))
#define unsigned_val(value) ((Uint)(value)>>_TAG_IMMED1_SIZE)
#define big_size lb_big_size
#define big_v(value) (boxed_val(value)+1)
#define big_sign(value) ((*boxed_val(value)&_TAG_HEADER_MASK)==NEG_BIG_SUBTAG)
#define BSF_LITTLE 2
#ifndef __has_builtin
#define __has_builtin(x) 0
#endif
typedef unsigned char byte;
typedef Uint ErtsDigit;
typedef uint16_t Uint16;
_Static_assert(((Sint)-1 >> 1)==-1,"Native small formatting requires arithmetic signed right shift");

static void
fmt_small_be(byte *buf, Eterm arg, Uint num_bits)
{
    Uint bit_offset;
    Sint val;
    Uint num_bytes;

    ASSERT(is_small(arg));
    ASSERT(num_bits != 0);      /* Tested by caller */

    num_bytes = NBYTES(num_bits);
    bit_offset = BIT_OFFSET(num_bits);
    val = signed_val(arg);

    size_t index = num_bytes;
    if (bit_offset) {
        buf[--index] = (Uint)val << (8-bit_offset);
        num_bytes--;
        val >>= bit_offset;
    }
    while (num_bytes--) { buf[--index] = val; val >>= 8; }
}

static void
fmt_small_le(byte *buf, Eterm arg, Uint num_bits)
{
    Uint bit_offset;
    Sint val;
    Uint num_bytes;

    ASSERT(is_small(arg));
    ASSERT(num_bits != 0);      /* Tested by caller */

    num_bytes = NBYTES(num_bits);
    bit_offset = BIT_OFFSET(num_bits);
    val = signed_val(arg);
    num_bytes--;
    while (num_bytes--) { *buf++ = val; val >>= 8; }
    *buf = bit_offset ? ((Uint)val << (8-bit_offset)) : val;
}


/*
 * Calculate a - c (carry), storing the result in b; set c
 * to the new carry.
 */
#if __has_builtin(__builtin_subc) && !defined(DEBUG)
#define SUBc(a, c, b)                           \
   do {                                         \
       *(b) = __builtin_subc(0, (a), c, &c);    \
   } while(0)
#else
#define SUBc(a, c, b)                           \
   do {                                         \
       byte __x = (a);                          \
       byte __y = (__x - !(c));                 \
       c = !(__y > __x);                        \
       *(b) = ~__y;                             \
   } while(0)
#endif

static void
fmt_big_be(byte *buf, Eterm val, Uint num_bits)
{
    unsigned long offs;
    int sign;
    Uint ds;
    ErtsDigit* dp;
    Uint n;
    ErtsDigit acc = 0;
    ErtsDigit d;
    Uint num_bytes;

    ASSERT(is_big(val));
    ASSERT(num_bits != 0);

    num_bytes = NBYTES(num_bits);
    sign = big_sign(val);
    ds = big_size(val)*sizeof(ErtsDigit); /* number of digits bytes */
    dp = big_v(val);
    n = MIN(num_bytes, ds);

    offs = BIT_OFFSET(num_bits);

    size_t index = num_bytes;       /* end of buffer */
    num_bytes -= n;             /* pad with this amount */
    offs = offs ? (8-offs) : 0; /* shift offset */

    if (sign) {             /* negative bignum */
        unsigned int c = 0;

        while (n >= sizeof(ErtsDigit)) {
            Uint i;

            d = *dp++;
            acc |= d << offs;
            SUBc(acc & 0xff, c, &buf[--index]);
            acc = d >> (8-offs);
            for (i = 0; i < sizeof(ErtsDigit)-1; i++) {
                SUBc(acc & 0xff, c, &buf[--index]);
                acc >>= 8;
            }
            n -= sizeof(ErtsDigit);
        }
        if (n) {
            acc |= ((ErtsDigit)*dp << offs);
            do {
                SUBc(acc & 0xff, c, &buf[--index]);
                acc >>= 8;
            } while (--n > 0);
        }
        /* pad */
        while (num_bytes--) {
            SUBc(acc & 0xff, c, &buf[--index]);
            acc >>= 8;
        }
    } else {                /* positive bignum */
        while (n >= sizeof(ErtsDigit)) {
            Uint i;

            d = *dp++;
            acc |= d << offs;
            buf[--index] = acc;
            acc = d >> (8-offs);
            for (i = 0; i < sizeof(ErtsDigit)-1; i++) {
                buf[--index] = acc;
                acc >>= 8;
            }
            n -= sizeof(ErtsDigit);
        }
        if (n) {
            acc |= (*dp << offs);
            do {
                buf[--index] = acc;
                acc >>= 8;
            } while (--n > 0);
        }
        while (num_bytes--) {
            buf[--index] = acc;
            acc >>= 8;
        }
    }
}

static void
fmt_big_le(byte *buf, Eterm val, Uint num_bits)
{
    unsigned long offs;
    int sign;
    Uint ds;
    ErtsDigit* dp;
    Uint n;
    Uint num_bytes;

    ASSERT(is_big(val));
    ASSERT(num_bits != 0);

    num_bytes = NBYTES(num_bits);
    sign = big_sign(val);
    ds = big_size(val)*sizeof(ErtsDigit); /* number of digits bytes */
    dp = big_v(val);
    n = MIN(num_bytes, ds);

    offs = BIT_OFFSET(num_bits);
    num_bytes -= n;         /* pad with this amount */
    if (sign) {             /* negative */
        unsigned int c = 0;
        while (n >= sizeof(ErtsDigit)) {
            ErtsDigit d = *dp++;
            Uint i;
            for (i = 0; i < sizeof(ErtsDigit); i++) {
                SUBc(d & 0xff, c, buf);
                buf++;
                d >>= 8;
            }
            n -= sizeof(ErtsDigit);
        }
        if (n) {
            ErtsDigit d = *dp;
            do {
                SUBc(d & 0xff, c, buf);
                buf++;
                d >>= 8;
            } while (--n > 0);
        }
        /* pad */
        while (num_bytes--) {
            SUBc(0, c, buf);
            buf++;
        }
    } else {                /* positive */
        while (n >= sizeof(ErtsDigit)) {
            ErtsDigit d = *dp++;
            Uint i;
            for(i = 0; i < sizeof(ErtsDigit); i++) {
                *buf++ = d;
                d >>= 8;
            }
            n -= sizeof(ErtsDigit);
        }
        if (n) {
            ErtsDigit d = *dp;
            do {
                *buf++ = d;
                d >>= 8;
            } while (--n > 0);
        }
        /* pad */
        while (num_bytes) {
            *buf++ = 0;
            num_bytes--;
        }
    }

    /* adjust MSB */
    if (offs) {
        buf--;
        *buf <<= (8 - offs);
    }
}

#undef SUBc

static void
restore_and_shift(byte *buf, Uint orig_byte, Uint bit_offset, Uint num_bits)
{
    Uint rshift = bit_offset;
    Uint lshift = 8 - bit_offset;
    Uint deoffs = BIT_OFFSET(bit_offset + num_bits);
    Uint lmask = MAKE_MASK(8 - bit_offset);
    Uint count = (num_bits - lshift) / 8;
    Uint bits, bits1;

    ASSERT(num_bits >= lshift);

    bits = *buf;
    bits1 = bits >> rshift;
    *buf = MASK_BITS(bits1, orig_byte, lmask);
    buf++;

    while (count--) {
        bits1 = bits << lshift;
        bits = *buf;
        *buf++ = bits1 | (bits >> rshift);
    }

    if (deoffs) {
        Uint rmask = MAKE_MASK(deoffs) << (8 - deoffs);

        bits1 = bits << lshift;
        if ((rmask << rshift) & 0xff) {
            bits = *buf;
            bits1 |= (bits >> rshift);
        }
        *buf = MASK_BITS(bits1, *buf, rmask);
    }
}

static int
native_put_integer_be(LbBitWriter *EBS, Eterm arg, Uint num_bits)
{
    byte* dst_bin = EBS->data;
    Uint bin_offset = EBS->offset;
    Uint bit_offset;
    byte b;
    byte *iptr;

    if (ERTS_UNLIKELY(num_bits == 0)) {
        return is_small(arg) || is_big(arg);
    }

    iptr = dst_bin + BYTE_OFFSET(bin_offset);
    bit_offset = BIT_OFFSET(bin_offset);
    if (is_small(arg)) {
        Uint rbits = 8 - bit_offset;

        if (bit_offset == 0) {
            /* Aligned on a byte boundary. */
            if (num_bits <= 8) {
                /* All bits are in the same byte. */
                b = (signed_val(arg) & MAKE_MASK(num_bits)) << (rbits-num_bits);
                *iptr = b;
            } else {
                /* More than one byte. */
                fmt_small_be(iptr, arg, num_bits);
            }
        } else if (bit_offset + num_bits <= 8) {
            /*
             * All bits are in the same byte.
             */
            b = *iptr & (0xff << rbits);
            b |= (signed_val(arg) & MAKE_MASK(num_bits)) << (rbits-num_bits);
            *iptr = b;
        } else {		/* Big endian */
            /*
             * Big-endian, more than one byte, but not aligned on a byte boundary.
             * Handle the bits up to the next byte boundary specially,
             * then let fmt_small_be() handle the rest.
             */
            Uint shift_count = num_bits - rbits;
            Sint val = signed_val(arg);

            ASSERT(num_bits > rbits);
            b = *iptr & (0xff << rbits);

            /*
             * Shifting with a shift count greater than or equal to the word
             * size may be a no-op (instead of 0 the result may be the unshifted
             * value). Therefore, only do the shift and the OR if the shift count
             * is less than the word size if the number is positive; if negative,
             * we must simulate the sign extension.
             */
            if (shift_count < sizeof(Uint)*8) {
                b |= (val >> shift_count) & MAKE_MASK(rbits);
            } else if (val < 0) {
                /* Simulate sign extension. */
                b |= (-1) & MAKE_MASK(rbits);
            }
            *iptr++ = b;

            fmt_small_be(iptr, arg, shift_count);
        }
    } else if (is_big(arg) && bit_offset == 0) {
        /*
         * Big number, aligned on a byte boundary. We can format the
         * integer directly into the binary.
         */
        fmt_big_be(iptr, arg, num_bits);
    } else if (is_big(arg) && bit_offset + num_bits <= 8) {
        /*
         * All bits are in the same byte.
         */
        Uint rbits = 8 - bit_offset;
        Sint sign = big_sign(arg);
        ErtsDigit* dp = big_v(arg);
        Uint val = sign ? -*dp : *dp;

        b = *iptr & (0xff << rbits);
        b |= (val & MAKE_MASK(num_bits)) << (rbits-num_bits);
        *iptr = b;
    } else if (is_big(arg)) {
        /*
         * Big number, not aligned on a byte boundary.
         *
         * Format the integer byte-aligned using the binary itself as
         * a temporary buffer.
         */
        b = *iptr;
        fmt_big_be(iptr, arg, num_bits);

        /*
         * Now restore the overwritten bits of the first byte and
         * shift everything to the right.
         */
        restore_and_shift(iptr, b, bit_offset, num_bits);
    } else {
        /* Not an integer. */
        return 0;
    }
    EBS->offset = bin_offset + num_bits;
    return 1;
}

static int
native_put_integer_le(LbBitWriter *EBS, Eterm arg, Uint num_bits)
{
    byte* dst_bin = EBS->data;
    Uint bin_offset = EBS->offset;
    Uint bit_offset;
    byte b;
    byte *iptr;

    if (ERTS_UNLIKELY(num_bits == 0)) {
        return is_small(arg) || is_big(arg);
    }

    iptr = dst_bin + BYTE_OFFSET(bin_offset);
    bit_offset = BIT_OFFSET(bin_offset);
    if (is_small(arg)) {
        Uint rbits = 8 - bit_offset;

        if (bit_offset == 0) {
            /* Aligned on a byte boundary. */
            if (num_bits <= 8) {
                /* All bits are in the same byte. */
                b = (signed_val(arg) & MAKE_MASK(num_bits)) << (rbits-num_bits);
                *iptr = b;
            } else {
                /* More than one byte. */
                fmt_small_le(iptr, arg, num_bits);
            }
        } else if (bit_offset + num_bits <= 8) {
            /*
             * All bits are in the same byte.
             */
            b = *iptr & (0xff << rbits);
            b |= (signed_val(arg) & MAKE_MASK(num_bits)) << (rbits-num_bits);
            *iptr = b;
        } else if (BIT_OFFSET(num_bits) == 0) {
            /*
             * Little endian small in more than one byte, not
             * aligned on a byte boundary. The size is evenly
             * divisible by 8, which means that there will be
             * one partial byte, followed by zero or more
             * complete bytes, followed by a final partial byte.
             */
            Sint val = signed_val(arg);
            Uint rshift = bit_offset;
            Uint lshift = rbits;
            Uint lmask = MAKE_MASK(rbits);
            Uint count = (num_bits - rbits) / 8;
            Uint bits, bits1;

            /* Handle the first partial byte. */
            bits = val;
            bits1 = bits >> rshift;
            *iptr = MASK_BITS(bits1, *iptr, lmask);
            iptr++;
            val >>= 8;

            /* Handle all complete bytes. */
            while (count--) {
                bits1 = bits << lshift;
                bits = val & 0xff;
                *iptr++ = bits1 | (bits >> rshift);
                val >>= 8;
            }

            /* Handle the final partial byte. */
            *iptr = bits << lshift;
        } else {
            /*
             * Little endian small in more than one byte, not aligned
             * on a byte boundary, and the size is not evenly
             * divisible by 8.
             *
             * Now this gets complicated. We used to handle this
             * directly, but since this case is presumably uncommon,
             * we do this in a simpler way in two steps.
             *
             * First format the integer byte-aligned using the binary
             * itself as a temporary buffer.
             */
            b = *iptr;
            fmt_small_le(iptr, arg, num_bits);

            /*
             * Now restore the overwritten bits of the first byte and
             * shift everything to the right.
             */
            restore_and_shift(iptr, b, bit_offset, num_bits);
        }
    } else if (is_big(arg) && bit_offset == 0) {
        /*
         * Big number, aligned on a byte boundary. We can format the
         * integer directly into the binary.
         */
        fmt_big_le(iptr, arg, num_bits);
    } else if (is_big(arg) && bit_offset + num_bits <= 8) {
        /*
         * All bits are in the same byte.
         */
        Uint rbits = 8 - bit_offset;
        Sint sign = big_sign(arg);
        ErtsDigit* dp = big_v(arg);
        Uint val = sign ? -*dp : *dp;

        b = *iptr & (0xff << rbits);
        b |= (val & MAKE_MASK(num_bits)) << (rbits-num_bits);
        *iptr = b;
    } else if (is_big(arg)) {
        /*
         * Big number, not aligned on a byte boundary.
         *
         * Format the integer byte-aligned using the binary itself as
         * a temporary buffer.
         */
        b = *iptr;
        fmt_big_le(iptr, arg, num_bits);

        /*
         * Now restore the overwritten bits of the first byte and
         * shift everything to the right.
         */
        restore_and_shift(iptr, b, bit_offset, num_bits);
    } else {
        /* Not an integer. */
        return 0;
    }
    EBS->offset = bin_offset + num_bits;
    return 1;
}

static int
native_put_utf8(LbBitWriter *EBS, Eterm arg)
{
    Uint bin_offset = EBS->offset;
    Uint bit_offset;
    Uint num_bits;
    byte tmp_buf[4];
    byte* dst;
    Sint val;

    if (is_not_small(arg)) {
	return 0;
    }
    val = signed_val(arg);
    if (val < 0) {
	return 0;
    }

    if ((bit_offset = BIT_OFFSET(bin_offset)) == 0) {
	/* We can write directly into the destination binary. */
        dst = EBS->data + BYTE_OFFSET(bin_offset);
    } else {
	/* Unaligned destination binary. Must use a temporary buffer. */
	dst = tmp_buf;
    }
    if (val < 0x80) {
	dst[0] = val;
	num_bits = 8;
    } else if (val < 0x800) {
	dst[0] = 0xC0 | (val >> 6);
	dst[1] = 0x80 | (val & 0x3F);
	num_bits = 16;
    } else if (val < 0x10000) {
	if (0xD800 <= val && val <= 0xDFFF) {
	    return 0;
	}
	dst[0] = 0xE0 | (val >> 12);
	dst[1] = 0x80 | ((val >> 6) & 0x3F);
	dst[2] = 0x80 | (val & 0x3F);
	num_bits = 24;
    } else if (val < 0x110000) {
	dst[0] = 0xF0 | (val >> 18);
	dst[1] = 0x80 | ((val >> 12) & 0x3F);
	dst[2] = 0x80 | ((val >> 6) & 0x3F);
	dst[3] = 0x80 | (val & 0x3F);
	num_bits = 32;
    } else {
	return 0;
    }

    if (bit_offset != 0) {
        if (!lb_bits_copy(dst, sizeof(tmp_buf), 0, EBS->data, EBS->capacity, bin_offset, num_bits)) abort();
    }

    EBS->offset += num_bits;

    return 1;
}

static int
native_put_utf16(LbBitWriter *EBS, Eterm arg, Uint flags)
{
    Uint bin_offset = EBS->offset;
    Uint bit_offset;
    Uint num_bits;
    byte tmp_buf[4];
    byte* dst;
    Uint val;

    if (is_not_small(arg)) {
	return 0;
    }
    val = unsigned_val(arg);
    if (val > 0x10FFFF || (0xD800 <= val && val <= 0xDFFF)) {
	return 0;
    }

    if ((bit_offset = BIT_OFFSET(bin_offset)) == 0) {
	/* We can write directly into the destination binary. */
	dst = EBS->data + BYTE_OFFSET(bin_offset);
    } else {
	/* Unaligned destination binary. Must use a temporary buffer. */
	dst = tmp_buf;
    }

    if (val < 0x10000UL) {
	num_bits = 16;
	if (flags & BSF_LITTLE) {
	    dst[0] = val;
	    dst[1] = val >> 8;
	} else {
	    dst[0] = val >> 8;
	    dst[1] = val;
	}
    } else {
	Uint16 w1, w2;

	num_bits = 32;
	val = val - 0x10000UL;
	w1 = 0xD800 | (val >> 10);
	w2 = 0xDC00 | (val & 0x3FF);
	if (flags & BSF_LITTLE) {
	    dst[0] = w1;
	    dst[1] = w1 >> 8;
	    dst[2] = w2;
	    dst[3] = w2 >> 8;
	} else {
	    dst[0] = w1 >> 8;
	    dst[1] = w1;
	    dst[2] = w2 >> 8;
	    dst[3] = w2;
	}
    }

    if (bit_offset != 0) {
	if (!lb_bits_copy(dst, sizeof(tmp_buf), 0, EBS->data, EBS->capacity, bin_offset, num_bits)) abort();
    }

    EBS->offset += num_bits;
    return 1;
}

/* Checked ownership-neutral boundary around the native kernels above. */
static int overlaps(const void *a,size_t an,const void *b,size_t bn)
{
    uintptr_t left=(uintptr_t)a,right=(uintptr_t)b;
    if(!an || !bn) return 0;
    if(left>UINTPTR_MAX-an || right>UINTPTR_MAX-bn) return 1;
    return left<right+bn && right<left+an;
}
static int valid_writer(const LbBitWriter *w)
{
    return w && w->capacity<=SIZE_MAX/8 && (w->data || !w->capacity) &&
           w->offset<=w->capacity*8 && !overlaps(w,sizeof(*w),w->data,w->capacity);
}
static int valid_order(LbBitEndian order)
{
    return order==LB_BITS_BIG_ENDIAN || order==LB_BITS_LITTLE_ENDIAN;
}
static int room(const LbBitWriter *w,size_t bits) { return bits<=w->capacity*8-w->offset; }
static int scalar(Eterm value,Uint *number)
{
    Sint n;
    if(!is_small(value)) return 0;
    n=signed_val(value);
    if(n<0 || n>0x10ffff || (n>=0xd800 && n<=0xdfff)) return 0;
    *number=(Uint)n; return 1;
}
LbBitWriteStatus lb_bit_writer_init(LbBitWriter *w,unsigned char *data,size_t size)
{
    if(!w || (!data && size) || size>SIZE_MAX/8 || overlaps(w,sizeof(*w),data,size)) return LB_BITS_INVALID;
    *w=(LbBitWriter){data,size,0}; return LB_BITS_OK;
}
LbBitWriteStatus lb_bits_put_integer(LbBitWriter *w,Eterm value,size_t bits,LbBitEndian order)
{
    size_t end; unsigned tail; byte saved=0; int success;
    if(!valid_writer(w) || !valid_order(order)) return LB_BITS_INVALID;
    if(!is_small(value) && !is_big(value)) return LB_BITS_BADARG;
    if(!room(w,bits)) return LB_BITS_NO_SPACE;
    if(bits && is_big(value) && overlaps(w->data,w->capacity,boxed_val(value),
                                        (big_size(value)+1)*sizeof(Eterm))) return LB_BITS_INVALID;
    end=w->offset+bits; tail=(unsigned)(end&7);
    /* Native builders own the unwritten tail. This reusable span boundary
     * additionally preserves it; no existing prefix or future segment is lost. */
    if(bits && tail) saved=w->data[end/8];
    success=order==LB_BITS_LITTLE_ENDIAN?native_put_integer_le(w,value,bits):native_put_integer_be(w,value,bits);
    if(!success || w->offset!=end) abort(); /* Type and capacity were checked. */
    if(bits && tail) w->data[end/8]=(byte)MASK_BITS(w->data[end/8],saved,0xffu<<(8-tail));
    return LB_BITS_OK;
}
LbBitWriteStatus lb_bits_put_utf8(LbBitWriter *w,Eterm value)
{
    Uint n; size_t bits;
    if(!valid_writer(w)) return LB_BITS_INVALID;
    if(!scalar(value,&n)) return LB_BITS_BADARG;
    bits=n<0x80?8:n<0x800?16:n<0x10000?24:32;
    if(!room(w,bits)) return LB_BITS_NO_SPACE;
    if(!native_put_utf8(w,value)) abort();
    return LB_BITS_OK;
}
LbBitWriteStatus lb_bits_put_utf16(LbBitWriter *w,Eterm value,LbBitEndian order)
{
    Uint n; size_t bits;
    if(!valid_writer(w) || !valid_order(order)) return LB_BITS_INVALID;
    if(!scalar(value,&n)) return LB_BITS_BADARG;
    bits=n<0x10000?16:32;
    if(!room(w,bits)) return LB_BITS_NO_SPACE;
    if(!native_put_utf16(w,value,order==LB_BITS_LITTLE_ENDIAN?BSF_LITTLE:0)) abort();
    return LB_BITS_OK;
}
LbBitWriteStatus lb_bits_put_utf32(LbBitWriter *w,Eterm value,LbBitEndian order)
{
    Uint n;
    if(!valid_writer(w) || !valid_order(order)) return LB_BITS_INVALID;
    if(!scalar(value,&n)) return LB_BITS_BADARG;
    /* The native construction instruction validates the scalar, then delegates
     * UTF-32 to its fixed-size integer segment writer. */
    return lb_bits_put_integer(w,value,32,order);
}
