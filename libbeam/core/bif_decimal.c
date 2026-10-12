/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1996-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * binary.c:integer_to_binary/1 with explicit process-owned continuation,
 * fallible scratch and the existing owned binary constructor.
 */
#include "process_internal.h"
#include "big.h"
#define DECIMAL_SLICE_WORDS 4096
static Eterm no_memory(LbProcess *p)
{
    p->status=LB_PROCESS_NO_MEMORY; return THE_NON_VALUE;
}
static Eterm publish(LbProcess *p,LbDecimalView view)
{
    size_t words=lb_bitstring_heap_words(view.size*8),need,collections=p->collections;
    Eterm result; LbAllocDomain *domain=p->entry_module->space->domain;
    /* The view borrows a stack buffer or independent owned scratch, never the
     * process heap. It therefore survives this possible copying collection. */
    if(!words || !lb_size_add(words,p->heap_reserved,&need) || !lb_heap_reserve(p,need,MAX_REG)) return no_memory(p);
    if(p->collections!=collections) p->last_native_cost+=p->last_gc_cost;
    if(lb_bitstring_build(domain,&p->off_heap,&p->htop,view.data,view.size*8,0,&result)!=LB_ALLOC_OK) return no_memory(p);
    return result;
}
Eterm lb_bif_integer_to_binary_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    LbAllocDomain *domain=p->entry_module->space->domain; size_t work; Eterm result;
    (void)pc; p->last_native_cost=0;
    if(!p->decimal) {
        LbAllocStatus status;
        if(is_small(args[0])) {
            unsigned char output[LB_DECIMAL_SMALL_BYTES];
            if(!lb_work_domain_charge(domain,LB_DECIMAL_SMALL_BYTES)) return no_memory(p);
            return publish(p,lb_decimal_small(signed_val(args[0]),output));
        }
        if(!lb_is_big(args[0])) { p->freason=LB_EXC_BADARG; return THE_NON_VALUE; }
        status=lb_decimal_create(domain,args[0],&p->decimal);
        if(status==LB_ALLOC_NO_MEMORY) return no_memory(p);
        if(status!=LB_ALLOC_OK) { p->freason=LB_EXC_SYSTEM_LIMIT; return THE_NON_VALUE; }
    }
    if(!lb_decimal_step(p->decimal,DECIMAL_SLICE_WORDS,&work)) {
        p->last_native_cost=work/16+1;
        return THE_NON_VALUE; /* owned continuation, not a guest exception */
    }
    p->last_native_cost=work/16+1;
    result=publish(p,lb_decimal_view(p->decimal));
    lb_decimal_destroy(p->decimal); p->decimal=NULL; return result;
}
