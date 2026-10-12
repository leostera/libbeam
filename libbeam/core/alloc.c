/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#include "alloc_internal.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Owned fallible backing, not a term format or malloc/RSS carrier estimate.
 * Preserve unknown-pointer checking: release scans owned nodes before reading
 * their header. Every simultaneously retained block counts through physical free. */
union LbBlock {
    struct { LbBlock *next; size_t charge; } info;
    max_align_t alignment;
};
static void *system_allocate(void *context,size_t size)
{ (void)context; return malloc(size); }
static void system_release(void *context,void *allocation)
{ (void)context; free(allocation); }
static LbAllocStatus failed(LbAllocDomain *domain,LbAllocStatus status)
{ domain->failure=status; return status; }
LbAllocStatus lb_alloc_domain_create(const LbSystemAllocator *allocator,LbAllocDomain **out)
{
    const LbSystemAllocator defaults={NULL,system_allocate,system_release};
    LbAllocDomain *domain;
    if(!out) return LB_ALLOC_INVALID;
    *out=NULL; if(!allocator) allocator=&defaults;
    if(!allocator->allocate || !allocator->release) return LB_ALLOC_INVALID;
    domain=allocator->allocate(allocator->context,sizeof(*domain));
    if(!domain) return LB_ALLOC_NO_MEMORY;
    memset(domain,0,sizeof(*domain)); domain->system=*allocator; domain->limit=SIZE_MAX;
    *out=domain; return LB_ALLOC_OK;
}
LbAllocStatus lb_alloc_scope_init(LbAllocDomain *parent,LbAllocDomain *scope,size_t limit)
{
    if(!parent || !scope || parent==scope || !limit || !parent->system.allocate || scope->system.allocate)
        return LB_ALLOC_INVALID;
    if(parent->children==SIZE_MAX) return LB_ALLOC_LIMIT;
    scope->system=parent->system; scope->parent=parent; scope->limit=limit;
    ++parent->children; return LB_ALLOC_OK;
}
LbAllocStatus lb_alloc_domain_allocate(LbAllocDomain *domain,size_t size,void **out)
{
    LbBlock *block; size_t charge;
    if(!out) return LB_ALLOC_INVALID;
    *out=NULL; if(!domain || !size || !domain->system.allocate) return LB_ALLOC_INVALID;
    /* Preserve the raw unbounded bootstrap API's impossible-size OOM result. */
    if(size>SIZE_MAX-sizeof(*block)) return failed(domain,domain->parent?LB_ALLOC_LIMIT:LB_ALLOC_NO_MEMORY);
    charge=sizeof(*block)+size;
    if(charge>domain->limit-domain->current) return failed(domain,LB_ALLOC_LIMIT);
    block=domain->system.allocate(domain->system.context,charge);
    if(!block) return failed(domain,LB_ALLOC_NO_MEMORY);
    block->info.next=domain->blocks; block->info.charge=charge; domain->blocks=block;
    domain->current+=charge; if(domain->current>domain->peak) domain->peak=domain->current;
    *out=block+1; return LB_ALLOC_OK;
}
LbAllocStatus lb_alloc_domain_release(LbAllocDomain *domain,void *allocation)
{
    LbBlock **link,*block;
    if(!domain || !allocation) return LB_ALLOC_INVALID;
    for(link=&domain->blocks;(block=*link)!=NULL;link=&block->info.next) {
        if((void *)(block+1)==allocation) {
            size_t charge=block->info.charge;
            *link=block->info.next;
            if(charge>domain->current) abort();
            domain->system.release(domain->system.context,block);
            domain->current-=charge; return LB_ALLOC_OK;
        }
    }
    return LB_ALLOC_INVALID;
}
LbAllocStatus lb_alloc_domain_destroy(LbAllocDomain *domain)
{
    LbSystemAllocator system;
    if(!domain || !domain->system.allocate) return LB_ALLOC_INVALID;
    if(domain->blocks || domain->children) return LB_ALLOC_BUSY;
    if(domain->current) abort();
    if(domain->parent) {
        if(!domain->parent->children) abort(); --domain->parent->children;
        memset(domain,0,sizeof(*domain)); return LB_ALLOC_OK;
    }
    system=domain->system; system.release(system.context,domain); return LB_ALLOC_OK;
}
LbAllocUsage lb_alloc_domain_usage(const LbAllocDomain *domain)
{ return (LbAllocUsage){domain->current,domain->peak,domain->limit}; }
void lb_alloc_domain_begin_operation(LbAllocDomain *domain) { domain->failure=LB_ALLOC_OK; }
LbAllocStatus lb_alloc_domain_failure(const LbAllocDomain *domain) { return domain->failure; }
