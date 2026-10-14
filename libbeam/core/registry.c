/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1996-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * register.c atom-index hash/equality and process association; hash.c chained
 * buckets. Engine serialization replaces global registry/process lock retries.
 * Fixed bucket storage is an owned-profile adaptation, not a registry quota.
 */
#include "registry.h"
#include "process_internal.h"
#define REGISTRY_BUCKETS 64
struct LbRegisteredProcess {
    struct LbRegisteredProcess *next;
    Uint hash;
    Eterm name;
    LbProcess *process;
    struct LbRegisteredProcess *previous;
    void *port; /* native RegProc alternative, never admitted by this profile */
};
_Static_assert(offsetof(LbRegisteredProcess,name)==2*sizeof(Eterm),"native HashBucket prefix");
struct LbRegistry { LbRegisteredProcess *bucket[REGISTRY_BUCKETS]; size_t count; };
static size_t bucket(Eterm name)
{
    /* Native hash_get_slot Fibonacci mapping, selected 64-bucket profile. */
    Uint hash=atom_val(name); hash^=hash>>58;
    return (UINT64_C(11400714819323198485)*hash)>>58;
}
LbProcess *lb_registry_lookup(LbCodeSpace *space,Eterm name,Uint *cost)
{
    if(!space || !space->registry || !is_atom(name)) return NULL;
    for(LbRegisteredProcess *r=space->registry->bucket[bucket(name)];r;r=r->next) {
        if(!lb_work_domain_charge(space->domain,1)) return NULL;
        if(cost && *cost<UINTPTR_MAX) ++*cost;
        if(r->name==name) return r->process;
    }
    return NULL;
}
LbProcessStatus lb_registry_register(LbProcess *caller,Eterm name,Eterm pid)
{
    LbCodeSpace *space=caller->entry_module->space;
    if(!is_atom(name) || name==am_undefined || !is_internal_pid(pid)) return LB_PROCESS_EXCEPTION;
    LbProcess *target=lb_pid_lookup(space->pids,pid);
    if(!target || target->registered || lb_registry_lookup(space,name,&caller->last_native_cost)) return LB_PROCESS_EXCEPTION;
    if(lb_alloc_domain_failure(space->domain)==LB_ALLOC_WORK_LIMIT) return LB_PROCESS_WORK_LIMIT;
    LbRegistry *registry=space->registry; int fresh=!registry; void *memory;
    if(fresh) {
        if(lb_alloc_domain_allocate(space->domain,sizeof(*registry),&memory)!=LB_ALLOC_OK) return LB_PROCESS_NO_MEMORY;
        registry=memory; memset(registry,0,sizeof(*registry));
        caller->last_native_cost+=REGISTRY_BUCKETS/16;
    }
    if(lb_alloc_domain_allocate(space->domain,sizeof(LbRegisteredProcess),&memory)!=LB_ALLOC_OK) {
        if(fresh && lb_alloc_domain_release(space->domain,registry)!=LB_ALLOC_OK) abort();
        return LB_PROCESS_NO_MEMORY;
    }
    LbRegisteredProcess *r=memory;
    r->hash=atom_val(name); r->name=name; r->process=target; r->port=NULL;
    r->previous=NULL; r->next=registry->bucket[bucket(name)];
    if(r->next) r->next->previous=r;
    /* A published name is reachable by later independent invocations. */
    lb_work_budget_escape(caller->work_active); lb_work_budget_escape(target->work_birth);
    /* No allocation or interpreter re-entry after either association publishes. */
    registry->bucket[bucket(name)]=r; ++registry->count;
    target->registered=r; space->registry=registry;
    return LB_PROCESS_READY;
}
LbProcessStatus lb_registry_names(LbProcess *p,Eterm *out)
{
    LbCodeSpace *space=p->entry_module->space;
    LbRegistry *registry=space->registry;
    size_t count=registry?registry->count:0,words,need,work=1;
    *out=THE_NON_VALUE; p->last_native_cost=0;
    if(!lb_size_mul(count,2,&words) || !lb_size_add(words,p->heap_reserved,&need)) return LB_PROCESS_LIMIT;
    if(registry && (!lb_size_mul(count,3,&work) || !lb_size_add(work,1+REGISTRY_BUCKETS,&work))) return LB_PROCESS_LIMIT;
    if(!lb_work_domain_charge(space->domain,work)) return LB_PROCESS_WORK_LIMIT;
    p->last_native_cost=work;
    if(!count) { *out=NIL; return LB_PROCESS_READY; }
    size_t before=p->collections;
    if(!lb_heap_reserve(p,need,0)) {
        LbAllocStatus cause=lb_alloc_domain_failure(space->domain);
        return cause==LB_ALLOC_WORK_LIMIT?LB_PROCESS_WORK_LIMIT:
            cause==LB_ALLOC_NO_MEMORY?LB_PROCESS_NO_MEMORY:LB_PROCESS_LIMIT;
    }
    if(p->collections!=before) p->last_native_cost+=p->last_gc_cost;
    /* GC moves guest terms, never these serialized registry associations or
     * their atom names. No allocation/re-entry after reserving the full list. */
    Eterm result=NIL; size_t visited=0;
    for(size_t i=0;i<REGISTRY_BUCKETS;++i) for(LbRegisteredProcess *r=registry->bucket[i];r;r=r->next) {
        if(visited==count) abort();
        result=CONS(p->htop,r->name,result); p->htop+=2; ++visited;
    }
    if(visited!=count) abort();
    *out=result; return LB_PROCESS_READY;
}
void lb_registry_remove(LbProcess *p)
{
    LbRegisteredProcess *r=p->registered;
    if(!r) return;
    LbCodeSpace *space=p->entry_module->space; LbRegistry *registry=space->registry;
    if(r->previous) r->previous->next=r->next;
    else {
        LbRegisteredProcess **head=&registry->bucket[bucket(r->name)];
        if(*head!=r) abort(); *head=r->next;
    }
    if(r->next) r->next->previous=r->previous;
    if(!registry->count) abort(); p->registered=NULL; --registry->count;
    if(lb_alloc_domain_release(space->domain,r)!=LB_ALLOC_OK) abort();
    if(!registry->count) {
        space->registry=NULL;
        if(lb_alloc_domain_release(space->domain,registry)!=LB_ALLOC_OK) abort();
    }
}
