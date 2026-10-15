/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 2013-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Native erl_ptab.c/.h modern free-ID ring and cache-line index permutation
 * (beam/ snapshot at 40166b36). Engine serialization replaces PTAB atomics and
 * locks. No processes/0 snapshots, legacy allocator or thread-progress readers.
 * Removal precedes physical process destruction. Exhausted generations retire
 * their slots rather than wrapping into still-retained old PID values.
 */
#include "process_internal.h"
#include <stdlib.h>
#define CACHE_BYTES 64u
#define IDS_PER_LINE (CACHE_BYTES/sizeof(Eterm))
#define MAX_CAPACITY (1u<<20)
#define ID_DATA_MASK (UINT64_MAX>>_TAG_IMMED1_SIZE)
struct LbPidTable {
    LbAllocDomain *domain;
    LbCodeSpace *space;
    LbPidEntry **slots;
    Eterm *free_ids;
    size_t capacity,count,retired;
    Uint mask,line_mask,item_mask;
    unsigned line_shift,item_shift;
    uint32_t allocated_index,freed_index;
};
static unsigned bits(size_t n)
{
    unsigned count=0; while(n) { ++count; n>>=1; } return count;
}
/* These are the native data2pix/pixdata2data transforms. Low PID data bits
 * contain the actual slot index; generation bits are unchanged. */
static size_t slot_index(const LbPidTable *t,Uint data)
{
    return ((data&t->line_mask)<<t->line_shift)+((data>>t->item_shift)&t->item_mask);
}
static Eterm make_id(const LbPidTable *t,Uint data)
{
    Uint pixdata=(data&~t->mask)|slot_index(t,data);
    return (pixdata<<_TAG_IMMED1_SIZE)|_TAG_IMMED1_PID;
}
static Uint id_data(const LbPidTable *t,Eterm id)
{
    Uint pixdata=id>>_TAG_IMMED1_SIZE;
    return (pixdata&~t->mask)|((pixdata>>t->line_shift)&t->line_mask)|((pixdata&t->item_mask)<<t->item_shift);
}
LbPidStatus lb_pid_table_create(LbCodeSpace *space,size_t wanted,LbPidTable **out)
{
    if(!out) return LB_PID_INVALID;
    *out=NULL;
    if(!space || !wanted || wanted>MAX_CAPACITY) return LB_PID_INVALID;
    LbAllocDomain *domain=space->domain;
    size_t capacity=IDS_PER_LINE;
    while(capacity<wanted) capacity*=2;
    size_t bytes=sizeof(LbPidTable)+CACHE_BYTES-1+capacity*(sizeof(LbPidEntry *)+sizeof(Eterm));
    void *memory;
    if(lb_alloc_domain_allocate(domain,bytes,&memory)!=LB_ALLOC_OK) return LB_PID_NO_MEMORY;
    LbPidTable *t=memory; memset(t,0,sizeof(*t)); t->domain=domain; t->space=space; t->capacity=capacity;
    t->slots=(LbPidEntry **)(((uintptr_t)(t+1)+CACHE_BYTES-1)&~(uintptr_t)(CACHE_BYTES-1));
    t->free_ids=(Eterm *)(t->slots+capacity);
    t->mask=capacity-1; t->line_mask=capacity/IDS_PER_LINE-1;
    t->line_shift=bits(IDS_PER_LINE-1); t->item_shift=bits(t->line_mask);
    t->item_mask=(capacity>>t->item_shift)-1;
    t->allocated_index=t->freed_index=UINT32_MAX;
    for(size_t i=0;i<capacity;++i) { t->slots[i]=NULL; t->free_ids[slot_index(t,i)]=i; }
    *out=t; return LB_PID_OK;
}
LbPidStatus lb_pid_table_destroy(LbPidTable *t)
{
    if(!t) return LB_PID_INVALID;
    if(t->count) return LB_PID_BUSY;
    if(lb_alloc_domain_release(t->domain,t)!=LB_ALLOC_OK) abort();
    return LB_PID_OK;
}
LbPidStatus lb_pid_insert(LbPidTable *t,LbPidEntry *entry,LbProcess *process)
{
    if(!t || !entry || !process || entry!=&process->identity ||
       process->entry_module->space!=t->space || entry->owner || is_internal_pid(entry->id)) return LB_PID_INVALID;
    if(t->count==t->capacity-t->retired) return LB_PID_FULL;
    /* Charge each visited slot, not the entire capacity on a fast insertion.
     * Keep the cursor private until success: exhaustion cannot skip identities
     * or mutate the ring, even after traversing retired/occupied positions. */
    uint32_t cursor=t->allocated_index;
    for(size_t tried=0;tried<t->capacity;++tried) {
        if(!lb_work_domain_charge(t->domain,1)) return LB_PID_NO_MEMORY;
        size_t index=slot_index(t,++cursor);
        Uint data=t->free_ids[index];
        if(data==ID_DATA_MASK) continue;
        size_t slot=slot_index(t,data); if(t->slots[slot]) abort();
        t->allocated_index=cursor; t->free_ids[index]=ID_DATA_MASK;
        *entry=(LbPidEntry){t,process,make_id(t,data)};
        t->slots[slot]=entry; ++t->count; return LB_PID_OK;
    }
    abort(); /* count/free-ring invariant */
}
LbPidStatus lb_pid_remove(LbPidTable *t,LbPidEntry *entry)
{
    if(!t || !entry || entry->owner!=t) return LB_PID_INVALID;
    size_t slot=(entry->id>>_TAG_IMMED1_SIZE)&t->mask;
    if(!t->count || t->slots[slot]!=entry) return LB_PID_INVALID;
    Uint data=id_data(t,entry->id);
    t->slots[slot]=NULL; --t->count;
    if(data>=ID_DATA_MASK-t->capacity) {
        ++t->retired; /* native next-generation increment would wrap/skip invalid */
    } else {
        data+=t->capacity;
        for(size_t tried=0;;++tried) {
            if(tried==t->capacity) abort();
            size_t index=slot_index(t,++t->freed_index);
            if(t->free_ids[index]==ID_DATA_MASK) { t->free_ids[index]=data; break; }
        }
    }
    entry->owner=NULL; entry->process=NULL;
    return LB_PID_OK;
}
LbProcess *lb_pid_lookup(LbPidTable *t,Eterm id)
{
    if(!t || (id&_TAG_IMMED1_MASK)!=_TAG_IMMED1_PID) return NULL;
    LbPidEntry *entry=t->slots[(id>>_TAG_IMMED1_SIZE)&t->mask];
    return entry && entry->id==id?entry->process:NULL;
}
int lb_pid_components(LbPidTable *t,Eterm id,Uint *number,Uint *serial)
{
    if(!t || !number || !serial || !is_internal_pid(id)) return 0;
    Uint data=id_data(t,id);
    *number=data&((UINT64_C(1)<<28)-1); *serial=data>>28;
    return 1;
}
int lb_pid_from_components(LbPidTable *t,Uint number,Uint serial,Eterm *out)
{
    if(!t || !out || number>=(UINT64_C(1)<<28) || serial>UINT32_MAX) return 0;
    *out=make_id(t,(serial<<28)|number);
    return 1;
}
size_t lb_pid_count(const LbPidTable *t) { return t?t->count:0; }
size_t lb_pid_capacity(const LbPidTable *t) { return t?t->capacity:0; }
LbPidStatus lb_pid_make_list(const LbPidTable *t,Eterm **heap,size_t count,Eterm *out)
{
    if(!heap || !*heap || !out || (t && count<t->count)) return LB_PID_INVALID;
    Eterm list=NIL; size_t made=0;
    if(t) for(size_t i=0;i<t->capacity;++i) if(t->slots[i]) {
        list=CONS(*heap,t->slots[i]->id,list); *heap+=2; ++made;
    }
    if(made!=count) return LB_PID_INVALID;
    *out=list; return LB_PID_OK;
}
