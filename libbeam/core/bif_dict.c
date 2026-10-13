/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1997-2025. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Native process-dictionary buckets, with fallible publish-last mutation and
 * an atom/small-key, fixed-bucket profile instead of incremental rehashing.
 */
#include "process_internal.h"
static int charge(LbProcess *p,Uint work)
{
    if(!lb_work_domain_charge(p->entry_module->space->domain,work)) { p->status=LB_PROCESS_WORK_LIMIT; return 0; }
    p->last_native_cost+=work; return 1;
}
static int begin(LbProcess *p,Eterm key)
{
    p->last_native_cost=0;
    if(!charge(p,1)) return 0;
    if(!is_atom(key) && !is_small(key)) { p->status=LB_PROCESS_REFUSED; return 0; }
    return 1;
}
static unsigned index_of(Eterm key)
{ return (unsigned)((is_small(key)?key>>_TAG_IMMED1_SIZE:atom_val(key))&(LB_PD_SLOTS-1)); }
/* Return a stable bucket's direct pair slot or a collision cons's CAR slot.
 * Any heap slot must be recomputed after collection. Never return a borrowed
 * slot across a fallible reserve or silently turn resource denial into absence. */
static int find(LbProcess *p,LbProcDict *dict,Eterm key,Eterm **found)
{
    *found=NULL; if(!dict) return 1;
    Eterm *slot=&dict->data[index_of(key)],bucket=*slot;
    if(is_tuple(bucket)) {
        if(!charge(p,1)) return 0;
        if(tuple_val(bucket)[1]==key) *found=slot;
    } else for(Eterm tail=bucket;is_list(tail);tail=CDR(list_val(tail))) {
        if(!charge(p,1)) return 0;
        Eterm *cell=list_val(tail); ASSERT(is_tuple(CAR(cell)));
        if(tuple_val(CAR(cell))[1]==key) { *found=cell; break; }
    }
    return 1;
}
Eterm lb_bif_get_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    Eterm *found; (void)pc;
    if(!begin(p,args[0]) || !find(p,p->dictionary,args[0],&found)) return THE_NON_VALUE;
    return found?tuple_val(*found)[2]:am_undefined;
}
static Eterm memory_failure(LbProcess *p)
{
    LbAllocStatus cause=lb_alloc_domain_failure(p->entry_module->space->domain);
    p->status=cause==LB_ALLOC_WORK_LIMIT?LB_PROCESS_WORK_LIMIT:cause==LB_ALLOC_LIMIT?LB_PROCESS_LIMIT:LB_PROCESS_NO_MEMORY;
    return THE_NON_VALUE;
}
Eterm lb_bif_put_2(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    Eterm key=args[0],*found; LbProcDict *dict=p->dictionary,*staged=NULL;
    LbAllocDomain *domain=p->entry_module->space->domain; (void)pc;
    if(!begin(p,key) || !find(p,dict,key,&found)) return THE_NON_VALUE;
    /* Native immediate replacement cannot introduce a younger heap pointer. */
    if(found && is_immed(args[1])) {
        Eterm *pair=tuple_val(*found),old=pair[2]; pair[2]=args[1]; return old;
    }
    Eterm bucket=dict?dict->data[index_of(key)]:NIL;
    size_t words=found?3:is_nil(bucket)?3:is_tuple(bucket)?7:5,need;
    if(!lb_size_add(words,p->heap_reserved,&need)) { p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE; }
    if(!charge(p,(Uint)words+LB_PD_SLOTS+4)) return THE_NON_VALUE;
    if(!dict) {
        void *memory;
        if(lb_alloc_domain_allocate(domain,sizeof(*dict),&memory)!=LB_ALLOC_OK) return memory_failure(p);
        staged=dict=memory; dict->sizeMask=2*LB_PD_SLOTS-1; dict->usedSlots=dict->arraySize=LB_PD_SLOTS;
        dict->splitPosition=0; dict->numElements=0;
        for(unsigned i=0;i<LB_PD_SLOTS;++i) dict->data[i]=NIL;
    }
    size_t collections=p->collections;
    if(!lb_heap_reserve(p,need,MAX_REG)) {
        if(staged) lb_release(domain,staged);
        return memory_failure(p);
    }
    if(collections!=p->collections) p->last_native_cost+=p->last_gc_cost;
    /* Args are process registers, reloaded after GC. No mutation precedes the
     * second lookup, which may still run out of work. Staged arrays are all NIL. */
    if(!find(p,dict,key,&found)) { if(staged) lb_release(domain,staged); return THE_NON_VALUE; }
    Eterm old=found?tuple_val(*found)[2]:am_undefined,*hp=p->htop;
    Eterm pair=TUPLE2(hp,key,args[1]); hp+=3;
    Eterm *slot=&dict->data[index_of(key)]; bucket=*slot;
    if(found) *found=pair;
    else {
        if(is_nil(bucket)) *slot=pair;
        else if(is_tuple(bucket)) {
            Eterm tail=CONS(hp,bucket,NIL); hp+=2; *slot=CONS(hp,pair,tail); hp+=2;
        } else { *slot=CONS(hp,pair,bucket); hp+=2; }
        ++dict->numElements;
    }
    p->htop=hp; p->dictionary=dict; return old;
}
Eterm lb_bif_erase_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    Eterm key=args[0]; (void)pc;
    if(!begin(p,key)) return THE_NON_VALUE;
    LbProcDict *dict=p->dictionary; if(!dict) return am_undefined;
    Eterm *slot=&dict->data[index_of(key)],bucket=*slot;
    if(is_tuple(bucket)) {
        if(!charge(p,1)) return THE_NON_VALUE;
        Eterm *pair=tuple_val(bucket); if(pair[1]!=key) return am_undefined;
        Eterm old=pair[2]; *slot=NIL; --dict->numElements; return old;
    }
    for(Eterm *link=slot;is_list(*link);link=&CDR(list_val(*link))) {
        if(!charge(p,1)) return THE_NON_VALUE;
        Eterm *cell=list_val(*link),*pair=tuple_val(CAR(cell));
        if(pair[1]==key) {
            Eterm old=pair[2]; *link=CDR(cell); --dict->numElements;
            if(is_list(*slot) && is_nil(CDR(list_val(*slot)))) *slot=CAR(list_val(*slot));
            return old;
        }
    }
    return am_undefined;
}
