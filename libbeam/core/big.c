/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1996-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Selected big.c/big.h flat-64 digit algorithms: I_comp, I_add, I_sub,
 * small_to_big, big_norm, B_plus_minus, big_plus, base-case multiplication and
 * big_to_double. Caller-owned output;
 * no ERTS allocator, process context, mutable singleton or alternate term ABI.
 */
#include "big.h"
#include <math.h>
#include <float.h>
#if defined(__FAST_MATH__) || (defined(__FINITE_MATH_ONLY__) && __FINITE_MATH_ONLY__)
#error "Native numeric semantics require IEEE floating-point behavior"
#endif
_Static_assert(sizeof(double)==8 && FLT_RADIX==2 && DBL_MANT_DIG==53 && DBL_MAX_EXP==1024 && DBL_MIN_EXP==-1021,"Native binary64 arithmetic");
typedef Uint ErtsDigit;
typedef Uint dsize_t;
#define BIG_V(p) ((p)+1)
#define BIG_SIZE(p) ((Uint)((p)[0]>>_HEADER_ARITY_OFFS))
#define BIG_SIGN(p) (((p)[0]&_TAG_HEADER_MASK)==NEG_BIG_SUBTAG)
#define IS_USMALL(sign,value) ((sign)?((value)<=(Uint)MAX_SMALL+1):((value)<=(Uint)MAX_SMALL))
/* Native big.c double-digit carry path for the selected 64-bit compiler ABI. */
#if !defined(__SIZEOF_INT128__)
#error "Selected native digit addition requires unsigned 128-bit arithmetic"
#endif
#define DSUMc(a,b,c,s) do { \
    __uint128_t sum=(__uint128_t)(a)+(b)+(c); \
    (s)=(ErtsDigit)sum; (c)=(ErtsDigit)(sum>>64); \
} while(0)
static int I_comp(const ErtsDigit *x,dsize_t xl,const ErtsDigit *y,dsize_t yl)
{
    if(xl<yl) return -1;
    if(xl>yl) return 1;
    if(x==y) return 0;
    /* Same most-significant-first comparison, without decrementing a pointer
     * before its allocation when all digits are equal. */
    while(xl) {
        --xl;
        if(x[xl]!=y[xl]) return x[xl]<y[xl]?-1:1;
    }
    return 0;
}
static dsize_t I_add(const ErtsDigit *x,dsize_t xl,const ErtsDigit *y,dsize_t yl,ErtsDigit *r)
{
    dsize_t sz=xl; ErtsDigit yr,xr,c=0;
    assert(xl>=yl && yl);
    xl-=yl;
    do {
        xr=*x++; yr=*y++; DSUMc(xr,yr,c,xr); *r++=xr;
    } while(--yl);
    while(xl--) { xr=*x++ + c; c=xr<c; *r++=xr; }
    if(c) { *r=1; return sz+1; }
    return sz;
}
static dsize_t I_sub(const ErtsDigit *x,dsize_t xl,const ErtsDigit *y,dsize_t yl,ErtsDigit *r)
{
    ErtsDigit *r0=r,yr,xr,c=0;
    assert(yl && I_comp(x,xl,y,yl)>=0);
    xl-=yl;
    do {
        yr=*y++ + c; xr=*x++; c=yr<c; yr=xr-yr; c+=yr>xr; *r++=yr;
    } while(--yl);
    while(xl--) { xr=*x++; yr=xr-c; c=yr>xr; *r++=yr; }
    do { --r; } while(*r==0 && r!=r0);
    return (dsize_t)(r-r0)+1;
}
Eterm lb_small_to_big(Sint x,Eterm *y)
{
    Uint xu;
    if(x>=0) { xu=(Uint)x; *y=_make_header(1,POS_BIG_SUBTAG); }
    else { xu=-(Uint)x; *y=_make_header(1,NEG_BIG_SUBTAG); }
    y[1]=xu; return make_boxed(y);
}
static Eterm big_norm(Eterm *x,dsize_t xl,int sign)
{
    if(xl==1) {
        Uint y=x[1];
        if(IS_USMALL(sign,y)) return make_small(sign?-(Sint)y:(Sint)y);
    }
    if(xl>LB_BIG_ARITY_MAX) return NIL;
    *x=_make_header(xl,sign?NEG_BIG_SUBTAG:POS_BIG_SUBTAG); return make_boxed(x);
}
static Eterm B_plus_minus(const ErtsDigit *x,dsize_t xl,int xs,const ErtsDigit *y,dsize_t yl,int ys,Eterm *r)
{
    if(xs==ys) {
        if(xl>yl) return big_norm(r,I_add(x,xl,y,yl,BIG_V(r)),xs);
        return big_norm(r,I_add(y,yl,x,xl,BIG_V(r)),xs);
    } else {
        int comp=I_comp(x,xl,y,yl);
        if(!comp) return make_small(0);
        if(comp>0) return big_norm(r,I_sub(x,xl,y,yl,BIG_V(r)),xs);
        return big_norm(r,I_sub(y,yl,x,xl,BIG_V(r)),ys);
    }
}
/* Native big_to_double, selected D_EXP=64 path. Caller supplies an IEEE
 * floating environment; canonical bignums exceed the small-integer range. */
int lb_big_to_double(Eterm x,double *result)
{
    const Eterm *xp=boxed_val(x); size_t xl=BIG_SIZE(xp),i;
    const ErtsDigit *v=BIG_V(xp); ErtsDigit msd,lesser_bits;
    Uint mant; unsigned msd_bits=0,bitlen,guard,half_bit,exp; double d;
    assert(lb_is_big(x) && xl && v[xl-1]);
    /* Reject huge values before narrowing the bit count. */
    if(xl>16) return -1;
    msd=v[xl-1]; while(msd) { ++msd_bits; msd>>=1; }
    bitlen=(unsigned)(xl-1)*64+msd_bits;
    assert(bitlen>53);
    if(bitlen>1024) return -1;
    guard=bitlen-54; mant=0;
    for(i=xl-1;;--i) {
        ErtsDigit digit=v[i]; unsigned lsb=(unsigned)i*64;
        if(lsb>guard) mant|=digit<<(lsb-guard);
        else {
            unsigned k=guard-lsb;
            lesser_bits=digit&(((ErtsDigit)1<<k)-1); mant|=digit>>k; break;
        }
    }
    half_bit=(unsigned)(mant&1); mant>>=1; exp=guard+1;
    if(half_bit) {
        if(!(mant&1)) {
            while(!lesser_bits) {
                if(!i) goto mant_exp_done;
                lesser_bits=v[--i];
            }
        }
        ++mant;
        if(mant==((Uint)1<<53)) { mant>>=1; ++exp; }
    }
mant_exp_done:
    d=ldexp((double)mant,(int)exp);
    if(!isfinite(d)) return -1;
    *result=BIG_SIGN(xp)?-d:d; return 0;
}
Eterm lb_big_plus(Eterm x,Eterm y,Eterm *r)
{
    const Eterm *xp=boxed_val(x),*yp=boxed_val(y);
    assert(lb_is_big(x) && lb_is_big(y) && BIG_SIZE(xp) && BIG_SIZE(yp));
    return B_plus_minus(BIG_V(xp),BIG_SIZE(xp),BIG_SIGN(xp),BIG_V(yp),BIG_SIZE(yp),BIG_SIGN(yp),r);
}
Eterm lb_big_minus(Eterm x,Eterm y,Eterm *r)
{
    const Eterm *xp=boxed_val(x),*yp=boxed_val(y);
    assert(lb_is_big(x) && lb_is_big(y) && BIG_SIZE(xp) && BIG_SIZE(yp));
    return B_plus_minus(BIG_V(xp),BIG_SIZE(xp),BIG_SIGN(xp),BIG_V(yp),BIG_SIZE(yp),!BIG_SIGN(yp),r);
}
/* Adapted native I_mul base case. Independent output is zeroed up front;
 * inputs never alias it. A double digit combines the native DMULc/DSUMc
 * carries without overflow: (2^64-1)^2 + 2*(2^64-1) == 2^128-1. */
static dsize_t I_mul(const ErtsDigit *x,dsize_t xl,const ErtsDigit *y,dsize_t yl,ErtsDigit *r)
{
    memset(r,0,(xl+yl)*sizeof(*r));
    for(dsize_t i=0;i<xl;++i) {
        ErtsDigit carry=0;
        for(dsize_t j=0;j<yl;++j) {
            __uint128_t product=(__uint128_t)x[i]*y[j]+r[i+j]+carry;
            r[i+j]=(ErtsDigit)product; carry=(ErtsDigit)(product>>64);
        }
        r[i+yl]=carry;
    }
    dsize_t size=xl+yl;
    while(size>1 && !r[size-1]) --size;
    return size;
}
Eterm lb_big_times(Eterm x,Eterm y,Eterm *r)
{
    const Eterm *xp=boxed_val(x),*yp=boxed_val(y);
    dsize_t xl=BIG_SIZE(xp),yl=BIG_SIZE(yp);
    assert(lb_is_big(x) && lb_is_big(y) && xl && yl);
    if(xl+yl>LB_BIG_ARITY_MAX+1) return NIL;
    return big_norm(r,I_mul(BIG_V(xp),xl,BIG_V(yp),yl,BIG_V(r)),BIG_SIGN(xp)^BIG_SIGN(yp));
}
/* Native D_div semantics expressed with indices (no before-allocation
 * pointers). Multi-digit division uses normalized Knuth-D estimates: each
 * quotient digit has at most two estimate corrections and one add-back. */
int lb_big_div_rem(Eterm left,Eterm right,Eterm *q,Eterm *r,
                   Eterm *scratch,size_t scratch_words,LbBigDivision *out)
{
    const Eterm *x=boxed_val(left),*y=boxed_val(right);
    size_t xl=BIG_SIZE(x),yl=BIG_SIZE(y),ql=1,rl=1;
    assert(lb_is_big(left) && lb_is_big(right) && xl && yl);
    if(!out || !q || !r || !scratch || xl>LB_BIG_ARITY_MAX || yl>LB_BIG_ARITY_MAX ||
       scratch_words<xl+yl+1 || (yl==1 && !y[1])) return 0;
    int order=I_comp(BIG_V(x),xl,BIG_V(y),yl);
    if(order<0) {
        q[1]=0; memcpy(r+1,x+1,xl*sizeof(Eterm)); rl=xl;
    } else if(order==0) {
        q[1]=1; r[1]=0;
    } else if(yl==1) {
        Uint remainder=0,divisor=y[1];
        for(size_t i=xl;i>0;--i) {
            __uint128_t numerator=((__uint128_t)remainder<<64)|x[i];
            q[i]=(Uint)(numerator/divisor); remainder=(Uint)(numerator%divisor);
        }
        ql=xl; r[1]=remainder;
    } else {
        Uint *u=scratch,*v=scratch+xl+1,carry=0;
        unsigned shift=(unsigned)__builtin_clzll(y[yl]);
        for(size_t i=0;i<xl;++i) {
            u[i]=shift?(x[i+1]<<shift)|carry:x[i+1];
            carry=shift?x[i+1]>>(64-shift):0;
        }
        u[xl]=carry; carry=0;
        for(size_t i=0;i<yl;++i) {
            v[i]=shift?(y[i+1]<<shift)|carry:y[i+1];
            carry=shift?y[i+1]>>(64-shift):0;
        }
        assert(!carry);
        const __uint128_t base=(__uint128_t)1<<64;
        ql=xl-yl+1;
        for(size_t span=ql;span>0;) {
            size_t j=--span; Uint estimate;
            __uint128_t remainder;
            assert(u[j+yl]<=v[yl-1]);
            if(u[j+yl]==v[yl-1]) {
                estimate=UINT64_MAX; remainder=(__uint128_t)u[j+yl-1]+v[yl-1];
            } else {
                __uint128_t numerator=((__uint128_t)u[j+yl]<<64)|u[j+yl-1];
                estimate=(Uint)(numerator/v[yl-1]); remainder=numerator%v[yl-1];
            }
            for(unsigned correction=0;correction<2 && remainder<base &&
                (__uint128_t)estimate*v[yl-2]>(remainder<<64)+u[j+yl-2];++correction) {
                --estimate; remainder+=v[yl-1];
            }
            assert(remainder>=base || (__uint128_t)estimate*v[yl-2]<=(remainder<<64)+u[j+yl-2]);
            Uint borrow=0; carry=0;
            for(size_t i=0;i<yl;++i) {
                __uint128_t product=(__uint128_t)estimate*v[i]+carry;
                Uint low=(Uint)product,previous=u[j+i]; carry=(Uint)(product>>64);
                u[j+i]=previous-low-borrow;
                borrow=previous<low || (borrow && previous==low);
            }
            __uint128_t subtrahend=(__uint128_t)carry+borrow;
            int negative=(__uint128_t)u[j+yl]<subtrahend;
            u[j+yl]=(Uint)((__uint128_t)u[j+yl]-subtrahend);
            if(negative) {
                --estimate; carry=0;
                for(size_t i=0;i<yl;++i) {
                    __uint128_t sum=(__uint128_t)u[j+i]+v[i]+carry;
                    u[j+i]=(Uint)sum; carry=(Uint)(sum>>64);
                }
                u[j+yl]+=carry;
            }
            q[j+1]=estimate;
        }
        assert(!u[yl]);
        for(size_t i=0;i<yl;++i)
            r[i+1]=shift?(u[i]>>shift)|(u[i+1]<<(64-shift)):u[i];
        rl=yl;
    }
    while(ql>1 && !q[ql]) --ql;
    while(rl>1 && !r[rl]) --rl;
    out->quotient=big_norm(q,ql,BIG_SIGN(x)^BIG_SIGN(y));
    out->remainder=big_norm(r,rl,BIG_SIGN(x));
    return 1;
}
static void big_to_twos(Eterm value,size_t count,Eterm *digits)
{
    const Eterm *source=boxed_val(value); size_t length=BIG_SIZE(source);
    int negative=BIG_SIGN(source); Uint carry=1;
    for(size_t i=0;i<count;++i) {
        Uint digit=i<length?BIG_V(source)[i]:0;
        if(!negative) digits[i]=digit;
        else if(carry) { digits[i]=~digit+1; carry=digits[i]==0; }
        else digits[i]=~digit;
    }
}
Eterm lb_big_bitwise(Eterm left,Eterm right,unsigned operation,Eterm *scratch,size_t scratch_words)
{
    size_t left_size,right_size,count,required,digits;
    Eterm *a,*b,*result; int negative;
    if(!lb_is_big(left) || !lb_is_big(right) || !scratch || operation>2) return NIL;
    left_size=lb_big_size(left); right_size=lb_big_size(right);
    count=(left_size>right_size?left_size:right_size)+1;
    if(!lb_size_mul(count,3,&required) || !lb_size_add(required,2,&required) || scratch_words<required) return NIL;
    a=scratch+1; b=a+count; result=b+count;
    big_to_twos(left,count,a); big_to_twos(right,count,b);
    for(size_t i=0;i<count;++i)
        result[i+1]=operation==0?(a[i]&b[i]):operation==1?(a[i]|b[i]):(a[i]^b[i]);
    negative=(result[count]>>63)!=0;
    if(negative) {
        Uint carry=1;
        for(size_t i=0;i<count;++i) { Uint prior=~result[i+1]; result[i+1]=prior+carry; carry=carry && result[i+1]==0; }
    }
    digits=count; while(digits>1 && result[digits]==0) --digits;
    return big_norm(result,(dsize_t)digits,negative);
}
Eterm lb_big_band(Eterm left,Eterm right,Eterm *scratch,size_t scratch_words)
{ return lb_big_bitwise(left,right,0,scratch,scratch_words); }
Eterm lb_big_bnot(Eterm value,Eterm *scratch,size_t scratch_words)
{
    size_t count,required,digits; Eterm *twos,*result; int negative;
    if(!lb_is_big(value) || !scratch) return NIL;
    count=lb_big_size(value)+1;
    if(!lb_size_mul(count,2,&required) || !lb_size_add(required,2,&required) || scratch_words<required) return NIL;
    twos=scratch+1; result=twos+count;
    big_to_twos(value,count,twos);
    for(size_t i=0;i<count;++i) result[i+1]=~twos[i];
    negative=(result[count]>>63)!=0;
    if(negative) {
        Uint carry=1;
        for(size_t i=0;i<count;++i) { Uint prior=~result[i+1]; result[i+1]=prior+carry; carry=carry && result[i+1]==0; }
    }
    digits=count; while(digits>1 && result[digits]==0) --digits;
    return big_norm(result,(dsize_t)digits,negative);
}
Eterm lb_big_shift(Eterm value,int left,size_t bits,Eterm *output,size_t output_words)
{
    const Eterm *source=boxed_val(value); size_t count=BIG_SIZE(source),word_shift,rest,digits;
    const ErtsDigit *input=BIG_V(source); int sign=BIG_SIGN(source);
    if(!lb_is_big(value) || !count || !output) return NIL;
    if(!bits) return value;
    if(left) {
        word_shift=bits/64; rest=bits%64;
        if(word_shift>LB_BIG_ARITY_MAX-count) return NIL;
        digits=count+word_shift;
        if(rest && (input[count-1]>>(64-rest))) {
            if(digits==LB_BIG_ARITY_MAX) return NIL;
            ++digits;
        }
        if(digits>LB_BIG_ARITY_MAX || output_words<digits+1) return NIL;
        memset(output+1,0,digits*sizeof(*output));
        for(size_t i=0;i<count;++i) {
            size_t index=word_shift+i;
            output[index+1]|=input[i]<<rest;
            if(rest) {
                ErtsDigit carry=input[i]>>(64-rest);
                if(carry) output[index+2]|=carry;
            }
        }
        return big_norm(output,(dsize_t)digits,sign);
    }
    size_t high_bits=0; ErtsDigit high=input[count-1];
    while(high) { ++high_bits; high>>=1; }
    size_t bit_length=(count-1)*64+high_bits;
    if(bits>=bit_length) return make_small(sign?-1:0);
    word_shift=bits/64; rest=bits%64; digits=count-word_shift;
    if(output_words<digits+2) return NIL; /* room for negative round-up carry */
    memset(output+1,0,(digits+1)*sizeof(*output));
    for(size_t i=0;i<digits;++i) {
        size_t source_index=word_shift+i;
        output[i+1]=input[source_index]>>rest;
        if(rest && source_index+1<count) output[i+1]|=input[source_index+1]<<(64-rest);
    }
    if(sign) {
        int discarded=0;
        for(size_t i=0;i<word_shift;++i) discarded|=input[i]!=0;
        if(rest) discarded|=(input[word_shift]&(((ErtsDigit)1<<rest)-1))!=0;
        if(discarded) {
            ErtsDigit carry=1; size_t i=0;
            for(;i<digits && carry;++i) { ErtsDigit prior=output[i+1]; output[i+1]=prior+1; carry=output[i+1]<prior; }
            if(carry) { output[digits+1]=1; ++digits; }
        }
    }
    return big_norm(output,(dsize_t)digits,sign);
}
