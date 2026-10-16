/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#include "process_internal.h"
#include "map_runtime.h"
#include "flatmap.h"
typedef enum { LOOKUP_VALUE, LOOKUP_MEMBERSHIP } LookupKind;
static int require_map(LbProcess *p,Eterm map)
{
    int is_map=0; p->last_native_cost=0;
    if(!lb_work_domain_charge(p->entry_module->space->domain,1)) {
        p->status=LB_PROCESS_WORK_LIMIT; return 0;
    }
    ++p->last_native_cost;
    LbMapStatus status=lb_map_is_map(map,&is_map);
    if(status!=LB_MAP_OK) { p->status=LB_PROCESS_REFUSED; return 0; }
    if(!is_map) { p->fvalue=map; p->freason=LB_EXC_BADMAP; return 0; }
    return 1;
}
static Eterm lookup(LbProcess *p,Eterm *args,LookupKind kind)
{
    Eterm value;
    if(!require_map(p,args[1])) return THE_NON_VALUE;
    LbMapStatus status=lb_map_find(p,args[1],args[0],&value);
    if(status==LB_MAP_OK) return kind==LOOKUP_MEMBERSHIP?am_true:value;
    if(status==LB_MAP_MISSING) {
        if(kind==LOOKUP_MEMBERSHIP) return am_false;
        p->fvalue=args[0]; p->freason=LB_EXC_BADKEY; return THE_NON_VALUE;
    }
    if(status==LB_MAP_WORK_LIMIT) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    if(status==LB_MAP_NO_MEMORY) { p->status=LB_PROCESS_NO_MEMORY; return THE_NON_VALUE; }
    if(status==LB_MAP_LIMIT) { p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE; }
    p->status=LB_PROCESS_REFUSED; return THE_NON_VALUE;
}
Eterm lb_bif_map_get_2(LbProcess *p,Eterm *args,const BeamInstr *pc)
{ (void)pc; return lookup(p,args,LOOKUP_VALUE); }
Eterm lb_bif_is_map_key_2(LbProcess *p,Eterm *args,const BeamInstr *pc)
{ (void)pc; return lookup(p,args,LOOKUP_MEMBERSHIP); }
Eterm lb_bif_map_size_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)pc;
    if(!require_map(p,args[0])) return THE_NON_VALUE;
    /* Both validated native map roots store cardinality immediately after
     * their header. Internal HAMT nodes are not valid map operands. */
    Uint size=boxed_val(args[0])[1];
    if(size>(Uint)MAX_SMALL) { p->status=LB_PROCESS_REFUSED; return THE_NON_VALUE; }
    return make_small((Sint)size);
}
Eterm lb_bif_maps_from_list_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)pc;
    Eterm keys[LB_FLATMAP_MAX_SIZE],values[LB_FLATMAP_MAX_SIZE];
    size_t count=0,words,before;
    Eterm rest=args[0];
    p->last_native_cost=0;
    if(args!=p->reg) { p->status=LB_PROCESS_INVALID; return THE_NON_VALUE; }
    while(!is_nil(rest)) {
        if(!is_list(rest)) { p->freason=LB_EXC_BADARG; return THE_NON_VALUE; }
        if(count==LB_FLATMAP_MAX_SIZE) { p->status=LB_PROCESS_REFUSED; return THE_NON_VALUE; }
        if(!lb_work_domain_charge(p->entry_module->space->domain,1)) {
            p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE;
        }
        ++p->last_native_cost;
        Eterm pair=CAR(list_val(rest));
        if(!is_tuple(pair) || arityval(*tuple_val(pair))!=2) {
            p->freason=LB_EXC_BADARG; return THE_NON_VALUE;
        }
        ++count;
        rest=CDR(list_val(rest));
    }
    if(!lb_size_add(LB_FLATMAP_WORDS,count,&words) ||
       !lb_size_add(words,count?count+1:2,&words)) { p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE; }
    before=p->collections;
    LbMapStatus reserve=lb_map_reserve(p,words,MAX_REG);
    if(reserve!=LB_MAP_OK) {
        p->status=reserve==LB_MAP_WORK_LIMIT?LB_PROCESS_WORK_LIMIT:
            reserve==LB_MAP_NO_MEMORY?LB_PROCESS_NO_MEMORY:
            reserve==LB_MAP_LIMIT?LB_PROCESS_LIMIT:LB_PROCESS_REFUSED;
        return THE_NON_VALUE;
    }
    if(p->collections!=before) p->last_native_cost+=p->last_gc_cost;
    rest=p->reg[0];
    for(size_t i=0;i<count;++i) {
        Eterm pair=CAR(list_val(rest)); Eterm *fields=tuple_val(pair);
        keys[i]=fields[1]; values[i]=fields[2]; rest=CDR(list_val(rest));
    }
    LbMapStatus status=lb_map_create(p,keys,values,count,&p->construction_root);
    switch(status) {
    case LB_MAP_OK: {
        Eterm result=p->construction_root; p->construction_root=NIL; return result;
    }
    case LB_MAP_WORK_LIMIT:p->status=LB_PROCESS_WORK_LIMIT;break;
    case LB_MAP_NO_MEMORY:p->status=LB_PROCESS_NO_MEMORY;break;
    case LB_MAP_LIMIT:p->status=LB_PROCESS_LIMIT;break;
    case LB_MAP_UNSUPPORTED:p->status=LB_PROCESS_REFUSED;break;
    default:p->status=LB_PROCESS_INVALID;break;
    }
    return THE_NON_VALUE;
}
Eterm lb_bif_maps_merge_2(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)pc;
    Eterm keys[LB_FLATMAP_MAX_SIZE],values[LB_FLATMAP_MAX_SIZE],result=THE_NON_VALUE;
    size_t count,words,before;
    int is_map=0;
    p->last_native_cost=0;
    if(args!=p->reg) { p->status=LB_PROCESS_INVALID; return THE_NON_VALUE; }
    if(!lb_work_domain_charge(p->entry_module->space->domain,1)) {
        p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE;
    }
    ++p->last_native_cost;
    LbMapStatus status=lb_map_is_map(args[0],&is_map);
    if(status!=LB_MAP_OK) { p->status=LB_PROCESS_REFUSED; return THE_NON_VALUE; }
    if(!is_map) { p->fvalue=args[0]; p->freason=LB_EXC_BADMAP; return THE_NON_VALUE; }
    status=lb_map_is_map(args[1],&is_map);
    if(status!=LB_MAP_OK) { p->status=LB_PROCESS_REFUSED; return THE_NON_VALUE; }
    if(!is_map) { p->fvalue=args[1]; p->freason=LB_EXC_BADMAP; return THE_NON_VALUE; }
    if(!lb_is_flatmap(args[0]) || !lb_is_flatmap(args[1])) {
        p->status=LB_PROCESS_REFUSED; return THE_NON_VALUE;
    }
    count=(size_t)boxed_val(args[1])[1];
    status=lb_map_reservation_words(args[0],count,0,&words);
    if(status!=LB_MAP_OK) { p->status=LB_PROCESS_REFUSED; return THE_NON_VALUE; }
    before=p->collections;
    status=lb_map_reserve(p,words,MAX_REG);
    if(status!=LB_MAP_OK) {
        p->status=status==LB_MAP_WORK_LIMIT?LB_PROCESS_WORK_LIMIT:
            status==LB_MAP_NO_MEMORY?LB_PROCESS_NO_MEMORY:
            status==LB_MAP_LIMIT?LB_PROCESS_LIMIT:LB_PROCESS_REFUSED;
        return THE_NON_VALUE;
    }
    if(p->collections!=before) p->last_native_cost+=p->last_gc_cost;
    Eterm *source_map=boxed_val(p->reg[1]);
    Eterm *key_tuple=tuple_val(source_map[2]);
    for(size_t i=0;i<count;++i) { keys[i]=key_tuple[i+1]; values[i]=source_map[LB_FLATMAP_WORDS+i]; }
    status=lb_map_update_assoc(p,p->reg[0],keys,values,count,&result);
    switch(status) {
    case LB_MAP_OK:return result;
    case LB_MAP_WORK_LIMIT:p->status=LB_PROCESS_WORK_LIMIT;break;
    case LB_MAP_NO_MEMORY:p->status=LB_PROCESS_NO_MEMORY;break;
    case LB_MAP_LIMIT:p->status=LB_PROCESS_LIMIT;break;
    case LB_MAP_MISSING:p->status=LB_PROCESS_INVALID;break;
    default:p->status=LB_PROCESS_REFUSED;break;
    }
    return THE_NON_VALUE;
}
Eterm lb_bif_map_next_3(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)pc;
    p->last_native_cost=0;
    if(args!=p->reg) { p->status=LB_PROCESS_INVALID; return THE_NON_VALUE; }
    if(!lb_work_domain_charge(p->entry_module->space->domain,1)) {
        p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE;
    }
    ++p->last_native_cost;
    if(!is_small(args[0]) || signed_val(args[0])<0) {
        p->freason=LB_EXC_BADARG; return THE_NON_VALUE;
    }
    int is_map=0;
    LbMapStatus map_status=lb_map_is_map(args[1],&is_map);
    if(map_status!=LB_MAP_OK) { p->status=LB_PROCESS_REFUSED; return THE_NON_VALUE; }
    if(!is_map) { p->freason=LB_EXC_BADARG; return THE_NON_VALUE; }
    if(!lb_is_flatmap(args[1])) { p->status=LB_PROCESS_REFUSED; return THE_NON_VALUE; }
    Eterm mode=args[2];
    int iterator=0;
    if(is_atom(mode)) {
        LbBeamBytes name={0};
        if(lb_atoms_name(p->entry_module->space->atoms,mode,&name)!=LB_ATOM_OK ||
           name.size!=8 || memcmp(name.data,"iterator",8)) {
            p->freason=LB_EXC_BADARG; return THE_NON_VALUE;
        }
        iterator=1;
    } else if(!is_nil(mode) && !is_list(mode)) {
        p->freason=LB_EXC_BADARG; return THE_NON_VALUE;
    }
    size_t path=(size_t)signed_val(args[0]);
    Eterm *map=boxed_val(args[1]);
    size_t count=(size_t)map[1];
    if(path>count) { p->freason=LB_EXC_BADARG; return THE_NON_VALUE; }
    if(count>SIZE_MAX/(iterator?4u:5u)) { p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE; }
    size_t words=count*(iterator?4u:5u);
    uint64_t traversal_cost=lb_work_bulk_units(count);
    if(!lb_work_domain_charge(p->entry_module->space->domain,traversal_cost)) {
        p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE;
    }
    p->last_native_cost+=traversal_cost;
    size_t before=p->collections;
    if(words && !lb_heap_reserve(p,words,MAX_REG)) {
        LbAllocStatus allocation=lb_alloc_domain_failure(p->entry_module->space->domain);
        p->status=allocation==LB_ALLOC_WORK_LIMIT?LB_PROCESS_WORK_LIMIT:
            allocation==LB_ALLOC_NO_MEMORY?LB_PROCESS_NO_MEMORY:LB_PROCESS_LIMIT;
        return THE_NON_VALUE;
    }
    if(p->collections!=before) p->last_native_cost+=p->last_gc_cost;
    map=boxed_val(p->reg[1]);
    Eterm *keys=tuple_val(map[2]);
    Eterm *values=map+LB_FLATMAP_WORDS;
    Eterm result=iterator?am_none:p->reg[2];
    Eterm *heap=p->htop;
    for(size_t i=count;i>0;--i) {
        if(iterator) {
            heap[0]=make_arityval(3); heap[1]=keys[i]; heap[2]=values[i-1]; heap[3]=result;
            result=make_tuple(heap); heap+=4;
        } else {
            heap[0]=make_arityval(2); heap[1]=keys[i]; heap[2]=values[i-1];
            Eterm pair=make_tuple(heap); heap+=3;
            heap[0]=pair; heap[1]=result; result=make_list(heap); heap+=2;
        }
    }
    p->htop=heap;
    return result;
}
