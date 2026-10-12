/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1996-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Selected native Sint_to_buf and big.c:write_big_simple/D_div, base 10.
 * Explicit owned continuation replaces dirty-scheduler handoff. This selects
 * the schoolbook algorithm directly, not an incomplete D&C wrapper/fallback.
 */
#include "decimal.h"
#include "work_budget.h"
#include "big.h"
#include <string.h>
#include <stdlib.h>
/* Native flat-64 digits_per_sint_lookup[10-2] and corresponding power. */
#define DECIMAL_DIGITS_PER_PASS 17
#define DECIMAL_DIVISOR UINT64_C(100000000000000000)
struct LbDecimal {
    LbAllocDomain *domain;
    size_t capacity,used,bytes;
    unsigned char *cursor;
    int negative,quick,done;
    Uint digits[];
};
LbDecimalView lb_decimal_small(Sint value,unsigned char output[LB_DECIMAL_SMALL_BYTES])
{
    unsigned char *end=output+LB_DECIMAL_SMALL_BYTES,*p=end;
    int sign=value<0; Uint magnitude=sign?-(Uint)value:(Uint)value;
    /* Unsigned magnitude also handles INT64_MIN without signed negation UB. */
    do { *--p=(unsigned char)('0'+magnitude%10); magnitude/=10; } while(magnitude);
    if(sign) *--p='-';
    return (LbDecimalView){p,(size_t)(end-p)};
}
LbAllocStatus lb_decimal_create(LbAllocDomain *domain,Eterm value,LbDecimal **out)
{
    size_t digits,digit_bytes,bytes,total; LbDecimal *d; void *memory; LbAllocStatus status;
    if(!out) return LB_ALLOC_INVALID;
    *out=NULL;
    if(!domain || !lb_is_big(value)) return LB_ALLOC_INVALID;
    digits=lb_big_size(value);
    if(!digits || digits>LB_BIG_ARITY_MAX || !boxed_val(value)[digits]) return LB_ALLOC_INVALID;
    /* A 64-bit limb needs <20 decimal characters; one extra byte for sign.
     * Integer-only checked sizing avoids dependence on ambient FP rounding. */
    if(digits>(SIZE_MAX-1)/20 || digits>SIZE_MAX/sizeof(Uint)) return LB_ALLOC_NO_MEMORY;
    bytes=digits*20+1; digit_bytes=digits*sizeof(Uint);
    if(digit_bytes>SIZE_MAX-sizeof(*d) || bytes>SIZE_MAX-sizeof(*d)-digit_bytes) return LB_ALLOC_NO_MEMORY;
    total=sizeof(*d)+digit_bytes+bytes;
    status=lb_alloc_domain_allocate(domain,total,&memory); if(status!=LB_ALLOC_OK) return status;
    d=memory; d->domain=domain; d->capacity=d->used=digits; d->bytes=bytes; d->done=0;
    d->negative=(*boxed_val(value)&_TAG_HEADER_MASK)==NEG_BIG_SUBTAG;
    memcpy(d->digits,boxed_val(value)+1,digit_bytes);
    d->quick=digits==1 && d->digits[0]<DECIMAL_DIVISOR;
    d->cursor=(unsigned char *)(d->digits+digits)+bytes; *out=d; return LB_ALLOC_OK;
}
/* Native D_div with x==q and D_EXP=64. Bounded indices replace pointers that
 * the original decrements before the digit allocation at the final iteration. */
static size_t divide(Uint *digits,size_t count,Uint *remainder)
{
    size_t unread=count-1,write=count,result=count; Uint high=digits[unread];
    if(DECIMAL_DIVISOR>high) {
        if(count==1) { *remainder=high; digits[0]=0; return 1; }
        --result; --write;
    }
    do {
        Uint low,quotient,product;
        if(DECIMAL_DIVISOR>high) { assert(unread); low=digits[--unread]; }
        else { low=high; high=0; }
        quotient=(Uint)((((__uint128_t)high<<64)|low)/DECIMAL_DIVISOR);
        product=DECIMAL_DIVISOR*quotient; high=low-product;
        digits[--write]=quotient;
    } while(unread);
    assert(!write); *remainder=high; return result;
}
static void character(LbDecimal *d,unsigned char value)
{
    /* A violation here is an internal sizing/algorithm error, not malformed
     * guest bytes; input terms have already passed native representation rules. */
    if(d->cursor<=(unsigned char *)(d->digits+d->capacity)) abort();
    *--d->cursor=value;
}
int lb_decimal_step(LbDecimal *d,size_t word_budget,size_t *work)
{
    size_t used=0; Uint remainder;
    assert(d && word_budget && work);
    while(!d->done && used<word_budget) {
        /* Division must not mutate the retained continuation without credit. */
        if(!lb_work_domain_charge(d->domain,d->used+DECIMAL_DIGITS_PER_PASS+1)) { *work=used; return 0; }
        used+=d->used;
        if(d->quick) { remainder=d->digits[0]; d->done=1; }
        else {
            d->used=divide(d->digits,d->used,&remainder);
            d->done=d->used==1 && !d->digits[0];
        }
        if(d->done) {
            do { character(d,(unsigned char)('0'+remainder%10)); remainder/=10; } while(remainder);
            if(d->negative) character(d,'-');
        } else {
            unsigned i=DECIMAL_DIGITS_PER_PASS;
            while(i--) { character(d,(unsigned char)('0'+remainder%10)); remainder/=10; }
        }
    }
    *work=used; return d->done;
}
LbDecimalView lb_decimal_view(const LbDecimal *d)
{
    if(!d || !d->done) return (LbDecimalView){NULL,0};
    return (LbDecimalView){d->cursor,(size_t)((const unsigned char *)(d->digits+d->capacity)+d->bytes-d->cursor)};
}
void lb_decimal_destroy(LbDecimal *d)
{
    if(d && lb_alloc_domain_release(d->domain,d)!=LB_ALLOC_OK) abort();
}
