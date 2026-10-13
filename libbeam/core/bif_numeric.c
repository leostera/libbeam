/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1996-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Owned adaptation of erl_arith.c:erts_mixed_plus. Integer digit algorithms
 * remain in big.c. Compute before collection, then publish into the owned heap;
 * no untracked ERTS heap fragments or dangling C operands across collection.
 */
#define _XOPEN_SOURCE 700
#include "process_internal.h"
#include "binary.h"
#include "binary_build.h"
#include "atoms.h"
#include "big.h"
#include "term_compare.h"
#include "bit_copy.h"
#include <errno.h>
#include <fenv.h>
#include <locale.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#if defined(__FAST_MATH__) || (defined(__FINITE_MATH_ONLY__) && __FINITE_MATH_ONLY__)
#error "Native numeric semantics require IEEE floating-point behavior"
#endif
#if defined(__clang__)
#pragma STDC FENV_ACCESS ON
#elif defined(__GNUC__)
#pragma GCC optimize ("rounding-math")
#endif
static Eterm fail(LbProcess *p,Uint reason)
{
    p->freason=reason; return THE_NON_VALUE;
}
static Eterm no_memory(LbProcess *p)
{
    p->status=LB_PROCESS_NO_MEMORY; return THE_NON_VALUE;
}
static Eterm allocation_failure(LbProcess *p,LbAllocStatus status)
{
    if(status==LB_ALLOC_LIMIT) p->status=LB_PROCESS_LIMIT;
    else if(status==LB_ALLOC_WORK_LIMIT) p->status=LB_PROCESS_WORK_LIMIT;
    else p->status=LB_PROCESS_NO_MEMORY;
    return THE_NON_VALUE;
}
static Eterm publish_words(LbProcess *p,const Eterm *words,size_t count)
{
    Eterm *hp; size_t need,collections=p->collections;
    /* Native arithmetic uses non-collecting heap fragments. The owned adapter
     * instead finishes computation into independent storage before collecting.
     * Preserve pending heap credit and all physically initialized X/Y roots. */
    if(!lb_size_add(count,p->heap_reserved,&need)) return fail(p,LB_EXC_SYSTEM_LIMIT);
    if(!lb_heap_reserve(p,need,MAX_REG)) return allocation_failure(p,lb_alloc_domain_failure(p->entry_module->space->domain));
    if(p->collections!=collections) p->last_native_cost+=p->last_gc_cost;
    hp=p->htop; memcpy(hp,words,count*sizeof(*hp)); p->htop+=count; return make_boxed(hp);
}
static Eterm integer_arithmetic(LbProcess *p,Eterm a,Eterm b,int subtract)
{
    Eterm small_a[2],small_b[2],result; Eterm *words;
    LbAllocDomain *domain=p->entry_module->space->domain; size_t size,bytes; void *memory;
    if(is_small(a) && is_small(b)) {
        Sint sum=signed_val(a)+(subtract?-signed_val(b):signed_val(b));
        if(IS_SSMALL(sum)) return make_small(sum);
        lb_small_to_big(sum,small_a); return publish_words(p,small_a,2);
    }
    if(is_small(a)) {
        if(a==make_small(0) && !subtract) return b;
        a=lb_small_to_big(signed_val(a),small_a);
    }
    if(is_small(b)) {
        if(b==make_small(0)) return a;
        b=lb_small_to_big(signed_val(b),small_b);
    }
    size=lb_big_size(a)>lb_big_size(b)?lb_big_size(a):lb_big_size(b);
    if(size>LB_BIG_ARITY_MAX) return fail(p,LB_EXC_SYSTEM_LIMIT);
    size+=2;
    // Comparison, debug invariant comparison, subtraction and normalization
    // can each visit the full limb span; prepay before independent scratch.
    if(!lb_work_domain_charge(domain,4*size+1)) return no_memory(p);
    if(!lb_size_mul(size,sizeof(Eterm),&bytes) || lb_alloc_domain_allocate(domain,bytes,&memory)!=LB_ALLOC_OK) return no_memory(p);
    words=memory; result=subtract?lb_big_minus(a,b,words):lb_big_plus(a,b,words);
    p->last_native_cost=size/16+1; /* native ERTS_ARITH_WORDS_PER_REDUCTION */
    if(is_nil(result)) result=fail(p,LB_EXC_SYSTEM_LIMIT);
    else if(lb_is_big(result)) result=publish_words(p,words,lb_big_size(result)+1);
    lb_release(domain,words); return result;
}
static int is_float_term(Eterm term)
{
    return is_boxed(term) && (*boxed_val(term)&_TAG_HEADER_MASK)==FLOAT_SUBTAG;
}
static int number_to_double(Eterm term,double *value)
{
    if(is_small(term)) { *value=(double)signed_val(term); return 1; }
    if(lb_is_big(term)) return lb_big_to_double(term,value)==0;
    if(is_float_term(term)) { memcpy(value,boxed_val(term)+1,sizeof(*value)); return isfinite(*value); }
    return 0;
}
typedef enum { FLOAT_ADD, FLOAT_SUBTRACT, FLOAT_MULTIPLY } FloatOperation;
static Eterm float_arithmetic(LbProcess *p,Eterm a,Eterm b,FloatOperation operation)
{
    fenv_t previous; double left,right,sum=0; Eterm words[2]; int valid;
    _Static_assert(sizeof(double)==sizeof(Eterm),"Native flat-64 float payload");
    uint64_t work=1+(lb_is_big(a)?LB_BIG_TO_DOUBLE_FIXED_WORK+lb_big_size(a):0)+
        (lb_is_big(b)?LB_BIG_TO_DOUBLE_FIXED_WORK+lb_big_size(b):0);
    if(!lb_work_domain_charge(p->entry_module->space->domain,work)) return allocation_failure(p,LB_ALLOC_WORK_LIMIT);
    p->last_native_cost+=work;
    /* The host may use another rounding mode or enabled FP traps. This scope
     * never exposes those settings to guest arithmetic, nor changes the host
     * control thread. Restore the worker's environment before any allocation. */
    if(fegetenv(&previous)) goto environment_failure;
    if(fesetenv(FE_DFL_ENV)) { (void)fesetenv(&previous); goto environment_failure; }
    valid=number_to_double(a,&left) && number_to_double(b,&right);
    if(valid) {
        sum=operation==FLOAT_MULTIPLY?left*right:operation==FLOAT_SUBTRACT?left-right:left+right;
        valid=isfinite(sum);
    }
    if(fesetenv(&previous)) goto environment_failure;
    if(!valid) return fail(p,LB_EXC_BADARITH);
    words[0]=_make_header(1,FLOAT_SUBTAG); memcpy(words+1,&sum,sizeof(sum));
    return publish_words(p,words,2);
environment_failure:
    /* Infrastructure refusal is not a false arithmetic guard or bad number. */
    p->status=LB_PROCESS_ENVIRONMENT; return THE_NON_VALUE;
}
Eterm lb_mixed_plus(LbProcess *p,Eterm a,Eterm b)
{
    p->last_native_cost=0;
    if((is_small(a)||lb_is_big(a)) && (is_small(b)||lb_is_big(b))) return integer_arithmetic(p,a,b,0);
    if(is_float_term(a) || is_float_term(b)) return float_arithmetic(p,a,b,FLOAT_ADD);
    return fail(p,LB_EXC_BADARITH);
}
Eterm lb_bif_plus_2(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)pc; return lb_mixed_plus(p,args[0],args[1]);
}
Eterm lb_mixed_times(LbProcess *p,Eterm a,Eterm b)
{
    Eterm small_a[2],small_b[2]; p->last_native_cost=0;
    if(is_float_term(a) || is_float_term(b)) return float_arithmetic(p,a,b,FLOAT_MULTIPLY);
    if(!(is_small(a)||lb_is_big(a)) || !(is_small(b)||lb_is_big(b))) return fail(p,LB_EXC_BADARITH);
    if(is_small(a) && is_small(b)) {
        Sint product;
        if(!__builtin_mul_overflow(signed_val(a),signed_val(b),&product)) {
            if(IS_SSMALL(product)) return make_small(product);
            lb_small_to_big(product,small_a); return publish_words(p,small_a,2);
        }
    }
    if(a==make_small(0) || b==make_small(0)) return make_small(0);
    if(is_small(a)) a=lb_small_to_big(signed_val(a),small_a);
    if(is_small(b)) b=lb_small_to_big(signed_val(b),small_b);
    size_t left=lb_big_size(a),right=lb_big_size(b),digits,words,bytes;
    if(!lb_size_add(left,right,&digits) || digits>LB_BIG_ARITY_MAX+1) return fail(p,LB_EXC_SYSTEM_LIMIT);
    if(!lb_size_add(digits,1,&words) || !lb_size_mul(words,sizeof(Eterm),&bytes))
        return allocation_failure(p,LB_ALLOC_LIMIT);
    /* Use the native schoolbook base case for all admitted sizes. Prepay
     * every digit product and the initialization/normalization span before
     * scratch allocation; no uncharged quadratic work or borrowed operands
     * survive publication's possible moving collection. */
    uint64_t work=(uint64_t)left*right+2*digits+1;
    LbAllocDomain *domain=p->entry_module->space->domain; void *memory;
    if(!lb_work_domain_charge(domain,work)) return allocation_failure(p,LB_ALLOC_WORK_LIMIT);
    p->last_native_cost=lb_work_bulk_units(work);
    LbAllocStatus status=lb_alloc_domain_allocate(domain,bytes,&memory);
    if(status!=LB_ALLOC_OK) return allocation_failure(p,status);
    Eterm result=lb_big_times(a,b,memory);
    if(is_nil(result)) result=fail(p,LB_EXC_SYSTEM_LIMIT);
    else if(lb_is_big(result)) result=publish_words(p,memory,lb_big_size(result)+1);
    lb_release(domain,memory); return result;
}
Eterm lb_bif_times_2(LbProcess *p,Eterm *args,const BeamInstr *pc)
{ (void)pc; return lb_mixed_times(p,args[0],args[1]); }
int lb_int_div_rem(LbProcess *p,Eterm a,Eterm b,Eterm *quotient,Eterm *remainder)
{
    Eterm small_a[2],small_b[2]; p->last_native_cost=1;
    if(!(is_small(a)||lb_is_big(a)) || !(is_small(b)||lb_is_big(b)) || b==make_small(0)) {
        fail(p,LB_EXC_BADARITH); return 0;
    }
    LbAllocDomain *domain=p->entry_module->space->domain;
    if(!lb_work_domain_charge(domain,1)) { allocation_failure(p,LB_ALLOC_WORK_LIMIT); return 0; }
    if(is_small(a) && is_small(b)) {
        Sint q=signed_val(a)/signed_val(b),r=signed_val(a)%signed_val(b);
        Eterm result;
        if(IS_SSMALL(q)) result=make_small(q);
        else { lb_small_to_big(q,small_a); result=publish_words(p,small_a,2); }
        if(is_non_value(result)) return 0;
        *quotient=result; *remainder=make_small(r); return 1;
    }
    if(a==make_small(0)) { *quotient=*remainder=make_small(0); return 1; }
    if(is_small(a)) a=lb_small_to_big(signed_val(a),small_a);
    if(is_small(b)) b=lb_small_to_big(signed_val(b),small_b);
    size_t xl=lb_big_size(a),yl=lb_big_size(b),sum,scratch_words,total,bytes;
    if(xl>LB_BIG_ARITY_MAX || yl>LB_BIG_ARITY_MAX) { fail(p,LB_EXC_SYSTEM_LIMIT); return 0; }
    if(!lb_size_add(xl,yl,&sum) || !lb_size_add(sum,1,&scratch_words) ||
       !lb_size_mul(sum,2,&total) || !lb_size_add(total,3,&total) || !lb_size_mul(total,sizeof(Eterm),&bytes)) {
        allocation_failure(p,LB_ALLOC_LIMIT); return 0;
    }
    /* Normalized division bounds estimate corrections to two and add-back
     * to one per quotient digit. Prepay the full rectangular traversal plus
     * normalization/copies, including the single-digit and comparison paths. */
    uint64_t work=5*(uint64_t)(xl>=yl?xl-yl+1:0)*yl+8*(uint64_t)sum+8;
    if(!lb_work_domain_charge(domain,work)) { allocation_failure(p,LB_ALLOC_WORK_LIMIT); return 0; }
    p->last_native_cost+=lb_work_bulk_units(work);
    Eterm *memory; LbAllocStatus status=lb_alloc_domain_allocate(domain,bytes,(void **)&memory);
    if(status!=LB_ALLOC_OK) { allocation_failure(p,status); return 0; }
    Eterm *q=memory,*r=q+xl+1,*scratch=r+yl+1;
    LbBigDivision result;
    if(!lb_big_div_rem(a,b,q,r,scratch,scratch_words,&result)) {
        p->status=LB_PROCESS_INVALID; lb_release(domain,memory); return 0;
    }
    size_t qwords=lb_is_big(result.quotient)?lb_big_size(result.quotient)+1:0;
    size_t rwords=lb_is_big(result.remainder)?lb_big_size(result.remainder)+1:0;
    size_t need,collections=p->collections;
    if(!lb_size_add(qwords+rwords,p->heap_reserved,&need)) {
        allocation_failure(p,LB_ALLOC_LIMIT); lb_release(domain,memory); return 0;
    }
    /* Both results are published after a single reservation. Publishing one
     * before reserving the other would leave an unrooted moving C operand. */
    if((qwords || rwords) && !lb_heap_reserve(p,need,MAX_REG)) {
        allocation_failure(p,lb_alloc_domain_failure(domain)); lb_release(domain,memory); return 0;
    }
    if(p->collections!=collections) p->last_native_cost+=p->last_gc_cost;
    if(qwords) { memcpy(p->htop,q,qwords*sizeof(Eterm)); result.quotient=make_boxed(p->htop); p->htop+=qwords; }
    if(rwords) { memcpy(p->htop,r,rwords*sizeof(Eterm)); result.remainder=make_boxed(p->htop); p->htop+=rwords; }
    lb_release(domain,memory); *quotient=result.quotient; *remainder=result.remainder; return 1;
}
Eterm lb_bif_intdiv_2(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    Eterm quotient,remainder; (void)pc;
    return lb_int_div_rem(p,args[0],args[1],&quotient,&remainder)?quotient:THE_NON_VALUE;
}
Eterm lb_bif_rem_2(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    Eterm quotient,remainder; (void)pc;
    return lb_int_div_rem(p,args[0],args[1],&quotient,&remainder)?remainder:THE_NON_VALUE;
}
Eterm lb_unary_minus(LbProcess *p,Eterm value)
{
    LbAllocDomain *domain=p->entry_module->space->domain; p->last_native_cost=1;
    if(!lb_work_domain_charge(domain,1)) return allocation_failure(p,LB_ALLOC_WORK_LIMIT);
    if(is_small(value) || lb_is_big(value)) {
        /* Native unary negation is big_minus(zero,value), including the
         * asymmetric MIN_SMALL boundary. Compute before possible collection. */
        Eterm result=integer_arithmetic(p,make_small(0),value,1);
        if(is_non_value(result) && p->status==LB_PROCESS_NO_MEMORY && lb_alloc_domain_failure(domain)!=LB_ALLOC_OK)
            return allocation_failure(p,lb_alloc_domain_failure(domain));
        return result;
    }
    if(is_float_term(value)) {
        Uint bits=boxed_val(value)[1]; Eterm words[2];
        if((bits&UINT64_C(0x7ff0000000000000))==UINT64_C(0x7ff0000000000000)) return fail(p,LB_EXC_BADARITH);
        /* IEEE unary negation flips both signed zeros, without rounding or
         * floating-environment effects. Nonfinite results remain badarith. */
        words[0]=_make_header(1,FLOAT_SUBTAG); words[1]=bits^UINT64_C(0x8000000000000000);
        return publish_words(p,words,2);
    }
    return fail(p,LB_EXC_BADARITH);
}
Eterm lb_bif_minus_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{ (void)pc; return lb_unary_minus(p,args[0]); }
static Eterm mixed_minus(LbProcess *p,Eterm a,Eterm b)
{
    p->last_native_cost=0;
    if((is_small(a)||lb_is_big(a)) && (is_small(b)||lb_is_big(b))) return integer_arithmetic(p,a,b,1);
    if(is_float_term(a) || is_float_term(b)) return float_arithmetic(p,a,b,FLOAT_SUBTRACT);
    return fail(p,LB_EXC_BADARITH);
}
Eterm lb_bif_minus_2(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)pc; return mixed_minus(p,args[0],args[1]);
}
static int big_is_negative(Eterm value)
{ return (*boxed_val(value)&_TAG_HEADER_MASK)==NEG_BIG_SUBTAG; }
static Eterm shift_integer(LbProcess *p,Eterm value,int left,size_t bits)
{
    LbAllocDomain *domain=p->entry_module->space->domain;
    Eterm local[2],*words; size_t input_words,capacity,bytes,work; void *memory=NULL;
    if(!bits || (is_small(value) && signed_val(value)==0)) return value;
    if(is_small(value) && !left) {
        if(bits>=SMALL_BITS-1) return make_small(signed_val(value)<0?-1:0);
        return make_small(signed_val(value)>>bits);
    }
    if(is_small(value) && left && bits<64) {
        __extension__ typedef __int128 LbWide;
        LbWide shifted=(LbWide)signed_val(value)*((LbWide)1<<bits);
        if(shifted>=MIN_SMALL && shifted<=MAX_SMALL) return make_small((Sint)shifted);
    }
    input_words=is_small(value)?1:lb_big_size(value);
    if(left) {
        size_t word_shift=bits/64;
        if(word_shift>LB_BIG_ARITY_MAX-input_words) return fail(p,LB_EXC_SYSTEM_LIMIT);
        capacity=input_words+word_shift+3;
        if(capacity>LB_BIG_ARITY_MAX+3u) return fail(p,LB_EXC_SYSTEM_LIMIT);
    } else capacity=input_words+2;
    if(!lb_size_mul(capacity,sizeof(Eterm),&bytes)) return fail(p,LB_EXC_SYSTEM_LIMIT);
    work=1+input_words+capacity+lb_work_bulk_units(bytes);
    if(!lb_work_domain_charge(domain,work)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    p->last_native_cost+=work;
    LbAllocStatus status=lb_alloc_domain_allocate(domain,bytes,&memory);
    if(status!=LB_ALLOC_OK) return allocation_failure(p,status);
    words=memory;
    if(is_small(value)) value=lb_small_to_big(signed_val(value),local);
    Eterm shifted=lb_big_shift(value,left,bits,words,capacity);
    if(is_nil(shifted)) {
        if(lb_alloc_domain_release(domain,words)!=LB_ALLOC_OK) abort();
        return fail(p,LB_EXC_SYSTEM_LIMIT);
    }
    Eterm result=is_small(shifted)?shifted:publish_words(p,words,lb_big_size(shifted)+1);
    if(lb_alloc_domain_release(domain,words)!=LB_ALLOC_OK) abort();
    return result;
}
Eterm lb_bif_bsr_2(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    Eterm value=args[0],shift=args[1]; Sint amount;
    (void)pc; p->last_native_cost=1;
    if(!(is_small(value)||lb_is_big(value)) || !(is_small(shift)||lb_is_big(shift))) return fail(p,LB_EXC_BADARITH);
    if(lb_is_big(shift)) {
        if(!big_is_negative(shift)) return make_small(is_small(value)?(signed_val(value)<0?-1:0):(big_is_negative(value)?-1:0));
        if(is_small(value) && signed_val(value)==0) return value;
        return fail(p,LB_EXC_SYSTEM_LIMIT);
    }
    amount=signed_val(shift);
    if(amount>=0) {
        if(is_small(value)) {
            if((Uint)amount>=SMALL_BITS-1) return make_small(signed_val(value)<0?-1:0);
            return make_small(signed_val(value)>>amount);
        }
        return shift_integer(p,value,0,(size_t)amount);
    }
    return shift_integer(p,value,1,(size_t)-amount);
}
Eterm lb_bif_bsl_2(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    Eterm value=args[0],shift=args[1]; Sint amount;
    (void)pc; p->last_native_cost=1;
    if(!(is_small(value)||lb_is_big(value)) || !(is_small(shift)||lb_is_big(shift))) return fail(p,LB_EXC_BADARITH);
    if(lb_is_big(shift)) {
        if(big_is_negative(shift)) return make_small(is_small(value)?(signed_val(value)<0?-1:0):(big_is_negative(value)?-1:0));
        if(is_small(value) && signed_val(value)==0) return value;
        return fail(p,LB_EXC_SYSTEM_LIMIT);
    }
    amount=signed_val(shift);
    return amount>=0?shift_integer(p,value,1,(size_t)amount):shift_integer(p,value,0,(size_t)-amount);
}
static unsigned char binary_view_byte(LbBitstringView view,size_t index)
{
    size_t bit=view.bit_offset+index*8,byte=bit/8,shift=bit%8;
    if(!shift) return view.data[byte];
    return (unsigned char)((view.data[byte]<<shift)|(view.data[byte+1]>>(8-shift)));
}
static Eterm integer_from_decimal_view(LbProcess *p,LbBitstringView view)
{
    LbAllocDomain *domain=p->entry_module->space->domain;
    size_t bytes,start=0,digits=0,bit_count,word_count,allocation_bytes;
    int negative=0; Eterm *words,result; void *memory=NULL; LbAllocStatus status;
    if((view.bit_size&7) || !view.bit_size) return fail(p,LB_EXC_BADARG);
    bytes=view.bit_size/8;
    if(!lb_work_domain_charge(domain,1+bytes+lb_work_bulk_units(bytes))) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    p->last_native_cost+=1+bytes+lb_work_bulk_units(bytes);
    unsigned char first=binary_view_byte(view,0);
    if(first=='-' || first=='+') { negative=first=='-'; start=1; }
    if(start==bytes) return fail(p,LB_EXC_BADARG);
    size_t first_significant=bytes;
    for(size_t i=start;i<bytes;++i) {
        unsigned char digit=binary_view_byte(view,i);
        if(digit<'0' || digit>'9') return fail(p,LB_EXC_BADARG);
        if(digit!='0' && first_significant==bytes) first_significant=i;
    }
    /* A zero-valued string succeeds even if its sign is negative. */
    if(first_significant==bytes) return make_small(0);
    start=first_significant; digits=bytes-start;
    /* 10^100000 < 2^332193, so this rational upper bound safely sizes the
     * native magnitude without floating-point rounding. */
    if(digits>(UINT64_MAX-99999u)/332193u) return fail(p,LB_EXC_SYSTEM_LIMIT);
    bit_count=(digits*332193u+99999u)/100000u;
    word_count=(bit_count+63u)/64u;
    if(!word_count) word_count=1;
    /* Capacity is an upper bound, not the normalized result's arity. Keep
     * one spare limb so valid values near the native ceiling are not denied
     * merely because their decimal digit count rounds the estimate upward. */
    if(word_count>LB_BIG_ARITY_MAX+1 || !lb_size_add(word_count,1,&allocation_bytes) ||
       !lb_size_mul(allocation_bytes,sizeof(Eterm),&allocation_bytes)) return fail(p,LB_EXC_SYSTEM_LIMIT);
    words=NULL;
    status=lb_alloc_domain_allocate(domain,allocation_bytes,&memory);
    if(status!=LB_ALLOC_OK) return allocation_failure(p,status);
    words=memory; memset(words,0,allocation_bytes); size_t used=1;
    for(size_t i=0;i<digits;++i) {
        if(!lb_work_domain_charge(domain,used+1)) {
            if(lb_alloc_domain_release(domain,memory)!=LB_ALLOC_OK) abort();
            p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE;
        }
        p->last_native_cost+=used+1;
        Uint carry=(Uint)(binary_view_byte(view,start+i)-'0');
        for(size_t j=0;j<used;++j) {
            __uint128_t product=(__uint128_t)words[j+1]*10+carry;
            words[j+1]=(Uint)product; carry=(Uint)(product>>64);
        }
        if(carry) {
            if(used==word_count) {
                if(lb_alloc_domain_release(domain,memory)!=LB_ALLOC_OK) abort();
                return fail(p,LB_EXC_SYSTEM_LIMIT);
            }
            words[++used]=carry;
        }
    }
    if(used>LB_BIG_ARITY_MAX) result=fail(p,LB_EXC_SYSTEM_LIMIT);
    else if(used==1 && (words[1]<=(Uint)MAX_SMALL || (negative && words[1]<=(Uint)MAX_SMALL+1))) {
        Sint small=negative?-(Sint)words[1]:(Sint)words[1]; result=make_small(small);
    } else {
        words[0]=_make_header((Uint)used,negative?NEG_BIG_SUBTAG:POS_BIG_SUBTAG);
        result=publish_words(p,words,used+1);
    }
    if(lb_alloc_domain_release(domain,memory)!=LB_ALLOC_OK) abort();
    return result;
}
Eterm lb_bif_binary_to_integer_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    LbBitstringView view; (void)pc; p->last_native_cost=0;
    if(!lb_bitstring_view(args[0],&view)) return fail(p,LB_EXC_BADARG);
    return integer_from_decimal_view(p,view);
}
Eterm lb_bif_list_to_integer_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    LbAllocDomain *domain=p->entry_module->space->domain;
    Eterm tail=args[0]; size_t count=0,significant=0; Uint magnitude=0;
    int negative=0,wide_character=0,any_digit=0; (void)pc; p->last_native_cost=0;
    _Static_assert(MAX_SMALL>=INT64_C(99999999999999999) && MAX_SMALL<INT64_C(999999999999999999),"native decimal fast-path bound");
    if(!is_list(tail)) return fail(p,LB_EXC_BADARG);
    while(is_list(tail)) {
        if(!lb_work_domain_charge(domain,1)) return allocation_failure(p,LB_ALLOC_WORK_LIMIT);
        ++p->last_native_cost;
        Eterm head=list_val(tail)[0];
        if(!is_small(head)) return fail(p,LB_EXC_BADARG);
        Sint character=signed_val(head); unsigned char byte=(unsigned char)(Uint)character;
        wide_character|=character<0 || character>255;
        if(!count && (character=='-' || character=='+')) negative=character=='-';
        else {
            if(byte<'0' || byte>'9') return fail(p,LB_EXC_BADARG);
            any_digit=1;
            if(byte!='0' || significant) {
                if(significant<18) ++significant;
                if(significant<=17) magnitude=magnitude*10+(byte-'0');
            }
        }
        if(count==SIZE_MAX/8) return fail(p,LB_EXC_SYSTEM_LIMIT);
        ++count; tail=list_val(tail)[1];
    }
    if(!is_nil(tail) || !any_digit) return fail(p,LB_EXC_BADARG);
    /* OTP's base-10 fast path casts each small head to byte and accepts at
     * most 17 significant digits. Its big fallback instead converts the
     * original whole list to binary, rejecting out-of-byte-range heads. */
    if(significant<=17) return make_small(negative?-(Sint)magnitude:(Sint)magnitude);
    if(wide_character) return fail(p,LB_EXC_BADARG);
    if(!lb_work_domain_charge(domain,count)) return allocation_failure(p,LB_ALLOC_WORK_LIMIT);
    p->last_native_cost+=count;
    unsigned char *bytes; LbAllocStatus status=lb_alloc_domain_allocate(domain,count,(void **)&bytes);
    if(status!=LB_ALLOC_OK) return allocation_failure(p,status);
    /* The list is immutable and the domain allocation cannot collect. The
     * parser's borrowed view then points at independent, accounted storage
     * through its final possible collection; no fabricated binary term. */
    tail=args[0];
    for(size_t i=0;i<count;++i) { bytes[i]=(unsigned char)signed_val(list_val(tail)[0]); tail=list_val(tail)[1]; }
    Eterm result=integer_from_decimal_view(p,(LbBitstringView){bytes,0,count*8});
    lb_release(domain,bytes); return result;
}
Eterm lb_bif_binary_to_float_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    LbAllocDomain *domain=p->entry_module->space->domain;
    LbBitstringView view; size_t bytes,buffer_size,work; char *buffer,*end;
    void *memory=NULL; LbAllocStatus status; fenv_t previous; locale_t previous_locale;
    double value; int parse_errno;
    Eterm words[2];
    (void)pc; p->last_native_cost=0;
    if(!lb_bitstring_view(args[0],&view) || (view.bit_size&7) || !view.bit_size) return fail(p,LB_EXC_BADARG);
    bytes=view.bit_size/8;
    if(bytes==SIZE_MAX || !lb_size_mul(bytes,2,&work) || !lb_size_add(work,1+lb_work_bulk_units(bytes),&work))
        return fail(p,LB_EXC_SYSTEM_LIMIT);
    if(!lb_work_domain_charge(domain,work)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    p->last_native_cost=work; buffer_size=bytes+1;
    status=lb_alloc_domain_allocate(domain,buffer_size,&memory);
    if(status!=LB_ALLOC_OK) return allocation_failure(p,status);
    buffer=memory;
    for(size_t i=0;i<bytes;++i) {
        unsigned char c=binary_view_byte(view,i);
        buffer[i]=(char)(c==','?'.':c);
    }
    buffer[bytes]='\0';
    size_t i=0;
    if(buffer[i]=='+' || buffer[i]=='-') ++i;
    size_t leading=i;
    while(i<bytes && buffer[i]>='0' && buffer[i]<='9') ++i;
    if(i==leading || i==bytes || buffer[i]!='.') goto badarg;
    ++i; size_t fraction=i;
    while(i<bytes && buffer[i]>='0' && buffer[i]<='9') ++i;
    if(i==fraction) goto badarg;
    if(i<bytes && (buffer[i]=='e' || buffer[i]=='E')) {
        ++i;
        if(i<bytes && (buffer[i]=='+' || buffer[i]=='-')) ++i;
        size_t exponent=i;
        while(i<bytes && buffer[i]>='0' && buffer[i]<='9') ++i;
        if(i==exponent) goto badarg;
    }
    if(i!=bytes) goto badarg;
    if(!p->entry_module->space->engine->numeric_locale || fegetenv(&previous)) goto infrastructure_error;
    if(fesetenv(FE_DFL_ENV)) { (void)fesetenv(&previous); goto infrastructure_error; }
    previous_locale=uselocale((locale_t)p->entry_module->space->engine->numeric_locale);
    if(previous_locale==(locale_t)0) { (void)fesetenv(&previous); goto infrastructure_error; }
    errno=0; value=strtod(buffer,&end); parse_errno=errno;
    if(uselocale(previous_locale)==(locale_t)0) { (void)fesetenv(&previous); goto infrastructure_error; }
    if(fesetenv(&previous)) goto infrastructure_error;
    if(end!=buffer+bytes || !isfinite(value) ||
       (parse_errno==ERANGE && (value==HUGE_VAL || value==-HUGE_VAL))) goto badarg;
    words[0]=_make_header(1,FLOAT_SUBTAG); memcpy(words+1,&value,sizeof(value));
    Eterm result=publish_words(p,words,2);
    if(lb_alloc_domain_release(domain,memory)!=LB_ALLOC_OK) abort();
    return result;
badarg:
    if(lb_alloc_domain_release(domain,memory)!=LB_ALLOC_OK) abort();
    return fail(p,LB_EXC_BADARG);
infrastructure_error:
    if(lb_alloc_domain_release(domain,memory)!=LB_ALLOC_OK) abort();
    p->status=LB_PROCESS_EXCEPTION; return fail(p,LB_EXC_NOTSUP);
}
static Eterm binary_to_atom(LbProcess *p,Eterm *args,int existing)
{
    LbBitstringView view; LbAtomTable *atoms=p->entry_module->space->atoms;
    unsigned char encoded[4*255]; size_t bytes,out_size=0; int latin1;
    Eterm atom=THE_NON_VALUE; LbAtomStatus status;
    if(!lb_bitstring_view(args[0],&view) || (view.bit_size&7)) return fail(p,LB_EXC_BADARG);
    if(args[1]!=am_utf8 && args[1]!=am_unicode && args[1]!=am_latin1) return fail(p,LB_EXC_BADARG);
    latin1=args[1]==am_latin1; bytes=view.bit_size/8;
    if(bytes>(latin1?255u:4u*255u)) return fail(p,LB_EXC_BADARG);
    uint64_t work=1+(uint64_t)bytes*(latin1?2u:1u)+lb_work_bulk_units(bytes);
    if(!lb_work_domain_charge(p->entry_module->space->domain,work)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    p->last_native_cost+=work;
    for(size_t i=0;i<bytes;++i) {
        unsigned char c=binary_view_byte(view,i);
        if(!latin1 || c<0x80) encoded[out_size++]=c;
        else { encoded[out_size++]=(unsigned char)(0xc0|(c>>6)); encoded[out_size++]=(unsigned char)(0x80|(c&0x3f)); }
    }
    if(existing) status=lb_atoms_find(atoms,encoded,out_size,&atom);
    else status=lb_atoms_intern(atoms,encoded,out_size,&atom);
    if(status==LB_ATOM_OK) return atom;
    if(status==LB_ATOM_LIMIT) return fail(p,LB_EXC_SYSTEM_LIMIT);
    if(status==LB_ATOM_NO_MEMORY) return allocation_failure(p,LB_ALLOC_NO_MEMORY);
    return fail(p,LB_EXC_BADARG);
}
/* Only committed namespace names reach this decoder: atom admission already
 * proves canonical scalar UTF-8 and at most 255 characters, including NUL.
 * Namespace-owned name storage is immutable and outside the moving heap. */
static Uint atom_name_scalar(LbBeamBytes name,size_t *position)
{
    assert(*position<name.size);
    unsigned first=name.data[(*position)++],following; Uint code;
    if(first<0x80) return first;
    assert(first>=0xc2 && first<=0xf4);
    if(first<0xe0) { following=1; code=first&0x1f; }
    else if(first<0xf0) { following=2; code=first&0x0f; }
    else { following=3; code=first&0x07; }
    assert(following<=name.size-*position);
    while(following--) code=(code<<6)|(name.data[(*position)++]&0x3f);
    return code;
}
Eterm lb_bif_atom_to_list_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    LbAllocDomain *domain=p->entry_module->space->domain; LbBeamBytes name;
    (void)pc; p->last_native_cost=0;
    if(!lb_work_domain_charge(domain,1)) return allocation_failure(p,LB_ALLOC_WORK_LIMIT);
    p->last_native_cost=1;
    if(!is_atom(args[0]) || lb_atoms_name(p->entry_module->space->atoms,args[0],&name)!=LB_ATOM_OK)
        return fail(p,LB_EXC_BADARG);
    if(!name.size) return NIL;
    /* Bound two decoding passes and cons construction by encoded byte count.
     * No guest heap pointer or dynamically allocated scratch crosses GC. */
    uint64_t work=3*(uint64_t)name.size+lb_work_bulk_units(2*name.size);
    if(!lb_work_domain_charge(domain,work)) return allocation_failure(p,LB_ALLOC_WORK_LIMIT);
    p->last_native_cost+=work;
    size_t position=0,count=0;
    while(position<name.size) { (void)atom_name_scalar(name,&position); ++count; }
    size_t words,need,collections=p->collections;
    if(!lb_size_mul(count,2,&words) || !lb_size_add(words,p->heap_reserved,&need)) return fail(p,LB_EXC_SYSTEM_LIMIT);
    if(!lb_heap_reserve(p,need,MAX_REG)) return allocation_failure(p,lb_alloc_domain_failure(domain));
    if(p->collections!=collections) p->last_native_cost+=p->last_gc_cost;
    Eterm *first=p->htop,*cell=first; position=0;
    for(size_t i=0;i<count;++i,cell+=2) {
        cell[0]=make_small((Sint)atom_name_scalar(name,&position));
        cell[1]=i+1<count?make_list(cell+2):NIL;
    }
    p->htop=cell; return make_list(first);
}
static Eterm atom_to_binary(LbProcess *p,Eterm atom,Eterm encoding)
{
    LbAtomTable *atoms=p->entry_module->space->atoms; LbBeamBytes name;
    LbAllocDomain *domain=p->entry_module->space->domain;
    unsigned char latin1[4*255]; const unsigned char *data; size_t size,bits,words,need,work;
    LbAtomStatus atom_status; LbAllocStatus alloc_status; Eterm result;
    if(!is_atom(atom) || (encoding!=am_utf8 && encoding!=am_unicode && encoding!=am_latin1))
        return fail(p,LB_EXC_BADARG);
    atom_status=lb_atoms_name(atoms,atom,&name);
    if(atom_status!=LB_ATOM_OK) return fail(p,LB_EXC_BADARG);
    if(!lb_size_mul(name.size,encoding==am_latin1?2:1,&work) ||
       !lb_size_add(work,1+lb_work_bulk_units(name.size),&work)) return fail(p,LB_EXC_SYSTEM_LIMIT);
    if(!lb_work_domain_charge(domain,work)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    p->last_native_cost+=work;
    data=name.data; size=name.size;
    if(encoding==am_latin1) {
        size_t pos=0,out=0;
        while(pos<name.size) {
            Uint code=atom_name_scalar(name,&pos);
            if(code>255) return fail(p,LB_EXC_BADARG);
            latin1[out++]=(unsigned char)code;
        }
        data=latin1; size=out;
    }
    if(!lb_size_mul(size,8,&bits) || !(words=lb_bitstring_heap_words(bits)) ||
       !lb_size_add(words,p->heap_reserved,&need)) return fail(p,LB_EXC_SYSTEM_LIMIT);
    size_t collections=p->collections;
    if(!lb_heap_reserve(p,need,MAX_REG)) return allocation_failure(p,lb_alloc_domain_failure(domain));
    if(p->collections!=collections) p->last_native_cost+=p->last_gc_cost;
    alloc_status=lb_bitstring_build(domain,&p->off_heap,&p->htop,data,bits,0,&result);
    if(alloc_status!=LB_ALLOC_OK) return allocation_failure(p,alloc_status);
    return result;
}
Eterm lb_bif_atom_to_binary_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{ (void)pc; p->last_native_cost=0; return atom_to_binary(p,args[0],am_utf8); }
Eterm lb_bif_atom_to_binary_2(LbProcess *p,Eterm *args,const BeamInstr *pc)
{ (void)pc; p->last_native_cost=0; return atom_to_binary(p,args[0],args[1]); }
static int float_short_text(double value,char *output,size_t capacity)
{
    char raw[64],digits[32],fixed[512],scientific[64]; int raw_size=-1,sign=0,exponent=0;
    for(int precision=1;precision<=17;++precision) {
        int size=snprintf(raw,sizeof(raw),"%.*g",precision,value);
        if(size<0 || (size_t)size>=sizeof(raw)) return -1;
        char *end; double parsed=strtod(raw,&end);
        if(end==raw+size && memcmp(&parsed,&value,sizeof(value))==0) { raw_size=size; break; }
    }
    if(raw_size<0) return -1;
    size_t pos=0;
    if(raw[pos]=='-' || raw[pos]=='+') { sign=raw[pos]=='-'; ++pos; }
    size_t digit_count=0,decimal_position=0; int saw_point=0;
    for(;pos<(size_t)raw_size && raw[pos]!='e' && raw[pos]!='E';++pos) {
        if(raw[pos]=='.') { saw_point=1; decimal_position=digit_count; continue; }
        if(raw[pos]<'0' || raw[pos]>'9' || digit_count==sizeof(digits)) return -1;
        digits[digit_count++]=raw[pos];
    }
    if(!saw_point) decimal_position=digit_count;
    if(pos<(size_t)raw_size) {
        ++pos; int exponent_sign=1;
        if(raw[pos]=='-' || raw[pos]=='+') { if(raw[pos++]=='-') exponent_sign=-1; }
        if(pos==(size_t)raw_size) return -1;
        for(;pos<(size_t)raw_size;++pos) {
            if(raw[pos]<'0' || raw[pos]>'9') return -1;
            exponent=exponent*10+(raw[pos]-'0');
        }
        exponent*=exponent_sign;
    }
    if(!digit_count) return -1;
    int point=(int)decimal_position+exponent;
    size_t f=0;
    if(sign) fixed[f++]='-';
    if(point<=0) {
        fixed[f++]='0'; fixed[f++]='.';
        for(int i=0;i< -point;++i) fixed[f++]='0';
        for(size_t i=0;i<digit_count;++i) fixed[f++]=digits[i];
    } else if((size_t)point>=digit_count) {
        for(size_t i=0;i<digit_count;++i) fixed[f++]=digits[i];
        for(size_t i=digit_count;i<(size_t)point;++i) fixed[f++]='0';
        fixed[f++]='.'; fixed[f++]='0';
    } else {
        for(int i=0;i<point;++i) fixed[f++]=digits[i];
        fixed[f++]='.';
        for(size_t i=(size_t)point;i<digit_count;++i) fixed[f++]=digits[i];
    }
    size_t s=0;
    if(sign) scientific[s++]='-';
    scientific[s++]=digits[0]; scientific[s++]='.';
    if(digit_count==1) scientific[s++]='0';
    else for(size_t i=1;i<digit_count;++i) scientific[s++]=digits[i];
    scientific[s++]='e';
    int scientific_exponent=point-1;
    if(scientific_exponent<0) { scientific[s++]='-'; scientific_exponent=-scientific_exponent; }
    char exponent_digits[8]; size_t exponent_count=0;
    do { exponent_digits[exponent_count++]=(char)('0'+scientific_exponent%10); scientific_exponent/=10; }
    while(scientific_exponent);
    while(exponent_count) scientific[s++]=exponent_digits[--exponent_count];
    const char *selected; size_t selected_size;
    if(s<f) { selected=scientific; selected_size=s; }
    else { selected=fixed; selected_size=f; }
    if(selected_size>=capacity) return -1;
    memcpy(output,selected,selected_size); output[selected_size]='\0'; return (int)selected_size;
}
Eterm lb_bif_float_to_binary_2(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    LbAllocDomain *domain=p->entry_module->space->domain;
    char output[256]; size_t options=0,output_size,bits,words,need;
    Eterm opts=args[1]; int mode=0,compact=0,decimals=20; double value; fenv_t previous;
    void *locale_saved; (void)pc; p->last_native_cost=0;
    if(!is_float_term(args[0])) return fail(p,LB_EXC_BADARG);
    memcpy(&value,boxed_val(args[0])+1,sizeof(value));
    while(is_list(opts)) {
        Eterm option=CAR(list_val(opts));
        if(!lb_work_domain_charge(domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
        ++options; ++p->last_native_cost;
        if(option==am_compact) compact=1;
        else if(option==am_short) mode=1;
        else if(is_tuple(option)) {
            Eterm *tuple=tuple_val(option);
            if(arityval(tuple[0])!=2 || !is_small(tuple[2])) return fail(p,LB_EXC_BADARG);
            Sint number=signed_val(tuple[2]);
            if(number<INT_MIN || number>255) return fail(p,LB_EXC_BADARG);
            if(tuple[1]==am_decimals) { mode=2; decimals=(int)number; }
            else if(tuple[1]==am_scientific) { mode=3; decimals=(int)number; }
            else return fail(p,LB_EXC_BADARG);
        } else return fail(p,LB_EXC_BADARG);
        opts=CDR(list_val(opts));
    }
    if(!is_nil(opts)) return fail(p,LB_EXC_BADARG);
    if(mode==2 && decimals<0) return fail(p,LB_EXC_BADARG);
    if(!lb_work_domain_charge(domain,1+options+((mode==1)?17*256:256))) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    p->last_native_cost+=1+options+((mode==1)?17*256:256);
    if(fegetenv(&previous)) goto float_infrastructure_error;
    if(fesetenv(FE_DFL_ENV)) { (void)fesetenv(&previous); goto float_infrastructure_error; }
    locale_saved=uselocale((locale_t)p->entry_module->space->engine->numeric_locale);
    if(locale_saved==(void *)0) { (void)fesetenv(&previous); goto float_infrastructure_error; }
    if(mode==1) { int length=float_short_text(value,output,sizeof(output)); output_size=length>0?(size_t)length:0; }
    else {
        int n;
        if(mode==2) n=snprintf(output,sizeof(output),"%.*f",decimals,value);
        else if(mode==3 || mode==0) {
            if(decimals<0) n=snprintf(output,sizeof(output),"%e",value);
            else n=snprintf(output,sizeof(output),"%.*e",decimals,value);
        } else n=-1;
        if(n<0 || (size_t)n>=sizeof(output)) output_size=0;
        else {
            output_size=(size_t)n;
            if(mode==2 && compact && decimals>0) {
                while(output_size && output[output_size-1]=='0') --output_size;
                if(output_size && output[output_size-1]=='.') --output_size;
                output[output_size]='\0';
            }
        }
    }
    if(uselocale((locale_t)locale_saved)==(locale_t)0) { (void)fesetenv(&previous); goto float_infrastructure_error; }
    if(fesetenv(&previous)) goto float_infrastructure_error;
    if(!output_size) return fail(p,LB_EXC_BADARG);
    if(!lb_size_mul(output_size,8,&bits) || !(words=lb_bitstring_heap_words(bits)) ||
       !lb_size_add(words,p->heap_reserved,&need)) return fail(p,LB_EXC_SYSTEM_LIMIT);
    size_t collections=p->collections;
    if(!lb_heap_reserve(p,need,MAX_REG)) return allocation_failure(p,lb_alloc_domain_failure(domain));
    if(p->collections!=collections) p->last_native_cost+=p->last_gc_cost;
    Eterm result; LbAllocStatus status=lb_bitstring_build(domain,&p->off_heap,&p->htop,(const unsigned char *)output,bits,0,&result);
    if(status!=LB_ALLOC_OK) return allocation_failure(p,status);
    return result;
float_infrastructure_error:
    p->status=LB_PROCESS_EXCEPTION; return fail(p,LB_EXC_NOTSUP);
}
Eterm lb_bif_integer_to_binary_2(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    LbAllocDomain *domain=p->entry_module->space->domain;
    Eterm value=args[0],small[2],*digits; Uint base; size_t count,digit_bytes,output_capacity,total_bytes;
    unsigned char *output; void *memory=NULL; LbAllocStatus status; int negative; size_t output_digits=0;
    (void)pc; p->last_native_cost=0;
    if(!(is_small(value)||lb_is_big(value)) || !is_small(args[1])) return fail(p,LB_EXC_BADARG);
    Sint signed_base=signed_val(args[1]);
    if(signed_base<2 || signed_base>36) return fail(p,LB_EXC_BADARG);
    base=(Uint)signed_base; negative=is_small(value)?signed_val(value)<0:big_is_negative(value);
    if(is_small(value)) value=lb_small_to_big(signed_val(value),small);
    count=lb_big_size(value);
    if(!lb_size_mul(count,sizeof(Eterm),&digit_bytes) || !lb_size_mul(count,64,&output_capacity) ||
       !lb_size_add(output_capacity,2,&output_capacity) || !lb_size_add(digit_bytes,output_capacity,&total_bytes))
        return fail(p,LB_EXC_SYSTEM_LIMIT);
    if(!lb_work_domain_charge(domain,1+count)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    p->last_native_cost=1+count;
    status=lb_alloc_domain_allocate(domain,total_bytes,&memory);
    if(status!=LB_ALLOC_OK) return allocation_failure(p,status);
    digits=memory; output=(unsigned char *)memory+digit_bytes;
    for(size_t i=0;i<count;++i) digits[i]=boxed_val(value)[i+1];
    if(count==1 && digits[0]==0) output[output_digits++]='0';
    else {
        for(;;) {
            if(!lb_work_domain_charge(domain,count)) { status=LB_ALLOC_WORK_LIMIT; goto work_failed; }
            p->last_native_cost+=count;
            Uint remainder=0;
            for(size_t i=count;i;--i) {
                __uint128_t dividend=((__uint128_t)remainder<<64)|digits[i-1];
                digits[i-1]=(Uint)(dividend/base); remainder=(Uint)(dividend%base);
            }
            if(output_digits>=output_capacity) { status=LB_ALLOC_LIMIT; goto allocation_failed; }
            output[output_digits++]=(unsigned char)(remainder<10?'0'+remainder:'A'+remainder-10);
            while(count>1 && digits[count-1]==0) --count;
            if(count==1 && digits[0]==0) break;
        }
    }
    if(!lb_work_domain_charge(domain,output_digits+(negative?output_digits:0))) { status=LB_ALLOC_WORK_LIMIT; goto work_failed; }
    p->last_native_cost+=output_digits+(negative?output_digits:0);
    for(size_t i=0;i<output_digits/2;++i) {
        unsigned char c=output[i]; output[i]=output[output_digits-i-1]; output[output_digits-i-1]=c;
    }
    size_t result_size=output_digits;
    if(negative) { memmove(output+1,output,output_digits); output[0]='-'; ++result_size; }
    size_t bits,heap_words,need,collections=p->collections;
    if(!lb_size_mul(result_size,8,&bits) || !(heap_words=lb_bitstring_heap_words(bits)) ||
       !lb_size_add(heap_words,p->heap_reserved,&need)) { status=LB_ALLOC_LIMIT; goto allocation_failed; }
    if(!lb_heap_reserve(p,need,MAX_REG)) { status=lb_alloc_domain_failure(domain); goto allocation_failed; }
    if(p->collections!=collections) p->last_native_cost+=p->last_gc_cost;
    Eterm result; status=lb_bitstring_build(domain,&p->off_heap,&p->htop,output,bits,0,&result);
    if(status!=LB_ALLOC_OK) goto allocation_failed;
    if(lb_alloc_domain_release(domain,memory)!=LB_ALLOC_OK) abort();
    return result;
work_failed:
    if(lb_alloc_domain_release(domain,memory)!=LB_ALLOC_OK) abort();
    p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE;
allocation_failed:
    if(lb_alloc_domain_release(domain,memory)!=LB_ALLOC_OK) abort();
    return allocation_failure(p,status);
}
Eterm lb_bif_binary_to_atom_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{ Eterm encoded[2]={args[0],am_utf8}; (void)pc; p->last_native_cost=0; return binary_to_atom(p,encoded,0); }
Eterm lb_bif_binary_to_existing_atom_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{ Eterm encoded[2]={args[0],am_utf8}; (void)pc; p->last_native_cost=0; return binary_to_atom(p,encoded,1); }
Eterm lb_bif_binary_to_atom_2(LbProcess *p,Eterm *args,const BeamInstr *pc)
{ (void)pc; p->last_native_cost=0; return binary_to_atom(p,args,0); }
Eterm lb_bif_binary_to_existing_atom_2(LbProcess *p,Eterm *args,const BeamInstr *pc)
{ (void)pc; p->last_native_cost=0; return binary_to_atom(p,args,1); }
static Eterm exact_equal(LbProcess *p,Eterm left,Eterm right,int negate)
{
    LbAllocDomain *domain=p->entry_module->space->domain; int equal=0;
    if(!lb_work_domain_charge(domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    p->last_native_cost=1;
    LbAllocStatus status=lb_term_equal(domain,left,right,&equal);
    if(status!=LB_ALLOC_OK) {
        LbAllocStatus failure=lb_alloc_domain_failure(domain);
        if(failure==LB_ALLOC_OK) failure=status;
        return allocation_failure(p,failure);
    }
    return (equal!=negate)?am_true:am_false;
}
Eterm lb_bif_seqeq_2(LbProcess *p,Eterm *args,const BeamInstr *pc)
{ (void)pc; p->last_native_cost=0; return exact_equal(p,args[0],args[1],0); }
Eterm lb_bif_sneqeq_2(LbProcess *p,Eterm *args,const BeamInstr *pc)
{ (void)pc; p->last_native_cost=0; return exact_equal(p,args[0],args[1],1); }
static int proper_list_count(LbProcess *,Eterm,size_t *);
static int compare_integer_terms(LbProcess *p,Eterm left,Eterm right,int *order)
{
    Eterm left_words[2],right_words[2];
    if(is_small(left)) left=lb_small_to_big(signed_val(left),left_words);
    if(is_small(right)) right=lb_small_to_big(signed_val(right),right_words);
    int left_negative=big_is_negative(left),right_negative=big_is_negative(right);
    if(left_negative!=right_negative) { *order=left_negative?-1:1; return 1; }
    size_t left_size=lb_big_size(left),right_size=lb_big_size(right);
    if(left_size!=right_size) {
        *order=(left_size<right_size?-1:1)*(left_negative?-1:1); return 1;
    }
    if(!lb_work_domain_charge(p->entry_module->space->domain,left_size)) {
        p->status=LB_PROCESS_WORK_LIMIT; return 0;
    }
    p->last_native_cost+=left_size;
    const Eterm *a=boxed_val(left),*b=boxed_val(right);
    while(left_size) {
        --left_size;
        if(a[left_size+1]!=b[left_size+1]) {
            *order=(a[left_size+1]<b[left_size+1]?-1:1)*(left_negative?-1:1); return 1;
        }
    }
    *order=0; return 1;
}
static int integer_exact_double(LbProcess *p,Eterm value,double *output)
{
    if(is_small(value)) {
        Sint integer=signed_val(value); double number=(double)integer;
        if((Sint)number!=integer) return 0;
        *output=number; return 1;
    }
    size_t words=lb_big_size(value); const Eterm *digits=boxed_val(value);
    if(!lb_work_domain_charge(p->entry_module->space->domain,words)) {
        p->status=LB_PROCESS_WORK_LIMIT; return -1;
    }
    p->last_native_cost+=words;
    Uint top=digits[words]; size_t high=0;
    while(top) { ++high; top>>=1; }
    size_t bits=(words-1)*64+high;
    if(bits>1024) return 2;
    if(bits<=53) return 0; /* noncanonical large integer, fail closed */
    size_t discarded=bits-53,whole=discarded/64,rest=discarded%64;
    for(size_t i=0;i<whole;++i) if(digits[i+1]) return 0;
    if(rest && (digits[whole+1]&(((Uint)1<<rest)-1))) return 0;
    return lb_big_to_double(value,output)==0?1:0;
}
static Eterm relaxed_equal(LbProcess *p,Eterm left,Eterm right,int negate)
{
    LbAllocDomain *domain=p->entry_module->space->domain; int equal=0;
    if(!lb_work_domain_charge(domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    p->last_native_cost=1;
    if(is_float_term(left) && is_float_term(right)) {
        double a,b; memcpy(&a,boxed_val(left)+1,sizeof(a)); memcpy(&b,boxed_val(right)+1,sizeof(b));
        equal=a==b;
    } else if((is_small(left)||lb_is_big(left)) && is_float_term(right)) {
        double integer,floating; int exact=integer_exact_double(p,left,&integer);
        if(exact<0) return THE_NON_VALUE;
        memcpy(&floating,boxed_val(right)+1,sizeof(floating)); equal=exact==1 && integer==floating;
    } else if(is_float_term(left) && (is_small(right)||lb_is_big(right))) {
        double integer,floating; int exact=integer_exact_double(p,right,&integer);
        if(exact<0) return THE_NON_VALUE;
        memcpy(&floating,boxed_val(left)+1,sizeof(floating)); equal=exact==1 && integer==floating;
    } else {
        LbAllocStatus status=lb_term_equal(domain,left,right,&equal);
        if(status!=LB_ALLOC_OK) {
            LbAllocStatus failure=lb_alloc_domain_failure(domain);
            if(failure==LB_ALLOC_OK) failure=status;
            return allocation_failure(p,failure);
        }
    }
    return (equal!=negate)?am_true:am_false;
}
Eterm lb_bif_seq_2(LbProcess *p,Eterm *args,const BeamInstr *pc)
{ (void)pc; p->last_native_cost=0; return relaxed_equal(p,args[0],args[1],0); }
Eterm lb_bif_sneq_2(LbProcess *p,Eterm *args,const BeamInstr *pc)
{ (void)pc; p->last_native_cost=0; return relaxed_equal(p,args[0],args[1],1); }
static int term_order_rank(Eterm term)
{
    if(is_small(term)||lb_is_big(term)||is_float_term(term)) return 0;
    if(is_atom(term)) return 1;
    if(is_boxed(term)) {
        Uint tag=*boxed_val(term)&_TAG_HEADER_MASK;
        if(tag==REF_SUBTAG || tag==EXTERNAL_REF_SUBTAG) return 2;
        if(tag==FUN_SUBTAG) return 3;
        if(tag==EXTERNAL_PORT_SUBTAG) return 4;
        if(tag==EXTERNAL_PID_SUBTAG) return 5;
        if(tag==ARITYVAL_SUBTAG) return 6;
        if(tag==MAP_SUBTAG) return 7;
        if(tag==HEAP_BITS_SUBTAG || tag==SUB_BITS_SUBTAG) return 10;
    }
    if(is_internal_pid(term)) return 5;
    if(is_nil(term)) return 8;
    if(is_list(term)) return 9;
    return -1;
}
static int compare_ordered_terms(LbProcess *p,Eterm left,Eterm right,int *order)
{
    LbAllocDomain *domain=p->entry_module->space->domain; int equal=0;
    LbAllocStatus status;
    if(!lb_work_domain_charge(domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return 0; }
    p->last_native_cost=1;
    status=lb_term_equal(domain,left,right,&equal);
    if(status!=LB_ALLOC_OK) {
        LbAllocStatus failure=lb_alloc_domain_failure(domain);
        if(failure==LB_ALLOC_OK) failure=status;
        (void)allocation_failure(p,failure); return 0;
    }
    if(equal) { *order=0; return 1; }
    int left_rank=term_order_rank(left),right_rank=term_order_rank(right);
    if(left_rank<0 || right_rank<0) goto unsupported;
    if(left_rank!=right_rank) { *order=left_rank<right_rank?-1:1; return 1; }
    if(left_rank==0) {
        int left_integer=is_small(left)||lb_is_big(left),right_integer=is_small(right)||lb_is_big(right);
        if(left_integer && right_integer) return compare_integer_terms(p,left,right,order);
        if(is_float_term(left) && is_float_term(right)) {
            double a,b; memcpy(&a,boxed_val(left)+1,sizeof(a)); memcpy(&b,boxed_val(right)+1,sizeof(b));
            *order=a<b?-1:a>b?1:0; return 1;
        }
        if(left_integer && is_float_term(right)) {
            double integer; int exact=integer_exact_double(p,left,&integer);
            double floating; memcpy(&floating,boxed_val(right)+1,sizeof(floating));
            if(exact<0) return 0;
            if(exact==2) { *order=big_is_negative(left)?-1:1; return 1; }
            if(exact!=1) goto unsupported;
            *order=integer<floating?-1:integer>floating?1:0; return 1;
        }
        if(is_float_term(left) && right_integer) {
            double integer; int exact=integer_exact_double(p,right,&integer);
            double floating; memcpy(&floating,boxed_val(left)+1,sizeof(floating));
            if(exact<0) return 0;
            if(exact==2) { *order=big_is_negative(right)?1:-1; return 1; }
            if(exact!=1) goto unsupported;
            *order=floating<integer?-1:floating>integer?1:0; return 1;
        }
    }
    if(left_rank==1) {
        LbBeamBytes a,b;
        if(lb_atoms_name(p->entry_module->space->atoms,left,&a)!=LB_ATOM_OK ||
           lb_atoms_name(p->entry_module->space->atoms,right,&b)!=LB_ATOM_OK) goto unsupported;
        size_t compared=a.size<b.size?a.size:b.size;
        if(!lb_work_domain_charge(domain,lb_work_bulk_units(compared))) { p->status=LB_PROCESS_WORK_LIMIT; return 0; }
        p->last_native_cost+=lb_work_bulk_units(compared);
        int cmp=compared?memcmp(a.data,b.data,compared):0;
        *order=cmp<0?-1:cmp>0?1:a.size<b.size?-1:a.size>b.size?1:0; return 1;
    }
unsupported:
    p->status=LB_PROCESS_REFUSED; return 0;
}
Eterm lb_bif_max_2(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    int order; (void)pc; p->last_native_cost=0;
    if(!compare_ordered_terms(p,args[0],args[1],&order)) return THE_NON_VALUE;
    /* Native max_2 uses CMP_LT: numerically equal values retain the first
     * operand, including its integer/float representation. No allocation. */
    return order<0?args[1]:args[0];
}
Eterm lb_bif_is_binary_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)pc; p->last_native_cost=0;
    if(!lb_work_domain_charge(p->entry_module->space->domain,1)) {
        p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE;
    }
    p->last_native_cost=1;
    return lb_is_binary(args[0])?am_true:am_false;
}
#define LB_TYPE_PREDICATE(name, predicate) \
    Eterm lb_bif_##name##_1(LbProcess *p,Eterm *args,const BeamInstr *pc) { \
        (void)pc; p->last_native_cost=1; \
        if(!lb_work_domain_charge(p->entry_module->space->domain,1)) \
            return allocation_failure(p,LB_ALLOC_WORK_LIMIT); \
        Eterm value=args[0]; return (predicate)?am_true:am_false; \
    }
#include "type_predicates.def"
#undef LB_TYPE_PREDICATE

/* bif.c:tuple_size_1. The admitted native arity fits a small without allocation. */
Eterm lb_bif_tuple_size_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    _Static_assert(MAX_ARITYVAL<=(Uint)MAX_SMALL,"native tuple arity fits a small");
    (void)pc; p->last_native_cost=1;
    if(!lb_work_domain_charge(p->entry_module->space->domain,1)) return allocation_failure(p,LB_ALLOC_WORK_LIMIT);
    if(!is_tuple(args[0])) return fail(p,LB_EXC_BADARG);
    return make_small(arityval(*tuple_val(args[0])));
}
Eterm lb_bif_is_atom_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)pc; p->last_native_cost=0;
    if(!lb_work_domain_charge(p->entry_module->space->domain,1)) {
        p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE;
    }
    p->last_native_cost=1;
    return is_atom(args[0])?am_true:am_false;
}
Eterm lb_bif_not_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)pc; p->last_native_cost=0;
    if(!lb_work_domain_charge(p->entry_module->space->domain,1)) {
        p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE;
    }
    p->last_native_cost=1;
    if(args[0]==am_true) return am_false;
    if(args[0]==am_false) return am_true;
    return fail(p,LB_EXC_BADARG);
}
Eterm lb_bif_is_list_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)pc; p->last_native_cost=0;
    if(!lb_work_domain_charge(p->entry_module->space->domain,1)) {
        p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE;
    }
    p->last_native_cost=1;
    return is_list(args[0]) || is_nil(args[0])?am_true:am_false;
}
Eterm lb_bif_is_integer_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)pc; p->last_native_cost=0;
    if(!lb_work_domain_charge(p->entry_module->space->domain,1)) {
        p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE;
    }
    p->last_native_cost=1;
    return lb_is_integer(args[0])?am_true:am_false;
}
Eterm lb_bif_min_2(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    int order; (void)pc; p->last_native_cost=0;
    if(!compare_ordered_terms(p,args[0],args[1],&order)) return THE_NON_VALUE;
    return order>0?args[1]:args[0];
}
static Eterm compare_bif(LbProcess *p,Eterm left,Eterm right,unsigned operation)
{
    int order;
    if(!compare_ordered_terms(p,left,right,&order)) return THE_NON_VALUE;
    int result=operation==0?order>=0:operation==1?order>0:operation==2?order<=0:order<0;
    return result?am_true:am_false;
}
Eterm lb_bif_sge_2(LbProcess *p,Eterm *args,const BeamInstr *pc)
{ (void)pc; p->last_native_cost=0; return compare_bif(p,args[0],args[1],0); }
Eterm lb_bif_sgt_2(LbProcess *p,Eterm *args,const BeamInstr *pc)
{ (void)pc; p->last_native_cost=0; return compare_bif(p,args[0],args[1],1); }
Eterm lb_bif_sle_2(LbProcess *p,Eterm *args,const BeamInstr *pc)
{ (void)pc; p->last_native_cost=0; return compare_bif(p,args[0],args[1],2); }
Eterm lb_bif_slt_2(LbProcess *p,Eterm *args,const BeamInstr *pc)
{ (void)pc; p->last_native_cost=0; return compare_bif(p,args[0],args[1],3); }
typedef enum { LIST_HEAD=0, LIST_TAIL=1 } ListPart;
static Eterm list_part(LbProcess *p,Eterm value,ListPart part)
{
    p->last_native_cost=0;
    if(!lb_work_domain_charge(p->entry_module->space->domain,1)) {
        p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE;
    }
    p->last_native_cost=1;
    if(!is_list(value)) return fail(p,LB_EXC_BADARG);
    /* Only the cons is required. An improper tail is still a valid result. */
    return list_val(value)[part];
}
Eterm lb_bif_hd_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{ (void)pc; return list_part(p,args[0],LIST_HEAD); }
Eterm lb_bif_tl_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{ (void)pc; return list_part(p,args[0],LIST_TAIL); }
Eterm lb_bif_length_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    size_t count=0; (void)pc; p->last_native_cost=0;
    if(!lb_work_domain_charge(p->entry_module->space->domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    ++p->last_native_cost;
    int counted=proper_list_count(p,args[0],&count);
    if(counted<0) return THE_NON_VALUE;
    if(counted==2) return fail(p,LB_EXC_SYSTEM_LIMIT);
    if(!counted || count>(size_t)MAX_SMALL) return fail(p,counted?LB_EXC_SYSTEM_LIMIT:LB_EXC_BADARG);
    return make_small((Sint)count);
}
/* Native erl_bif_guard.c double_to_integer, using independent bounded storage
 * before GC. Preserve the strict native small thresholds, including the boxed
 * negative MIN_SMALL boundary; do not silently normalize that representation. */
static Eterm integer_from_integral_double(double x,Eterm words[17])
{
    if(x<(double)(MAX_SMALL+1) && x>(double)(MIN_SMALL-1)) return make_small((Sint)x);
    int negative=x<0; if(negative) x=-x;
    size_t digits=0; const double base=0x1p64;
    while(x>=1.0) { if(digits==16) return THE_NON_VALUE; x/=base; ++digits; }
    if(!digits) return THE_NON_VALUE;
    for(size_t i=digits;i>0;--i) { x*=base; Uint digit=(Uint)x; words[i]=digit; x-=(double)digit; }
    words[0]=_make_header(digits,negative?NEG_BIG_SUBTAG:POS_BIG_SUBTAG);
    return make_boxed(words);
}
Eterm lb_bif_floor_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)pc; p->last_native_cost=1; Eterm value=args[0];
    LbAllocDomain *domain=p->entry_module->space->domain;
    if(!lb_work_domain_charge(domain,1)) return allocation_failure(p,LB_ALLOC_WORK_LIMIT);
    if(lb_is_integer(value)) return value;
    if(!is_float_term(value)) return fail(p,LB_EXC_BADARG);
    /* Binary64 has at most sixteen native 64-bit integer digits. Precharge
     * both scaling passes and publication; C stack scratch is fixed-size. */
    if(!lb_work_domain_charge(domain,64)) return allocation_failure(p,LB_ALLOC_WORK_LIMIT);
    p->last_native_cost+=64;
    double number; Eterm words[17],result=THE_NON_VALUE; fenv_t previous;
    memcpy(&number,boxed_val(value)+1,sizeof(number));
    if(fegetenv(&previous)) goto environment_failure;
    if(fesetenv(FE_DFL_ENV)) { (void)fesetenv(&previous); goto environment_failure; }
    if(isfinite(number)) result=integer_from_integral_double(floor(number),words);
    if(fesetenv(&previous)) goto environment_failure;
    if(is_non_value(result)) return fail(p,LB_EXC_BADARG);
    if(is_small(result)) return result;
    return publish_words(p,words,(size_t)(words[0]>>_HEADER_ARITY_OFFS)+1);
environment_failure:
    p->status=LB_PROCESS_ENVIRONMENT; return THE_NON_VALUE;
}
Eterm lb_bif_list_to_tuple_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)pc; p->last_native_cost=0;
    if(args!=p->reg) { p->status=LB_PROCESS_INVALID; return THE_NON_VALUE; }
    LbAllocDomain *domain=p->entry_module->space->domain;
    if(!lb_work_domain_charge(domain,1)) return allocation_failure(p,LB_ALLOC_WORK_LIMIT);
    p->last_native_cost=1; size_t count=0; Eterm tail=args[0];
    while(is_list(tail)) {
        if(!lb_work_domain_charge(domain,1)) return allocation_failure(p,LB_ALLOC_WORK_LIMIT);
        ++p->last_native_cost;
        if(++count>MAX_ARITYVAL) return fail(p,LB_EXC_BADARG);
        tail=CDR(list_val(tail));
    }
    if(!is_nil(tail)) return fail(p,LB_EXC_BADARG);
    if(!count) return make_tuple(p->entry_module->space->engine->empty_tuple)|TAG_LITERAL_PTR;
    size_t words=count+1,need,collections=p->collections;
    if(!lb_size_add(words,p->heap_reserved,&need)) return allocation_failure(p,LB_ALLOC_LIMIT);
    if(!lb_work_domain_charge(domain,words)) return allocation_failure(p,LB_ALLOC_WORK_LIMIT);
    p->last_native_cost+=words;
    if(!lb_heap_reserve(p,need,1)) {
        LbAllocStatus cause=lb_alloc_domain_failure(domain);
        return allocation_failure(p,cause==LB_ALLOC_OK?LB_ALLOC_LIMIT:cause);
    }
    if(p->collections!=collections) p->last_native_cost+=p->last_gc_cost;
    /* Proper-list proof survives GC. Reload the root; no guest execution,
     * failure or allocation can intervene during the native shallow copy. */
    Eterm *tuple=p->htop; tail=args[0]; tuple[0]=make_arityval(count);
    for(size_t i=1;i<=count;++i) { Eterm *cell=list_val(tail); tuple[i]=CAR(cell); tail=CDR(cell); }
    p->htop+=words; return make_tuple(tuple);
}
Eterm lb_bif_tuple_to_list_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)pc; p->last_native_cost=0;
    if(args!=p->reg) { p->status=LB_PROCESS_INVALID; return THE_NON_VALUE; }
    LbAllocDomain *domain=p->entry_module->space->domain;
    if(!lb_work_domain_charge(domain,1)) return allocation_failure(p,LB_ALLOC_WORK_LIMIT);
    p->last_native_cost=1;
    if(!is_tuple(args[0])) return fail(p,LB_EXC_BADARG);
    size_t count=arityval(*tuple_val(args[0]));
    if(!count) return NIL;
    size_t words,need,collections=p->collections;
    if(!lb_size_mul(count,2,&words) || !lb_size_add(words,p->heap_reserved,&need))
        return allocation_failure(p,LB_ALLOC_LIMIT);
    if(!lb_work_domain_charge(domain,words)) return allocation_failure(p,LB_ALLOC_WORK_LIMIT);
    p->last_native_cost+=words;
    if(!lb_heap_reserve(p,need,1)) {
        LbAllocStatus cause=lb_alloc_domain_failure(domain);
        return allocation_failure(p,cause==LB_ALLOC_OK?LB_ALLOC_LIMIT:cause);
    }
    if(p->collections!=collections) p->last_native_cost+=p->last_gc_cost;
    /* Reservation can move the source. No fallible step or collection follows
     * this reload; build backwards without changing or deep-copying elements. */
    Eterm *source=tuple_val(args[0]),*hp=p->htop,result=NIL;
    for(size_t i=count;i>0;--i) { result=CONS(hp,source[i],result); hp+=2; }
    p->htop=hp; return result;
}
Eterm lb_bif_make_tuple_2(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)pc; p->last_native_cost=0;
    if(args!=p->reg) { p->status=LB_PROCESS_INVALID; return THE_NON_VALUE; }
    LbAllocDomain *domain=p->entry_module->space->domain;
    if(!lb_work_domain_charge(domain,1)) return allocation_failure(p,LB_ALLOC_WORK_LIMIT);
    p->last_native_cost=1;
    if(!is_small(args[0]) || signed_val(args[0])<0 || (Uint)signed_val(args[0])>MAX_ARITYVAL)
        return fail(p,LB_EXC_BADARG);
    size_t count=(size_t)signed_val(args[0]);
    /* Worlds/processes retain their Engine, so this immutable, namespace-free
     * native singleton outlives every guest reference without an extra lease. */
    if(!count) return make_tuple(p->entry_module->space->engine->empty_tuple)|TAG_LITERAL_PTR;
    size_t words=count+1,need,collections=p->collections;
    if(!lb_size_add(words,p->heap_reserved,&need)) return allocation_failure(p,LB_ALLOC_LIMIT);
    if(!lb_work_domain_charge(domain,words)) return allocation_failure(p,LB_ALLOC_WORK_LIMIT);
    p->last_native_cost+=words;
    if(!lb_heap_reserve(p,need,2)) {
        LbAllocStatus cause=lb_alloc_domain_failure(domain);
        return allocation_failure(p,cause==LB_ALLOC_OK?LB_ALLOC_LIMIT:cause);
    }
    if(p->collections!=collections) p->last_native_cost+=p->last_gc_cost;
    Eterm *tuple=p->htop,value=args[1]; tuple[0]=make_arityval(count);
    for(size_t i=1;i<=count;++i) tuple[i]=value;
    p->htop+=words; return make_tuple(tuple);
}
Eterm lb_bif_setelement_3(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)pc; p->last_native_cost=1;
    /* Heavy-BIF trampolines pass the process's moving X roots, never copied
     * C operands. Re-read both tuple and replacement after heap reservation. */
    if(args!=p->reg) { p->status=LB_PROCESS_INVALID; return THE_NON_VALUE; }
    if(!is_small(args[0]) || !is_tuple(args[1])) return fail(p,LB_EXC_BADARG);
    Sint index=signed_val(args[0]); size_t arity=arityval(*tuple_val(args[1]));
    if(index<1 || (Uint)index>arity) return fail(p,LB_EXC_BADARG);
    size_t words,need,collections=p->collections;
    if(!lb_size_add(arity,1,&words) || !lb_size_add(words,p->heap_reserved,&need))
        return allocation_failure(p,LB_ALLOC_LIMIT);
    LbAllocDomain *domain=p->entry_module->space->domain;
    p->last_native_cost+=lb_work_bulk_units(words);
    if(!lb_work_domain_charge(domain,p->last_native_cost)) {
        p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE;
    }
    if(!lb_heap_reserve(p,need,MAX_REG)) return allocation_failure(p,lb_alloc_domain_failure(domain));
    if(p->collections!=collections) p->last_native_cost+=p->last_gc_cost;
    Eterm *result=p->htop;
    memcpy(result,tuple_val(p->reg[1]),words*sizeof(Eterm));
    result[index]=p->reg[2]; p->htop+=words;
    return make_tuple(result);
}
Eterm lb_bif_abs_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    Eterm value=args[0],words[2]; (void)pc; p->last_native_cost=1;
    LbAllocDomain *domain=p->entry_module->space->domain;
    if(!lb_work_domain_charge(domain,1)) return allocation_failure(p,LB_ALLOC_WORK_LIMIT);
    if(is_small(value)) {
        Sint integer=signed_val(value),magnitude=integer<0?-integer:integer;
        if(IS_SSMALL(magnitude)) return make_small(magnitude);
        lb_small_to_big(magnitude,words); return publish_words(p,words,2);
    }
    if(lb_is_big(value)) {
        if(!big_is_negative(value)) return value;
        size_t count=lb_big_size(value)+1,bytes;
        if(!lb_work_domain_charge(domain,2*(uint64_t)count)) return allocation_failure(p,LB_ALLOC_WORK_LIMIT);
        p->last_native_cost+=2*lb_work_bulk_units(count);
        if(!lb_size_mul(count,sizeof(Eterm),&bytes)) return allocation_failure(p,LB_ALLOC_LIMIT);
        Eterm *copy; LbAllocStatus status=lb_alloc_domain_allocate(domain,bytes,(void **)&copy);
        if(status!=LB_ALLOC_OK) return allocation_failure(p,status);
        /* Independent numeric payload survives moving collection even when
         * the native BIF group passed operands in its local tmp_reg array. */
        memcpy(copy,boxed_val(value),bytes); copy[0]=_make_header(count-1,POS_BIG_SUBTAG);
        Eterm result=publish_words(p,copy,count); lb_release(domain,copy); return result;
    }
    if(is_float_term(value)) {
        Uint bits=boxed_val(value)[1],magnitude=bits&UINT64_C(0x7fffffffffffffff);
        /* Native abs copies both signed zeros and negative finite floats,
         * but returns positive nonzero values. Clearing the sign bit needs
         * no floating-environment mutation or rounding. NaNs, if presented
         * internally, preserve the native comparison-false return path. */
        if((!(bits>>63) && magnitude) || magnitude>UINT64_C(0x7ff0000000000000)) return value;
        words[0]=_make_header(1,FLOAT_SUBTAG); words[1]=magnitude;
        return publish_words(p,words,2);
    }
    return fail(p,LB_EXC_BADARG);
}
Eterm lb_bif_element_2(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    Eterm index=args[0],tuple=args[1]; (void)pc; p->last_native_cost=1;
    if(!is_small(index) || !is_tuple(tuple)) return fail(p,LB_EXC_BADARG);
    Sint position=signed_val(index); Eterm *elements=tuple_val(tuple);
    Uint arity=arityval(elements[0]);
    if(position<1 || (Uint)position>arity) return fail(p,LB_EXC_BADARG);
    return elements[position];
}
Eterm lb_bif_split_binary_2(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    LbAllocDomain *domain=p->entry_module->space->domain;
    LbBitstringView view; size_t total,prefix_size,suffix_size,prefix_bits,suffix_bits;
    size_t prefix_words,suffix_words,need,source_bytes; Sint position;
    unsigned char *copy=NULL; void *memory=NULL; LbAllocStatus status;
    Eterm prefix,suffix,*tuple; (void)pc; p->last_native_cost=1;
    if(!lb_bitstring_view(args[0],&view) || (view.bit_size&7) || !is_small(args[1])) return fail(p,LB_EXC_BADARG);
    total=view.bit_size/8; position=signed_val(args[1]);
    if(position<0 || (Uint)position>total) return fail(p,LB_EXC_BADARG);
    prefix_size=(size_t)position; suffix_size=total-prefix_size;
    if(!lb_size_mul(prefix_size,8,&prefix_bits) || !lb_size_mul(suffix_size,8,&suffix_bits) ||
       !(prefix_words=lb_bitstring_heap_words(prefix_bits)) || !(suffix_words=lb_bitstring_heap_words(suffix_bits)) ||
       !lb_size_add(prefix_words,suffix_words,&need) || !lb_size_add(need,3,&need) ||
       !lb_size_add(need,p->heap_reserved,&need)) return fail(p,LB_EXC_SYSTEM_LIMIT);
    if(total && !lb_work_domain_charge(domain,1+lb_work_bulk_units(total))) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    p->last_native_cost+=lb_work_bulk_units(total);
    if(total) {
        status=lb_alloc_domain_allocate(domain,total,(void **)&memory);
        if(status!=LB_ALLOC_OK) return allocation_failure(p,status);
        copy=memory;
        if(!lb_size_add(view.bit_offset,view.bit_size,&source_bytes) ||
           !lb_size_add(source_bytes,7,&source_bytes) ||
           (prefix_bits && !lb_bits_copy(view.data,source_bytes/8,view.bit_offset,copy,total,0,prefix_bits)) ||
           (suffix_bits && !lb_bits_copy(view.data,source_bytes/8,view.bit_offset+prefix_bits,copy+prefix_size,suffix_size,0,suffix_bits))) {
            if(lb_alloc_domain_release(domain,memory)!=LB_ALLOC_OK) abort();
            return fail(p,LB_EXC_BADARG);
        }
    }
    size_t collections=p->collections;
    if(!lb_heap_reserve(p,need,MAX_REG)) {
        if(memory && lb_alloc_domain_release(domain,memory)!=LB_ALLOC_OK) abort();
        return allocation_failure(p,lb_alloc_domain_failure(domain));
    }
    if(p->collections!=collections) p->last_native_cost+=p->last_gc_cost;
    status=lb_bitstring_build(domain,&p->off_heap,&p->htop,copy,prefix_bits,0,&prefix);
    if(status==LB_ALLOC_OK)
        status=lb_bitstring_build(domain,&p->off_heap,&p->htop,copy?copy+prefix_size:NULL,suffix_bits,0,&suffix);
    if(memory && lb_alloc_domain_release(domain,memory)!=LB_ALLOC_OK) abort();
    if(status!=LB_ALLOC_OK) return allocation_failure(p,status);
    tuple=p->htop; tuple[0]=make_arityval(2); tuple[1]=prefix; tuple[2]=suffix; p->htop+=3;
    return make_tuple(tuple);
}
static int proper_list_count(LbProcess *p,Eterm list,size_t *count)
{
    LbAllocDomain *domain=p->entry_module->space->domain; size_t length=0;
    while(is_list(list)) {
        if(length==SIZE_MAX) return 2;
        if(!lb_work_domain_charge(domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return -1; }
        ++p->last_native_cost; ++length; list=CDR(list_val(list));
    }
    *count=length; return is_nil(list)?1:0;
}
Eterm lb_bif_minusminus_2(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    LbAllocDomain *domain=p->entry_module->space->domain;
    Eterm *left_items,*right_items,*cells,*previous=NULL,*first=NULL;
    unsigned char *removed; size_t left_count=0,right_count=0,left_bytes,right_bytes,total_bytes;
    size_t words,need,copy_work,work,survivors; void *memory=NULL; LbAllocStatus status;
    (void)pc; p->last_native_cost=0;
    if(!lb_work_domain_charge(domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    p->last_native_cost=1;
    int counted=proper_list_count(p,args[0],&left_count);
    if(counted<0) return THE_NON_VALUE;
    if(counted==2) return fail(p,LB_EXC_SYSTEM_LIMIT);
    if(!counted) return fail(p,LB_EXC_BADARG);
    counted=proper_list_count(p,args[1],&right_count);
    if(counted<0) return THE_NON_VALUE;
    if(counted==2) return fail(p,LB_EXC_SYSTEM_LIMIT);
    if(!counted) return fail(p,LB_EXC_BADARG);
    if(!left_count) return NIL;
    if(!lb_size_mul(left_count,sizeof(Eterm),&left_bytes) ||
       !lb_size_mul(right_count,sizeof(Eterm),&right_bytes) ||
       !lb_size_add(left_bytes,right_bytes,&total_bytes) ||
       !lb_size_add(total_bytes,left_count,&total_bytes) ||
       !lb_size_mul(left_count,2,&words) || !lb_size_add(words,p->heap_reserved,&need) ||
       !lb_size_add(left_count,right_count,&copy_work) || !lb_size_add(copy_work,left_count,&work))
        return fail(p,LB_EXC_SYSTEM_LIMIT);
    if(!lb_work_domain_charge(domain,work)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    p->last_native_cost+=work;
    size_t collections=p->collections;
    if(!lb_heap_reserve(p,need,MAX_REG)) return allocation_failure(p,lb_alloc_domain_failure(domain));
    if(p->collections!=collections) p->last_native_cost+=p->last_gc_cost;
    status=lb_alloc_domain_allocate(domain,total_bytes,&memory);
    if(status!=LB_ALLOC_OK) return allocation_failure(p,status);
    left_items=memory; right_items=(Eterm *)((unsigned char *)memory+left_bytes);
    removed=(unsigned char *)memory+left_bytes+right_bytes; memset(removed,0,left_count);
    Eterm list=args[0];
    for(size_t i=0;i<left_count;++i) { left_items[i]=CAR(list_val(list)); list=CDR(list_val(list)); }
    list=args[1];
    for(size_t i=0;i<right_count;++i) { right_items[i]=CAR(list_val(list)); list=CDR(list_val(list)); }
    survivors=left_count;
    for(size_t j=0;j<right_count;++j) for(size_t i=0;i<left_count;++i) {
        if(!lb_work_domain_charge(domain,1)) {
            if(lb_alloc_domain_release(domain,memory)!=LB_ALLOC_OK) abort();
            p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE;
        }
        ++p->last_native_cost;
        if(removed[i]) continue;
        int equal=0; status=lb_term_equal(domain,left_items[i],right_items[j],&equal);
        if(status!=LB_ALLOC_OK) {
            LbAllocStatus failure=lb_alloc_domain_failure(domain);
            if(failure==LB_ALLOC_OK) failure=status;
            if(lb_alloc_domain_release(domain,memory)!=LB_ALLOC_OK) abort();
            return allocation_failure(p,failure);
        }
        if(equal) { removed[i]=1; --survivors; break; }
    }
    cells=p->htop;
    for(size_t i=0;i<left_count;++i) if(!removed[i]) {
        if(!lb_work_domain_charge(domain,1)) {
            if(lb_alloc_domain_release(domain,memory)!=LB_ALLOC_OK) abort();
            p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE;
        }
        ++p->last_native_cost; cells[0]=left_items[i]; cells[1]=NIL;
        if(previous) previous[1]=make_list(cells); else first=cells;
        previous=cells; cells+=2;
    }
    if(previous) previous[1]=NIL;
    p->htop=cells;
    if(lb_alloc_domain_release(domain,memory)!=LB_ALLOC_OK) abort();
    return survivors?make_list(first):NIL;
}
Eterm lb_bif_plusplus_2(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    LbAllocDomain *domain=p->entry_module->space->domain;
    Eterm source=args[0],tail; Eterm *first=NULL,*previous=NULL,*cell,*hp;
    size_t count=0,words,need;
    (void)pc; p->last_native_cost=0;
    if(!lb_work_domain_charge(domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    p->last_native_cost=1;
    while(is_list(source)) {
        if(count==SIZE_MAX/2) return fail(p,LB_EXC_SYSTEM_LIMIT);
        if(!lb_work_domain_charge(domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
        ++p->last_native_cost; ++count; source=CDR(list_val(source));
    }
    if(!is_nil(source)) return fail(p,LB_EXC_BADARG);
    if(!count) return args[1];
    if(!lb_work_domain_charge(domain,count)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    p->last_native_cost+=count;
    if(!lb_size_mul(count,2,&words) || !lb_size_add(words,p->heap_reserved,&need)) return fail(p,LB_EXC_SYSTEM_LIMIT);
    size_t collections=p->collections;
    if(!lb_heap_reserve(p,need,MAX_REG))
        return allocation_failure(p,lb_alloc_domain_failure(domain));
    if(p->collections!=collections) p->last_native_cost+=p->last_gc_cost;
    source=args[0]; tail=args[1]; hp=p->htop;
    while(is_list(source)) {
        cell=list_val(source); hp[0]=cell[0]; hp[1]=NIL;
        if(previous) previous[1]=make_list(hp); else first=hp;
        previous=hp; hp+=2; source=cell[1];
    }
    previous[1]=tail; p->htop=hp;
    return make_list(first);
}
static Eterm integer_bitwise(LbProcess *p,Eterm left,Eterm right,unsigned operation)
{
    LbAllocDomain *domain=p->entry_module->space->domain;
    Eterm small_left[2],small_right[2],*scratch,result;
    size_t count,words,bytes; void *memory=NULL; LbAllocStatus status;
    if(!(is_small(left)||lb_is_big(left)) || !(is_small(right)||lb_is_big(right))) return fail(p,LB_EXC_BADARITH);
    if(is_small(left) && is_small(right)) {
        Sint a=signed_val(left),b=signed_val(right);
        return make_small(operation==0?(a&b):operation==1?(a|b):(a^b));
    }
    if(is_small(left)) left=lb_small_to_big(signed_val(left),small_left);
    if(is_small(right)) right=lb_small_to_big(signed_val(right),small_right);
    count=(lb_big_size(left)>lb_big_size(right)?lb_big_size(left):lb_big_size(right))+1;
    if(!lb_size_mul(count,3,&words) || !lb_size_add(words,2,&words) ||
       !lb_size_mul(words,sizeof(Eterm),&bytes)) return fail(p,LB_EXC_SYSTEM_LIMIT);
    if(!lb_work_domain_charge(domain,1+words+lb_work_bulk_units(bytes))) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    p->last_native_cost=1+words;
    status=lb_alloc_domain_allocate(domain,bytes,&memory);
    if(status!=LB_ALLOC_OK) return allocation_failure(p,status);
    scratch=memory; result=lb_big_bitwise(left,right,operation,scratch,words);
    if(is_nil(result)) {
        if(lb_alloc_domain_release(domain,memory)!=LB_ALLOC_OK) abort();
        return fail(p,LB_EXC_SYSTEM_LIMIT);
    }
    if(!is_small(result)) result=publish_words(p,boxed_val(result),lb_big_size(result)+1);
    if(lb_alloc_domain_release(domain,memory)!=LB_ALLOC_OK) abort();
    return result;
}
Eterm lb_bif_band_2(LbProcess *p,Eterm *args,const BeamInstr *pc)
{ (void)pc; p->last_native_cost=0; return integer_bitwise(p,args[0],args[1],0); }
Eterm lb_bif_bor_2(LbProcess *p,Eterm *args,const BeamInstr *pc)
{ (void)pc; p->last_native_cost=0; return integer_bitwise(p,args[0],args[1],1); }
Eterm lb_bif_bxor_2(LbProcess *p,Eterm *args,const BeamInstr *pc)
{ (void)pc; p->last_native_cost=0; return integer_bitwise(p,args[0],args[1],2); }
Eterm lb_bif_bnot_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    LbAllocDomain *domain=p->entry_module->space->domain;
    Eterm value=args[0],*scratch,result; size_t count,words,bytes;
    void *memory=NULL; LbAllocStatus status;
    (void)pc; p->last_native_cost=0;
    if(is_small(value)) return make_small(~signed_val(value));
    if(!lb_is_big(value)) return fail(p,LB_EXC_BADARITH);
    count=lb_big_size(value)+1;
    if(!lb_size_mul(count,2,&words) || !lb_size_add(words,2,&words) ||
       !lb_size_mul(words,sizeof(Eterm),&bytes)) return fail(p,LB_EXC_SYSTEM_LIMIT);
    if(!lb_work_domain_charge(domain,1+words+lb_work_bulk_units(bytes))) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    p->last_native_cost=1+words;
    status=lb_alloc_domain_allocate(domain,bytes,&memory);
    if(status!=LB_ALLOC_OK) return allocation_failure(p,status);
    scratch=memory; result=lb_big_bnot(value,scratch,words);
    if(is_nil(result)) {
        if(lb_alloc_domain_release(domain,memory)!=LB_ALLOC_OK) abort();
        return fail(p,LB_EXC_SYSTEM_LIMIT);
    }
    if(!is_small(result)) result=publish_words(p,boxed_val(result),lb_big_size(result)+1);
    if(lb_alloc_domain_release(domain,memory)!=LB_ALLOC_OK) abort();
    return result;
}
