/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1996-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Selected big.c/big.h flat-64 digit algorithms: I_comp, I_add, I_sub,
 * small_to_big, big_norm, B_plus_minus, big_plus and big_to_double. Caller-owned output;
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
