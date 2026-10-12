/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1996-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#include "process_internal.h"
#include "fun.h"
#include "flatmap.h"
#include "big.h"

/* beam_common.c is_function2 and erl_bif_op.c erl_is_function, restricted to
 * owned local funs. The cold guard retains native badarg pending flags; it
 * performs no allocation and cannot conceal an infrastructure failure. */
int lb_fun_is_arity(Eterm value,Uint arity)
{
    const LbFun *fun=lb_fun(value); return fun && lb_fun_arity(fun)==arity;
}
Eterm lb_fun_test_arity(LbProcess *p,Eterm value,Eterm argument)
{
    Sint arity;
    if(is_small(argument)) {
        arity=signed_val(argument);
        if(arity<0) goto error;
    } else if(lb_is_big(argument) && (*boxed_val(argument)&_HEADER_SUBTAG_MASK)==POS_BIG_SUBTAG) arity=-1;
    else goto error;
    return lb_fun_is_arity(value,(Uint)arity)?am_true:am_false;
error:
    p->freason=LB_EXC_BADARG; return THE_NON_VALUE;
}
Eterm lb_bif_is_function_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)p; (void)pc; return lb_fun(args[0])?am_true:am_false;
}
Eterm lb_bif_is_function_2(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)pc; return lb_fun_test_arity(p,args[0],args[1]);
}
static LbAllocStatus retain(LbCodeSpace *space,LbFunLease **leases,const LbFunEntry *entry)
{
    if(!space || !leases || !entry || !entry->owner || entry->owner->space!=space ||
       !entry->owner->published || !entry->dispatch.addresses[0] || entry->pend_purge_address) return LB_ALLOC_INVALID;
    for(LbFunLease *lease=*leases;lease;lease=lease->next) {
        if(!lb_work_domain_charge(space->domain,1)) return LB_ALLOC_WORK_LIMIT;
        if(lease->entry->owner==entry->owner) return LB_ALLOC_OK;
    }
    if(entry->owner->users==SIZE_MAX) return LB_ALLOC_NO_MEMORY;
    void *memory;
    if(lb_alloc_domain_allocate(space->domain,sizeof(LbFunLease),&memory)!=LB_ALLOC_OK) return LB_ALLOC_NO_MEMORY;
    LbFunLease *lease=memory; *lease=(LbFunLease){*leases,entry,0};
    ++entry->owner->users; *leases=lease; return LB_ALLOC_OK;
}
LbAllocStatus lb_fun_retain(LbProcess *p,const LbFunEntry *entry)
{
    if(!p || !p->entry_module) return LB_ALLOC_INVALID;
    return retain(p->entry_module->space,&p->fun_leases,entry);
}
/* Prepare code ownership before copying a valid owned term into this process.
 * Term storage is not mutated; a failed preparation remains wholly owned by
 * the unpublished destination and is reclaimed by its destructor. */
static LbAllocStatus retain_roots(LbCodeSpace *space,LbFunLease **leases,Eterm term,Uint *cost,int pin)
{
    Eterm local[64],*todo=local; size_t count=1,capacity=64;
    LbAllocDomain *domain=space->domain;
    LbAllocStatus status=LB_ALLOC_OK; todo[0]=term;
    while(count) {
        if(!lb_work_domain_charge(domain,1)) { status=LB_ALLOC_WORK_LIMIT; break; }
        Eterm value=todo[--count]; const Eterm *children=NULL; size_t n=0;
        ++*cost;
        if(is_list(value)) { children=list_val(value); n=2; }
        else if(is_boxed(value)) {
            const Eterm *pointer=boxed_val(value);
            if(is_arity_value(*pointer)) { children=pointer+1; n=arityval(*pointer); }
            else if(*pointer==LB_FLATMAP_HEADER) {
                (void)lb_flatmap_words(pointer); children=pointer+2; n=pointer[1]+1;
            } else if(pin && *pointer==LB_HEADER_SUB_BITS) {
                if(!lb_bitstring_pin(value)) { status=LB_ALLOC_INVALID; break; }
            } else if((*pointer&_TAG_HEADER_MASK)==FUN_SUBTAG) {
                const LbFun *fun=lb_fun(value);
                status=retain(space,leases,fun->entry.fun); if(status!=LB_ALLOC_OK) break;
                children=fun->env; n=lb_fun_free(fun);
            }
        }
        if(n>capacity-count) {
            size_t next=capacity; void *memory;
            while(next-count<n) {
                if(next>SIZE_MAX/2/sizeof(Eterm)) { status=LB_ALLOC_NO_MEMORY; break; }
                next*=2;
            }
            if(status!=LB_ALLOC_OK) break;
            status=lb_alloc_domain_allocate(domain,next*sizeof(Eterm),&memory);
            if(status!=LB_ALLOC_OK) break;
            Eterm *grown=memory; memcpy(grown,todo,count*sizeof(Eterm));
            if(todo!=local) lb_release(domain,todo);
            todo=grown; capacity=next;
        }
        if(n) {
            if(!lb_work_domain_charge(domain,lb_work_bulk_units(n))) { status=LB_ALLOC_WORK_LIMIT; break; }
            memcpy(todo+count,children,n*sizeof(Eterm));
        }
        count+=n;
    }
    if(todo!=local) lb_release(domain,todo); return status;
}
LbAllocStatus lb_fun_retain_roots(LbCodeSpace *space,LbFunLease **leases,Eterm term,Uint *cost)
{ return retain_roots(space,leases,term,cost,0); }
LbAllocStatus lb_fun_retain_message_roots(LbCodeSpace *space,LbFunLease **leases,Eterm term,Uint *cost)
{ return retain_roots(space,leases,term,cost,1); }
LbAllocStatus lb_fun_retain_term(LbProcess *p,Eterm term,Uint *cost)
{
    return lb_fun_retain_roots(p->entry_module->space,&p->fun_leases,term,cost);
}
static void release(LbCodeSpace *space,LbFunLease *lease)
{
    LbCodeModule *owner=lease->entry->owner;
    if(!owner->users) abort();
    --owner->users; lb_release(space->domain,lease);
}
void lb_fun_release_prefix(LbCodeSpace *space,LbFunLease **leases,LbFunLease *stop)
{
    while(*leases!=stop) {
        LbFunLease *lease=*leases;
        if(!lease) abort(); /* the saved prefix boundary belongs to this list */
        *leases=lease->next; release(space,lease);
    }
}
void lb_fun_release_roots(LbCodeSpace *space,LbFunLease **leases)
{
    lb_fun_release_prefix(space,leases,NULL);
}
void lb_fun_leases_clear(LbProcess *p)
{
    lb_fun_release_roots(p->entry_module->space,&p->fun_leases);
}
void lb_fun_marks_clear(LbProcess *p)
{
    for(LbFunLease *lease=p->fun_leases;lease;lease=lease->next) lease->marked=0;
}
void lb_fun_mark(LbProcess *p,const LbFunEntry *entry)
{
    for(LbFunLease *lease=p->fun_leases;lease;lease=lease->next)
        if(lease->entry->owner==entry->owner) { lease->marked=1; return; }
    abort(); /* no code pointer may outlive its defining-module lease */
}
static int contains_code(LbCodeModule *module,Uint address)
{
    return !(address%sizeof(BeamInstr)) && address>=(Uint)module->words && address<(Uint)(module->words+module->word_count);
}
Uint lb_fun_mark_code(LbProcess *p)
{
    if(!p->running && p->status!=LB_PROCESS_READY && p->status!=LB_PROCESS_YIELDED) return 0;
    Uint cost=0;
    for(LbFunLease *lease=p->fun_leases;lease;lease=lease->next) {
        LbCodeModule *module=lease->entry->owner; ++cost;
        if(contains_code(module,(Uint)p->i)) { lease->marked=1; continue; }
        /* Continuations are untagged native code words among initialized Y
         * roots. Conservatively retain a module for any matching stack word;
         * a dead closure must not free code underneath a suspended call. */
        for(const Eterm *slot=p->stop;slot<p->hend;++slot) {
            ++cost;
            if(contains_code(module,*slot)) { lease->marked=1; break; }
        }
    }
    return cost/16+(cost%16!=0);
}
LbAllocStatus lb_fun_prepare_instruction(LbProcess *p,const BeamInstr *pc,Uint *cost)
{
    *cost=0; LbCodeSpace *space=p->entry_module->space;
    if(!space->has_fun_literals) return LB_ALLOC_OK;
    Uint address=(Uint)pc;
    for(LbCodeModule *module=space->modules;module;module=module->next) {
        if(!lb_work_domain_charge(space->domain,1)) return LB_ALLOC_WORK_LIMIT;
        ++*cost;
        if(!contains_code(module,address)) continue;
        size_t index=(size_t)(pc-module->words);
        for(const LbLiteralFunUse *use=module->literal_fun_uses;use;use=use->next) {
            if(!lb_work_domain_charge(space->domain,1)) return LB_ALLOC_WORK_LIMIT;
            ++*cost;
            if(use->pc!=index) continue;
            LbAllocStatus status=lb_fun_retain(p,use->entry);
            if(status!=LB_ALLOC_OK) return status;
        }
        break;
    }
    return LB_ALLOC_OK;
}
/* Native beam_common.c call_fun's local loaded-fun and badfun/badarity
 * paths. Unloaded/purging/export fun effects have no admission: all accepted
 * entries carry a defining-module lease and a verified loaded code address. */
const BeamInstr *lb_fun_call(LbProcess *p,unsigned arity)
{
    p->last_native_cost=0;
    if(arity>MAX_ARG) { p->status=LB_PROCESS_INVALID; return NULL; }
    Eterm value=p->reg[arity]; const LbFun *fun=lb_fun(value);
    if(!fun) { p->current=NULL; p->freason=LB_EXC_BADFUN; p->fvalue=value; return NULL; }
    const LbFunEntry *entry=fun->entry.fun; int retained=0;
    for(LbFunLease *lease=p->fun_leases;lease;lease=lease->next) {
        if(!lb_work_domain_charge(p->entry_module->space->domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return NULL; }
        ++p->last_native_cost;
        if(lease->entry->owner==entry->owner) { retained=1; break; }
    }
    if(!retained || entry->owner->space!=p->entry_module->space ||
       !entry->owner->published || entry->pend_purge_address || !entry->dispatch.addresses[0]) {
        p->status=LB_PROCESS_REFUSED; return NULL;
    }
    if(lb_fun_arity(fun)==arity) {
        unsigned count=(unsigned)lb_fun_free(fun);
        if(arity+count>MAX_ARG || entry->arity!=(int)arity) { p->status=LB_PROCESS_INVALID; return NULL; }
        int found=0;
        for(size_t i=0;i<lb_code_module_function_count(entry->owner);++i) {
            if(!lb_work_domain_charge(p->entry_module->space->domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return NULL; }
            const BeamInstr *info=(const BeamInstr *)entry->owner->words[i]; ++p->last_native_cost;
            if(info+sizeof(LbCodeInfo)/sizeof(BeamInstr)==entry->dispatch.addresses[0]) {
                found=info[sizeof(LbCodeInfo)/sizeof(BeamInstr)-1]==arity+count; break;
            }
        }
        if(!found) { p->status=LB_PROCESS_INVALID; return NULL; }
        if(!lb_work_domain_charge(p->entry_module->space->domain,count+lb_work_bulk_units(count))) {
            p->status=LB_PROCESS_WORK_LIMIT; return NULL;
        }
        for(unsigned i=0;i<count;++i) p->reg[arity+i]=fun->env[i];
        return entry->dispatch.addresses[0];
    }
    /* Build the native {Fun,Args} badarity payload in one fallible reservation.
     * Both the argument registers and fun register survive moving collection. */
    if(!lb_work_domain_charge(p->entry_module->space->domain,arity+lb_work_bulk_units(2*(uint64_t)arity+3))) {
        p->status=LB_PROCESS_WORK_LIMIT; return NULL;
    }
    size_t before=p->collections,need;
    if(!lb_size_add(2*(size_t)arity+3,p->heap_reserved,&need) || !lb_heap_reserve(p,need,arity+1)) {
        p->status=LB_PROCESS_NO_MEMORY; return NULL;
    }
    if(before!=p->collections) p->last_native_cost+=p->last_gc_cost;
    Eterm args=NIL; value=p->reg[arity];
    for(unsigned i=arity;i;--i) { args=CONS(p->htop,p->reg[i-1],args); p->htop+=2; }
    p->freason=LB_EXC_BADARITY; p->fvalue=TUPLE2(p->htop,value,args); p->htop+=3;
    return NULL;
}
void lb_fun_sweep(LbProcess *p)
{
    LbFunLease **link=&p->fun_leases;
    while(*link) {
        LbFunLease *lease=*link;
        if(lease->marked) link=&lease->next;
        else { *link=lease->next; release(p->entry_module->space,lease); }
    }
}
