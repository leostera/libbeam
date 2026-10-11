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
 * erl_bits.c:erts_bs_put_float/f64_to_f16, with independent conversion storage
 * and a scoped IEEE environment before publication. See otp/bit-writer-sources.json.
 */
#include "bit_writer_internal.h"
#include "big.h"
#include <fenv.h>
#include <float.h>
#include <math.h>
#if defined(__FAST_MATH__) || (defined(__FINITE_MATH_ONLY__) && __FINITE_MATH_ONLY__)
#error "Native numeric semantics require IEEE floating-point behavior"
#endif
#if defined(__FLOAT_WORD_ORDER__) && __FLOAT_WORD_ORDER__ != __BYTE_ORDER__
#error "The selected float writer requires ordinary IEEE word order"
#endif
#if defined(__clang__)
#pragma STDC FENV_ACCESS ON
#elif defined(__GNUC__)
#pragma GCC optimize ("rounding-math")
#endif
_Static_assert(sizeof(double)==8 && DBL_MANT_DIG==53 && DBL_MAX_EXP==1024 && DBL_MIN_EXP==-1021,"Native binary64");
_Static_assert(sizeof(float)==4 && FLT_RADIX==2 && FLT_MANT_DIG==24 && FLT_MAX_EXP==128 && FLT_MIN_EXP==-125,"Native binary32");

/* Native software fallback: single round-to-nearest-even conversion. Going
 * through float first would double-round some binary64 values incorrectly. */
static uint16_t f64_to_f16(double fp)
{
    uint64_t raw,man,keep,low;
    uint16_t sign;
    int exp,e,shift,guard,sticky;
    memcpy(&raw,&fp,sizeof(raw));
    sign=(uint16_t)((raw>>48)&0x8000);
    exp=(int)((raw>>52)&0x7ff);
    man=raw&((UINT64_C(1)<<52)-1);
    assert(exp!=0x7ff);
    if(exp==0) return sign;
    e=exp-1023; man|=UINT64_C(1)<<52;
    if(e>=-14) {
        uint64_t k;
        if(e>15) return (uint16_t)(sign|0x7c00);
        k=man>>42;
        guard=(int)((man>>41)&1);
        sticky=(man&((UINT64_C(1)<<41)-1))!=0;
        if(guard && (sticky || (k&1))) {
            ++k;
            if(k==2048) {
                k=1024; ++e;
                if(e>15) return (uint16_t)(sign|0x7c00);
            }
        }
        return (uint16_t)(sign|((uint16_t)(e+15)<<10)|(uint16_t)(k&0x3ff));
    }
    shift=28-e;
    if(shift>63) return sign;
    keep=man>>shift;
    guard=(int)((man>>(shift-1))&1);
    low=man&((UINT64_C(1)<<(shift-1))-1);
    sticky=low!=0;
    if(guard && (sticky || (keep&1))) ++keep;
    return (uint16_t)(sign|(uint16_t)keep);
}
static int float_term(Eterm value)
{
    return is_boxed(value) && (*boxed_val(value)&_TAG_HEADER_MASK)==FLOAT_SUBTAG;
}
static LbBitFloatError to_double(Eterm value,double *out)
{
    if(is_small(value)) { *out=(double)signed_val(value); return LB_FLOAT_NONE; }
    if(lb_is_big(value)) return lb_big_to_double(value,out)<0?LB_FLOAT_NO_FLOAT:LB_FLOAT_NONE;
    memcpy(out,boxed_val(value)+1,sizeof(*out));
    return isfinite(*out)?LB_FLOAT_NONE:LB_FLOAT_TYPE;
}
static uint32_t narrow_float(double value)
{
    float single; uint32_t bits;
    /* Native IEEE narrowing permits infinity bytes. Spell out the overflow
     * interval instead of relying on an out-of-range ISO C cast. The midpoint
     * between FLT_MAX and 2^128 is exactly representable in binary64. */
    if(value>FLT_MAX) return value<0x1.ffffffp127?UINT32_C(0x7f7fffff):UINT32_C(0x7f800000);
    if(value<-FLT_MAX) return -value<0x1.ffffffp127?UINT32_C(0xff7fffff):UINT32_C(0xff800000);
    single=(float)value; memcpy(&bits,&single,sizeof(bits)); return bits;
}
static LbBitFloatError convert(Eterm value,size_t width,uint64_t *bits)
{
    double number; LbBitFloatError error;
    if(width==32 && is_small(value)) {
        /* Keep the native direct integer-to-binary32 conversion. Going via
         * binary64 can round a small integer twice near a binary32 midpoint. */
        float single=(float)signed_val(value); uint32_t raw;
        memcpy(&raw,&single,sizeof(raw)); *bits=raw; return LB_FLOAT_NONE;
    }
    error=to_double(value,&number); if(error!=LB_FLOAT_NONE) return error;
    if(width==64) memcpy(bits,&number,sizeof(*bits));
    else if(width==32) *bits=narrow_float(number);
    else *bits=f64_to_f16(number);
    return LB_FLOAT_NONE;
}
static LbBitFloatResult result(LbBitWriteStatus status,LbBitFloatError error)
{
    return (LbBitFloatResult){status,error};
}
LbBitFloatResult lb_bits_put_float(LbBitWriter *w,Eterm value,size_t width,LbBitEndian order)
{
    fenv_t previous; uint64_t bits=0; unsigned char bytes[8]; size_t i,count;
    LbBitFloatError error; LbBitWriteStatus status;
    if(!lb_bit_writer_valid(w) || !lb_bit_order_valid(order)) return result(LB_BITS_INVALID,LB_FLOAT_NONE);
    if(width!=16 && width!=32 && width!=64) return result(LB_BITS_BADARG,LB_FLOAT_SIZE);
    if(!is_small(value) && !lb_is_big(value) && !float_term(value)) return result(LB_BITS_BADARG,LB_FLOAT_TYPE);
    if(width>w->capacity*8-w->offset) return result(LB_BITS_NO_SPACE,LB_FLOAT_NONE);
    if(is_boxed(value) && lb_bit_writer_overlaps_source(w,boxed_val(value),
        (lb_is_big(value)?lb_big_size(value)+1:2)*sizeof(Eterm))) return result(LB_BITS_INVALID,LB_FLOAT_NONE);
    if(fegetenv(&previous)) return result(LB_BITS_ENVIRONMENT,LB_FLOAT_NONE);
    if(fesetenv(FE_DFL_ENV)) {
        (void)fesetenv(&previous); return result(LB_BITS_ENVIRONMENT,LB_FLOAT_NONE);
    }
    error=convert(value,width,&bits);
    if(fesetenv(&previous)) return result(LB_BITS_ENVIRONMENT,LB_FLOAT_NONE);
    if(error!=LB_FLOAT_NONE) return result(LB_BITS_BADARG,error);
    /* Native endian byte emission into independent storage, followed by the
     * same forward-copy path for aligned and unaligned destinations. */
    count=width/8;
    for(i=0;i<count;++i) bytes[i]=(unsigned char)(bits>>((order==LB_BITS_LITTLE_ENDIAN?i:count-1-i)*8));
    status=lb_bits_put_string(w,bytes,count);
    return result(status,LB_FLOAT_NONE);
}
