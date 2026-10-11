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
    r->next=registry->bucket[bucket(name)];
    /* No allocation or interpreter re-entry after either association publishes. */
    registry->bucket[bucket(name)]=r; ++registry->count;
    target->registered=r; space->registry=registry;
    return LB_PROCESS_READY;
}
void lb_registry_remove(LbProcess *p)
{
    LbRegisteredProcess *r=p->registered;
    if(!r) return;
    LbCodeSpace *space=p->entry_module->space; LbRegistry *registry=space->registry;
    LbRegisteredProcess **link=&registry->bucket[bucket(r->name)];
    while(*link && *link!=r) link=&(*link)->next;
    if(*link!=r || !registry->count) abort();
    *link=r->next; p->registered=NULL; --registry->count;
    if(lb_alloc_domain_release(space->domain,r)!=LB_ALLOC_OK) abort();
    if(!registry->count) {
        space->registry=NULL;
        if(lb_alloc_domain_release(space->domain,registry)!=LB_ALLOC_OK) abort();
    }
}
