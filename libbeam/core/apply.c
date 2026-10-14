/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1996-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Owned adaptation of beam_common.c apply/fixed_apply. Dynamic dispatch stays
 * within the already-published code space; no stub export or autoload effect.
 */
#include "apply.h"
#include "fun.h"

typedef struct { Eterm module,function,arguments; } ApplyCall;
static int charge(LbProcess *p,Uint units)
{
    if(!lb_work_domain_charge(p->entry_module->space->domain,units)) {
        p->status=LB_PROCESS_WORK_LIMIT; return 0;
    }
    p->last_native_cost+=units; return 1;
}
static const Export *allocation_failure(LbProcess *p,LbAllocStatus status)
{
    LbAllocStatus cause=lb_alloc_domain_failure(p->entry_module->space->domain);
    if(cause!=LB_ALLOC_OK) status=cause;
    switch(status) {
    case LB_ALLOC_LIMIT:p->status=LB_PROCESS_LIMIT; break;
    case LB_ALLOC_WORK_LIMIT:p->status=LB_PROCESS_WORK_LIMIT; break;
    case LB_ALLOC_NO_MEMORY:p->status=LB_PROCESS_NO_MEMORY; break;
    default:p->status=LB_PROCESS_INVALID; break;
    }
    return NULL;
}
static const Export *guest_error(LbProcess *p,ApplyCall call,Uint reason)
{
    /* OTP reports the innermost apply triple after flattening apply-of-apply. */
    p->reg[0]=call.module; p->reg[1]=call.function; p->reg[2]=call.arguments;
    p->freason=reason; return NULL;
}
static const Export *resolve(LbProcess *p,Eterm module,Eterm function,unsigned arity)
{
    LbCodeSpace *space=p->entry_module->space;
    for(Export *entry=space->exports;entry;entry=entry->next) {
        if(!charge(p,1)) return NULL;
        if(entry->info.mfa.module!=module || entry->info.mfa.function!=function || entry->info.mfa.arity!=arity) continue;
        LbAllocStatus status=lb_fun_retain_export(space,&p->fun_leases,entry,&p->last_native_cost);
        if(status!=LB_ALLOC_OK) return allocation_failure(p,status);
        return entry;
    }
    p->status=LB_PROCESS_REFUSED; return NULL;
}
const Export *lb_apply(LbProcess *p)
{
    ApplyCall call={p->reg[0],p->reg[1],p->reg[2]}; p->last_native_cost=0;
    for(;;) {
        if(!charge(p,1)) return NULL;
        if(!is_atom(call.module) || !is_atom(call.function)) return guest_error(p,call,LB_EXC_BADARG);
        if(call.module!=am_erlang || call.function!=am_apply) break;
        /* Bounded lookahead: only a proper three-element list is recursive
         * apply/3. Other arities go through normal loaded-export resolution. */
        if(!charge(p,3)) return NULL;
        Eterm cursor=call.arguments,m,f;
        if(!is_list(cursor)) break;
        m=list_val(cursor)[0]; cursor=list_val(cursor)[1];
        if(!is_list(cursor)) break;
        f=list_val(cursor)[0]; cursor=list_val(cursor)[1];
        if(!is_list(cursor) || !is_nil(list_val(cursor)[1])) break;
        call=(ApplyCall){m,f,list_val(cursor)[0]};
    }
    Eterm cursor=call.arguments; unsigned arity=0;
    while(is_list(cursor)) {
        if(!charge(p,1)) return NULL;
        if(arity==MAX_ARG) return guest_error(p,call,LB_EXC_SYSTEM_LIMIT);
        ++arity; cursor=list_val(cursor)[1];
    }
    if(!is_nil(cursor)) return guest_error(p,call,LB_EXC_BADARG);
    /* Reserve all publication work before acquiring the target lease. No
     * fallible step or collection may follow successful lease acquisition. */
    if(!charge(p,arity+lb_work_bulk_units(arity))) return NULL;
    const Export *target=resolve(p,call.module,call.function,arity);
    if(!target) return NULL;
    cursor=call.arguments;
    for(unsigned i=0;i<arity;++i) { p->reg[i]=list_val(cursor)[0]; cursor=list_val(cursor)[1]; }
    return target;
}
const BeamInstr *lb_apply_fun(LbProcess *p)
{
    Eterm fun=p->reg[0],arguments=p->reg[1],cursor=arguments; unsigned arity=0;
    p->last_native_cost=0;
    if(!charge(p,1)) return NULL;
    /* Validate the complete spine before overwriting any argument register.
     * List errors precede badfun/badarity, as in the pinned apply_fun. */
    while(is_list(cursor)) {
        if(!charge(p,1)) return NULL;
        if(arity==MAX_REG-1) {
            p->current=&p->entry_module->space->apply_fun_export.info.mfa;
            p->freason=LB_EXC_SYSTEM_LIMIT; return NULL;
        }
        ++arity; cursor=CDR(list_val(cursor));
    }
    if(!is_nil(cursor)) {
        p->current=&p->entry_module->space->apply_fun_export.info.mfa;
        p->freason=LB_EXC_BADARG; return NULL;
    }
    if(!charge(p,arity+lb_work_bulk_units(arity+1))) return NULL;
    cursor=arguments;
    for(unsigned i=0;i<arity;++i) { p->reg[i]=CAR(list_val(cursor)); cursor=CDR(list_val(cursor)); }
    p->reg[arity]=fun;
    /* All borrowed input views are now rooted in the register bank. Shared
     * fun resolution checks namespace/code leases and owns any error GC. */
    Uint prior=p->last_native_cost;
    const BeamInstr *next=lb_fun_call(p,arity); p->last_native_cost+=prior;
    return next;
}
const Export *lb_fixed_apply(LbProcess *p,unsigned arity)
{
    p->last_native_cost=0;
    if(arity>MAX_ARG) { p->status=LB_PROCESS_INVALID; return NULL; }
    if(!charge(p,1)) return NULL;
    Eterm module=p->reg[arity],function=p->reg[arity+1];
    if(!is_atom(module) || !is_atom(function)) {
        size_t words=2*(size_t)arity,need,collections=p->collections;
        if(!charge(p,arity+lb_work_bulk_units(words))) return NULL;
        if(!lb_size_add(words,p->heap_reserved,&need)) { p->status=LB_PROCESS_LIMIT; return NULL; }
        if(words && !lb_heap_reserve(p,need,arity+2)) return allocation_failure(p,LB_ALLOC_NO_MEMORY);
        if(p->collections!=collections) p->last_native_cost+=p->last_gc_cost;
        /* Module/function themselves can be boxed invalid arguments. Reload
         * them as well as argument values after the moving collection. */
        module=p->reg[arity]; function=p->reg[arity+1]; Eterm args=NIL;
        for(unsigned i=arity;i;--i) { args=CONS(p->htop,p->reg[i-1],args); p->htop+=2; }
        return guest_error(p,(ApplyCall){module,function,args},LB_EXC_BADARG);
    }
    if(module==am_erlang && function==am_apply && arity==3) {
        Uint prior=p->last_native_cost;
        const Export *target=lb_apply(p); p->last_native_cost+=prior; return target;
    }
    return resolve(p,module,function,arity);
}
