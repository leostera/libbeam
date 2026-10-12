/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1996-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Owned adaptation of erl_arith.c:erts_mixed_plus. Integer digit algorithms
 * remain in big.c. Compute before collection, then publish into the owned heap;
 * no untracked ERTS heap fragments or dangling C operands across collection.
 */
#include "process_internal.h"
#include "big.h"
#include <fenv.h>
#include <math.h>
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
static Eterm publish_words(LbProcess *p,const Eterm *words,size_t count)
{
    Eterm *hp; size_t need,collections=p->collections;
    /* Native arithmetic uses non-collecting heap fragments. The owned adapter
     * instead finishes computation into independent storage before collecting.
     * Preserve pending heap credit and all physically initialized X/Y roots. */
    if(!lb_size_add(count,p->heap_reserved,&need) || !lb_heap_reserve(p,need,MAX_REG)) return no_memory(p);
    if(p->collections!=collections) p->last_native_cost+=p->last_gc_cost;
    hp=p->htop; memcpy(hp,words,count*sizeof(*hp)); p->htop+=count; return make_boxed(hp);
}
static Eterm integer_plus(LbProcess *p,Eterm a,Eterm b)
{
    Eterm small_a[2],small_b[2],result; Eterm *words;
    LbAllocDomain *domain=p->entry_module->space->domain; size_t size,bytes; void *memory;
    if(is_small(a) && is_small(b)) {
        Sint sum=signed_val(a)+signed_val(b);
        if(IS_SSMALL(sum)) return make_small(sum);
        lb_small_to_big(sum,small_a); return publish_words(p,small_a,2);
    }
    if(is_small(a)) {
        if(a==make_small(0)) return b;
        a=lb_small_to_big(signed_val(a),small_a);
    }
    if(is_small(b)) {
        if(b==make_small(0)) return a;
        b=lb_small_to_big(signed_val(b),small_b);
    }
    size=lb_big_size(a)>lb_big_size(b)?lb_big_size(a):lb_big_size(b);
    if(size>LB_BIG_ARITY_MAX) return fail(p,LB_EXC_SYSTEM_LIMIT);
    size+=2;
    if(!lb_work_domain_charge(domain,2*size+1)) return no_memory(p);
    if(!lb_size_mul(size,sizeof(Eterm),&bytes) || lb_alloc_domain_allocate(domain,bytes,&memory)!=LB_ALLOC_OK) return no_memory(p);
    words=memory; result=lb_big_plus(a,b,words);
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
static Eterm float_plus(LbProcess *p,Eterm a,Eterm b)
{
    fenv_t previous; double left,right,sum=0; Eterm words[2]; int valid;
    _Static_assert(sizeof(double)==sizeof(Eterm),"Native flat-64 float payload");
    uint64_t work=1+(lb_is_big(a)?lb_big_size(a):0)+(lb_is_big(b)?lb_big_size(b):0);
    if(!lb_work_domain_charge(p->entry_module->space->domain,work)) return no_memory(p);
    /* The host may use another rounding mode or enabled FP traps. This scope
     * never exposes those settings to guest arithmetic, nor changes the host
     * control thread. Restore the worker's environment before any allocation. */
    if(fegetenv(&previous)) goto environment_failure;
    if(fesetenv(FE_DFL_ENV)) { (void)fesetenv(&previous); goto environment_failure; }
    valid=number_to_double(a,&left) && number_to_double(b,&right);
    if(valid) { sum=left+right; valid=isfinite(sum); }
    if(fesetenv(&previous)) goto environment_failure;
    if(!valid) return fail(p,LB_EXC_BADARITH);
    words[0]=_make_header(1,FLOAT_SUBTAG); memcpy(words+1,&sum,sizeof(sum));
    return publish_words(p,words,2);
environment_failure:
    /* Infrastructure refusal is not a false arithmetic guard or bad number. */
    p->status=LB_PROCESS_EXCEPTION; return fail(p,LB_EXC_NOTSUP);
}
Eterm lb_mixed_plus(LbProcess *p,Eterm a,Eterm b)
{
    p->last_native_cost=0;
    if((is_small(a)||lb_is_big(a)) && (is_small(b)||lb_is_big(b))) return integer_plus(p,a,b);
    if(is_float_term(a) || is_float_term(b)) return float_plus(p,a,b);
    return fail(p,LB_EXC_BADARITH);
}
Eterm lb_bif_plus_2(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)pc; return lb_mixed_plus(p,args[0],args[1]);
}
