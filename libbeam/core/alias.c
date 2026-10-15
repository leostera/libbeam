/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Namespace-local process aliases with stable, moving-heap-independent identity.
 */
#include "alias.h"
#include "process_internal.h"
#include "reference.h"
#include "alloc_internal.h"
#include "work_budget_internal.h"

LbAlias *lb_alias_lookup(LbCodeSpace *space,Eterm value,Uint *work)
{
    const LbOrdinaryRef *ref=lb_reference(value);
    if(!space || !ref) return NULL;
    for(LbAlias *alias=space->aliases;alias;alias=alias->next) {
        if(!lb_work_domain_charge(space->domain,1)) return NULL;
        if(work) ++*work;
        if(!memcmp(alias->reference,ref->num,sizeof(alias->reference))) return alias;
    }
    return NULL;
}
void lb_alias_deactivate(LbAlias *alias)
{ if(alias) alias->active=0; }
void lb_alias_deactivate_process(LbProcess *p)
{
    if(!p) return;
    for(LbAlias *alias=p->entry_module->space->aliases;alias;alias=alias->next)
        if(alias->owner==p) alias->active=0;
}
void lb_alias_space_destroy(LbCodeSpace *space)
{
    LbAlias *alias=space->aliases; space->aliases=NULL;
    while(alias) { LbAlias *next=alias->next; lb_release(space->domain,alias); alias=next; }
}
static Eterm create_alias(LbProcess *p,int reply)
{
    LbCodeSpace *space=p->entry_module->space; LbEngine *engine=space->engine; LbAllocDomain *domain=space->domain;
    p->last_native_cost=1;
    if(!lb_work_domain_charge(domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    if(!p->world || !p->running) { p->status=LB_PROCESS_REFUSED; return THE_NON_VALUE; }
    if(engine->reference_counter==UINT64_MAX) { p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE; }
    size_t needed,before=p->collections;
    if(!lb_size_add(LB_REF_WORDS,p->heap_reserved,&needed)) { p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE; }
    if(!lb_work_domain_charge(domain,LB_REF_WORDS+1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    p->last_native_cost+=LB_REF_WORDS+1;
    if(!lb_heap_reserve(p,needed,MAX_REG)) {
        LbAllocStatus cause=lb_alloc_domain_failure(domain);
        p->status=cause==LB_ALLOC_WORK_LIMIT?LB_PROCESS_WORK_LIMIT:
            cause==LB_ALLOC_NO_MEMORY?LB_PROCESS_NO_MEMORY:LB_PROCESS_LIMIT;
        return THE_NON_VALUE;
    }
    if(p->collections!=before) p->last_native_cost+=p->last_gc_cost;
    void *memory;
    if(lb_alloc_domain_allocate(domain,sizeof(LbAlias),&memory)!=LB_ALLOC_OK) {
        LbAllocStatus cause=lb_alloc_domain_failure(domain);
        p->status=cause==LB_ALLOC_WORK_LIMIT?LB_PROCESS_WORK_LIMIT:
            cause==LB_ALLOC_NO_MEMORY?LB_PROCESS_NO_MEMORY:LB_PROCESS_LIMIT;
        return THE_NON_VALUE;
    }
    LbAlias *alias=memory; memset(alias,0,sizeof(*alias));
    Eterm reference=lb_reference_write(p->htop,engine->reference_counter);
    const LbOrdinaryRef *ref=lb_reference(reference);
    memcpy(alias->reference,ref->num,sizeof(alias->reference));
    alias->space=space; alias->owner=p; alias->active=1; alias->reply=reply;
    alias->next=space->aliases; space->aliases=alias;
    p->htop+=LB_REF_WORDS; ++engine->reference_counter;
    return reference;
}
Eterm lb_bif_alias_0(LbProcess *p,Eterm *args,const Uint *pc)
{ (void)args; (void)pc; return create_alias(p,0); }
Eterm lb_bif_alias_1(LbProcess *p,Eterm *args,const Uint *pc)
{
    (void)pc; LbCodeSpace *space=p->entry_module->space; Eterm tail=args[0];
    int reply=0; size_t count=0; p->last_native_cost=0;
    while(is_list(tail)) {
        if(!lb_work_domain_charge(space->domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
        if(++count>64) { p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE; }
        Eterm option=CAR(list_val(tail)); tail=CDR(list_val(tail)); ++p->last_native_cost;
        if(option==am_reply) reply=1;
        else if(option==am_explicit_unalias) { /* explicit cleanup is the default */ }
        else if(option==am_priority) { p->status=LB_PROCESS_REFUSED; return THE_NON_VALUE; }
        else { p->freason=LB_EXC_BADARG; return THE_NON_VALUE; }
    }
    if(!is_nil(tail)) { p->freason=LB_EXC_BADARG; return THE_NON_VALUE; }
    return create_alias(p,reply);
}
Eterm lb_bif_unalias_1(LbProcess *p,Eterm *args,const Uint *pc)
{
    (void)pc; LbCodeSpace *space=p->entry_module->space; p->last_native_cost=1;
    if(!lb_work_domain_charge(space->domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    LbAlias *alias=lb_alias_lookup(space,args[0],&p->last_native_cost);
    if(lb_alloc_domain_failure(space->domain)==LB_ALLOC_WORK_LIMIT) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    if(!alias || !alias->active || alias->owner!=p) return am_false;
    lb_alias_deactivate(alias); return am_true;
}
