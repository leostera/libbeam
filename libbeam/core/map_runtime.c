/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 2014-2025. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Bounded native flatmap consumers and constructors; no HAMT implementation.
 */
#include "process_internal.h"
#include "map_runtime.h"
#include "flatmap.h"
#include "map_hamt.h"
#include "binary_match.h"
#include "term_compare.h"

typedef struct { Eterm key,value; } LbMapPair;
typedef struct { Eterm *words,*keys,*values; size_t size; } LbMapView;

static LbMapStatus allocation_status(LbProcess *process)
{
    LbAllocStatus failure=lb_alloc_domain_failure(process->entry_module->space->domain);
    if(failure==LB_ALLOC_WORK_LIMIT) return LB_MAP_WORK_LIMIT;
    if(failure==LB_ALLOC_LIMIT) return LB_MAP_LIMIT;
    if(failure==LB_ALLOC_NO_MEMORY || process->status==LB_PROCESS_NO_MEMORY) return LB_MAP_NO_MEMORY;
    return LB_MAP_LIMIT;
}
static LbMapStatus charge(LbProcess *process,uint64_t units)
{
    if(!process) return LB_MAP_OK; /* loader-time comparisons are artifact-bounded */
    if(!lb_work_domain_charge(process->entry_module->space->domain,units)) return allocation_status(process);
    if(units>UINTPTR_MAX-process->last_native_cost) process->last_native_cost=UINTPTR_MAX;
    else process->last_native_cost+=(Uint)units;
    return LB_MAP_OK;
}
static int keys_tuple(Eterm term,Eterm **keys,size_t *size)
{
    Eterm *tuple;
    if(!is_tuple(term)) return 0;
    tuple=tuple_val(term);
    *size=(size_t)arityval(tuple[0]);
    *keys=tuple+1;
    return *size<=LB_FLATMAP_MAX_SIZE;
}
static LbMapStatus hamt_status(LbHamtStatus status)
{
    switch(status) {
    case LB_HAMT_OK:return LB_MAP_OK;
    case LB_HAMT_MISSING:return LB_MAP_MISSING;
    case LB_HAMT_WORK_LIMIT:return LB_MAP_WORK_LIMIT;
    case LB_HAMT_NO_MEMORY:return LB_MAP_NO_MEMORY;
    case LB_HAMT_INVALID:return LB_MAP_INVALID;
    default:return LB_MAP_UNSUPPORTED;
    }
}
static int small_map(Eterm term,LbMapView *view)
{
    Eterm *words,*keys; size_t size,key_count;
    if(!is_boxed(term)) return 0;
    words=boxed_val(term);
    if(words[0]!=LB_FLATMAP_HEADER) return 0;
    size=(size_t)words[1];
    (void)lb_flatmap_words(words);
    if(!keys_tuple(words[2],&keys,&key_count) || key_count!=size) return 0;
    if(view) *view=(LbMapView){words,keys,words+LB_FLATMAP_WORDS,size};
    return 1;
}

/* This ordering covers the atom and byte-string keys used by the admitted
 * application profile, plus small integer keys. Other same-rank compound keys
 * are rejected rather than assigned a guessed order. */
static int key_rank(Eterm term)
{
    if(is_small(term)) return 0;
    if(is_atom(term)) return 1;
    if(is_pid(term)) return 2;
    if(is_boxed(term)) {
        Eterm header=*boxed_val(term);
        if((header&_HEADER_SUBTAG_MASK)==FUN_SUBTAG) return 3;
        if(is_arity_value(header)) return 4;
        if(header==LB_FLATMAP_HEADER || lb_map_is_hamt_root_header(header)) return 5;
        if((header&_HEADER_SUBTAG_MASK)==POS_BIG_SUBTAG ||
           (header&_HEADER_SUBTAG_MASK)==NEG_BIG_SUBTAG ||
           (header&_HEADER_SUBTAG_MASK)==FLOAT_SUBTAG) return 0;
        if((header&_HEADER_SUBTAG_MASK)==HEAP_BITS_SUBTAG || header==LB_HEADER_SUB_BITS) return 8;
        if((header&_HEADER_SUBTAG_MASK)==REF_SUBTAG) return 2;
    }
    if(is_nil(term)) return 6;
    if(is_list(term)) return 7;
    return -1;
}
static LbMapStatus compare_bitstrings(LbProcess *process,Eterm left,Eterm right,int *order)
{
    LbBitstringView a,b;
    uint64_t common;
    if(!lb_bitstring_view(left,&a) || !lb_bitstring_view(right,&b)) return LB_MAP_INVALID;
    common=a.bit_size<b.bit_size?a.bit_size:b.bit_size;
    if(common && charge(process,lb_work_bulk_units((common+7)/8))!=LB_MAP_OK)
        return process?allocation_status(process):LB_MAP_WORK_LIMIT;
    for(uint64_t bit=0;bit<common;++bit) {
        unsigned av=(a.data[bit/8]>>(7-(bit%8)))&1u;
        unsigned bv=(b.data[bit/8]>>(7-(bit%8)))&1u;
        if(av!=bv) { *order=av<bv?-1:1; return LB_MAP_OK; }
    }
    *order=a.bit_size<b.bit_size?-1:a.bit_size>b.bit_size?1:0;
    return LB_MAP_OK;
}
static LbMapStatus compare_keys_depth(LbProcess *process,Eterm left,Eterm right,int *order,unsigned depth)
{
    int a,b;
    if(depth>128) return LB_MAP_UNSUPPORTED;
    if(!order) return LB_MAP_INVALID;
    *order=0;
    if(charge(process,1)!=LB_MAP_OK) return allocation_status(process);
    if(left==right) return LB_MAP_OK;
    a=key_rank(left); b=key_rank(right);
    if(a<0 || b<0) return LB_MAP_UNSUPPORTED;
    if(a!=b) { *order=a<b?-1:1; return LB_MAP_OK; }
    if(a==0 && is_small(left) && is_small(right)) {
        Sint x=signed_val(left),y=signed_val(right);
        *order=x<y?-1:x>y?1:0; return LB_MAP_OK;
    }
    if(a==1) {
        Uint x=atom_val(left),y=atom_val(right);
        *order=x<y?-1:x>y?1:0; return LB_MAP_OK;
    }
    if(a==4) {
        Eterm *left_tuple=tuple_val(left),*right_tuple=tuple_val(right);
        Uint left_arity=(Uint)arityval(left_tuple[0]),right_arity=(Uint)arityval(right_tuple[0]);
        if(left_arity!=right_arity) { *order=left_arity<right_arity?-1:1; return LB_MAP_OK; }
        for(Uint i=1;i<=left_arity;++i) {
            LbMapStatus status=compare_keys_depth(process,left_tuple[i],right_tuple[i],order,depth+1);
            if(status!=LB_MAP_OK || *order) return status;
        }
        return LB_MAP_OK;
    }
    if(a==7) {
        Eterm left_tail=left,right_tail=right;
        while(is_list(left_tail) && is_list(right_tail)) {
            Eterm *left_cell=list_val(left_tail),*right_cell=list_val(right_tail);
            LbMapStatus status=compare_keys_depth(process,left_cell[0],right_cell[0],order,depth+1);
            if(status!=LB_MAP_OK || *order) return status;
            left_tail=left_cell[1]; right_tail=right_cell[1];
        }
        if(left_tail==right_tail) { *order=0; return LB_MAP_OK; }
        return compare_keys_depth(process,left_tail,right_tail,order,depth+1);
    }
    if(a==8) return compare_bitstrings(process,left,right,order);
    return LB_MAP_UNSUPPORTED;
}
static LbMapStatus compare_keys(LbProcess *process,Eterm left,Eterm right,int *order)
{ return compare_keys_depth(process,left,right,order,0); }
LbMapStatus lb_map_compare_literals(Eterm left,Eterm right,int *order)
{ return compare_keys(NULL,left,right,order); }
LbMapStatus lb_map_compare_keys(LbProcess *process,Eterm left,Eterm right,int *order)
{ return compare_keys(process,left,right,order); }
static LbMapStatus find_key(LbProcess *process,const LbMapView *map,Eterm key,size_t *index)
{
    for(size_t i=0;i<map->size;++i) {
        int order;
        LbMapStatus status=compare_keys(process,map->keys[i],key,&order);
        if(status!=LB_MAP_OK) return status;
        if(!order) { *index=i; return LB_MAP_OK; }
    }
    return LB_MAP_MISSING;
}
static LbMapStatus reserve_words(LbProcess *process,size_t words,size_t live)
{
    size_t before;
    if(!process || live>MAX_REG) return LB_MAP_INVALID;
    if(words>process->heap_reserved) process->heap_reserved=words;
    before=process->collections;
    if(!lb_heap_reserve(process,words,live)) return allocation_status(process);
    if(before!=process->collections) {
        Uint cost=process->last_gc_cost;
        if(cost>UINTPTR_MAX-process->last_native_cost) process->last_native_cost=UINTPTR_MAX;
        else process->last_native_cost+=cost;
    }
    return LB_MAP_OK;
}
LbMapStatus lb_map_reserve(LbProcess *process,size_t maximum_words,size_t live)
{ return reserve_words(process,maximum_words,live); }

static size_t key_tuple_words(size_t count) { return count?count+1:2; }
static LbMapStatus commit_map(LbProcess *process,const Eterm *keys,const Eterm *values,
                              size_t count,Eterm reused_keys,int reuse_key_tuple,Eterm *result)
{
    size_t map_words,total_words;
    Eterm *map,*tuple=NULL;
    LbMapStatus status;
    if(!process || !result || count>LB_FLATMAP_MAX_SIZE || (count && (!keys || !values)))
        return LB_MAP_UNSUPPORTED;
    if(!lb_size_add(LB_FLATMAP_WORDS,count,&map_words)) return LB_MAP_LIMIT;
    total_words=map_words;
    if(!reuse_key_tuple && !lb_size_add(total_words,key_tuple_words(count),&total_words)) return LB_MAP_LIMIT;
    if(reuse_key_tuple) {
        Eterm *existing; size_t existing_count;
        if(!keys_tuple(reused_keys,&existing,&existing_count) || existing_count!=count ||
           (count && memcmp(existing,keys,count*sizeof(Eterm)))) return LB_MAP_INVALID;
    }
    if(total_words>process->heap_reserved || (size_t)(process->stop-process->htop)<total_words+S_RESERVED)
        return LB_MAP_INVALID;
    status=charge(process,total_words+lb_work_bulk_units(total_words));
    if(status!=LB_MAP_OK) return status;
    map=process->htop;
    map[0]=LB_FLATMAP_HEADER; map[1]=count;
    if(reuse_key_tuple) map[2]=reused_keys;
    else {
        tuple=map+map_words;
        tuple[0]=count?make_arityval(count):make_arityval_zero();
        if(!count) tuple[1]=THE_NON_VALUE;
        else for(size_t i=0;i<count;++i) tuple[i+1]=keys[i];
        map[2]=make_tuple(tuple);
    }
    for(size_t i=0;i<count;++i) map[LB_FLATMAP_WORDS+i]=values[i];
    process->htop+=total_words;
    process->heap_reserved-=total_words;
    *result=make_boxed(map);
    return LB_MAP_OK;
}

LbMapStatus lb_map_is_map(Eterm term,int *result)
{
    if(!result) return LB_MAP_INVALID;
    *result=0;
    if(!is_boxed(term) || (boxed_val(term)[0]&_HEADER_SUBTAG_MASK)!=MAP_SUBTAG) return LB_MAP_OK;
    if(lb_hamt_is_root(term)) {
        if(lb_hamt_size(term)<=LB_FLATMAP_MAX_SIZE) return LB_MAP_INVALID;
        *result=1; return LB_MAP_OK;
    }
    LbMapView view;
    if(!small_map(term,&view)) return LB_MAP_INVALID;
    *result=1; return LB_MAP_OK;
}

LbMapStatus lb_map_reservation_words(Eterm map_term,size_t updates,int exact,size_t *words)
{
    LbMapView map; size_t count,total;
    if(!words || !small_map(map_term,&map) || updates>LB_FLATMAP_MAX_SIZE) return LB_MAP_UNSUPPORTED;
    if(exact) count=map.size;
    else count=updates>LB_FLATMAP_MAX_SIZE-map.size?LB_FLATMAP_MAX_SIZE:map.size+updates;
    if(exact) {
        if(!lb_size_add(LB_FLATMAP_WORDS,count,&total)) return LB_MAP_LIMIT;
    } else {
        if(!lb_size_add(LB_FLATMAP_WORDS,count,&total) ||
           !lb_size_add(total,key_tuple_words(count),&total)) return LB_MAP_LIMIT;
    }
    *words=total; return LB_MAP_OK;
}

LbMapStatus lb_map_find(LbProcess *process,Eterm map_term,Eterm key,Eterm *value)
{
    LbMapView map; size_t index; LbMapStatus status;
    if(!value) return LB_MAP_INVALID;
    *value=THE_NON_VALUE;
    if(charge(process,1)!=LB_MAP_OK) return allocation_status(process);
    if(lb_hamt_is_root(map_term))
        return hamt_status(lb_hamt_get(process,map_term,key,value));
    if(!small_map(map_term,&map)) return LB_MAP_UNSUPPORTED;
    status=find_key(process,&map,key,&index);
    if(status==LB_MAP_OK) *value=map.values[index];
    return status;
}

LbMapStatus lb_map_create(LbProcess *process,const Eterm *keys,const Eterm *values,
                          size_t count,Eterm *result)
{
    LbMapPair pairs[LB_FLATMAP_MAX_SIZE];
    Eterm sorted_keys[LB_FLATMAP_MAX_SIZE],sorted_values[LB_FLATMAP_MAX_SIZE];
    size_t unique=0;
    LbMapStatus status;
    if(count>LB_FLATMAP_MAX_SIZE || (count && (!keys || !values))) return LB_MAP_UNSUPPORTED;
    status=charge(process,lb_work_bulk_units(count)); if(status!=LB_MAP_OK) return status;
    for(size_t i=0;i<count;++i) pairs[i]=(LbMapPair){keys[i],values[i]};
    /* Stable insertion sort preserves the source order of duplicate keys, so
     * the last evaluated association supplies the published value. */
    for(size_t i=1;i<count;++i) {
        LbMapPair pair=pairs[i]; size_t j=i;
        while(j) {
            int order;
            status=compare_keys(process,pairs[j-1].key,pair.key,&order);
            if(status!=LB_MAP_OK) return status;
            if(order<=0) break;
            pairs[j]=pairs[j-1]; --j;
        }
        pairs[j]=pair;
    }
    for(size_t i=0;i<count;++i) {
        if(unique) {
            int order;
            status=compare_keys(process,sorted_keys[unique-1],pairs[i].key,&order);
            if(status!=LB_MAP_OK) return status;
            if(!order) { sorted_values[unique-1]=pairs[i].value; continue; }
        }
        sorted_keys[unique]=pairs[i].key; sorted_values[unique]=pairs[i].value; ++unique;
    }
    return commit_map(process,sorted_keys,sorted_values,unique,NIL,0,result);
}

LbMapStatus lb_map_create_literal(LbProcess *process,Eterm keys,const Eterm *values,
                                  size_t count,Eterm *result)
{
    Eterm *literal_keys,ordered_keys[LB_FLATMAP_MAX_SIZE]; size_t literal_count;
    if(!keys_tuple(keys,&literal_keys,&literal_count) || literal_count!=count ||
       count>LB_FLATMAP_MAX_SIZE || (count && !values)) return LB_MAP_UNSUPPORTED;
    if(count) memcpy(ordered_keys,literal_keys,count*sizeof(Eterm));
    return lb_map_create(process,ordered_keys,values,count,result);
}

LbMapStatus lb_map_update_assoc(LbProcess *process,Eterm map_term,const Eterm *keys,
                                const Eterm *values,size_t count,Eterm *result)
{
    LbMapView map;
    Eterm out_keys[LB_FLATMAP_MAX_SIZE],out_values[LB_FLATMAP_MAX_SIZE];
    size_t out_count;
    LbMapStatus status;
    if(!result) return LB_MAP_INVALID;
    if(!small_map(map_term,&map)) return LB_MAP_UNSUPPORTED;
    if(count>LB_FLATMAP_MAX_SIZE || (count && (!keys || !values))) return LB_MAP_UNSUPPORTED;
    if(!count) {
        status=charge(process,1);
        if(status==LB_MAP_OK) *result=map_term;
        return status;
    }
    out_count=map.size;
    if(out_count) {
        status=charge(process,lb_work_bulk_units(2*(uint64_t)out_count));
        if(status!=LB_MAP_OK) return status;
        memcpy(out_keys,map.keys,out_count*sizeof(Eterm));
        memcpy(out_values,map.values,out_count*sizeof(Eterm));
    }
    for(size_t i=1;i<out_count;++i) {
        Eterm key=out_keys[i],value=out_values[i]; size_t j=i;
        while(j) {
            int order;
            status=compare_keys(process,out_keys[j-1],key,&order);
            if(status!=LB_MAP_OK) return status;
            if(order<0) break;
            if(!order) return LB_MAP_INVALID;
            out_keys[j]=out_keys[j-1]; out_values[j]=out_values[j-1]; --j;
        }
        out_keys[j]=key; out_values[j]=value;
    }
    for(size_t i=0;i<count;++i) {
        size_t at=0;
        while(at<out_count) {
            int order;
            status=compare_keys(process,out_keys[at],keys[i],&order);
            if(status!=LB_MAP_OK) return status;
            if(order>=0) {
                if(!order) { out_values[at]=values[i]; break; }
                break;
            }
            ++at;
        }
        if(at<out_count && out_keys[at]==keys[i]) continue;
        if(at<out_count) {
            int order;
            status=compare_keys(process,out_keys[at],keys[i],&order);
            if(status!=LB_MAP_OK) return status;
            if(!order) { out_values[at]=values[i]; continue; }
        }
        if(out_count==LB_FLATMAP_MAX_SIZE) return LB_MAP_UNSUPPORTED;
        memmove(out_keys+at+1,out_keys+at,(out_count-at)*sizeof(Eterm));
        memmove(out_values+at+1,out_values+at,(out_count-at)*sizeof(Eterm));
        out_keys[at]=keys[i]; out_values[at]=values[i]; ++out_count;
    }
    return commit_map(process,out_keys,out_values,out_count,NIL,0,result);
}

LbMapStatus lb_map_update_exact_missing(LbProcess *process,Eterm map_term,const Eterm *keys,
                                        const Eterm *values,size_t count,Eterm *result,Eterm *missing_key)
{
    LbMapView map,sorted;
    Eterm out_keys[LB_FLATMAP_MAX_SIZE],out_values[LB_FLATMAP_MAX_SIZE];
    LbMapStatus status;
    if(!small_map(map_term,&map)) return LB_MAP_UNSUPPORTED;
    if(count>LB_FLATMAP_MAX_SIZE || (count && (!keys || !values))) return LB_MAP_UNSUPPORTED;
    if(map.size) {
        status=charge(process,lb_work_bulk_units(2*(uint64_t)map.size));
        if(status!=LB_MAP_OK) return status;
        memcpy(out_keys,map.keys,map.size*sizeof(Eterm));
        memcpy(out_values,map.values,map.size*sizeof(Eterm));
    }
    for(size_t i=1;i<map.size;++i) {
        Eterm key=out_keys[i],value=out_values[i]; size_t j=i;
        while(j) {
            int order;
            status=compare_keys(process,out_keys[j-1],key,&order);
            if(status!=LB_MAP_OK) return status;
            if(order<0) break;
            if(!order) return LB_MAP_INVALID;
            out_keys[j]=out_keys[j-1]; out_values[j]=out_values[j-1]; --j;
        }
        out_keys[j]=key; out_values[j]=value;
    }
    sorted=(LbMapView){map.words,out_keys,out_values,map.size};
    for(size_t i=0;i<count;++i) {
        size_t index;
        status=find_key(process,&sorted,keys[i],&index);
        if(status!=LB_MAP_OK) {
            if(status==LB_MAP_MISSING && missing_key) *missing_key=keys[i];
            return status;
        }
        out_values[index]=values[i];
    }
    int already_sorted=!map.size || !memcmp(out_keys,map.keys,map.size*sizeof(Eterm));
    return commit_map(process,out_keys,out_values,map.size,map.words[2],already_sorted,result);
}

LbMapStatus lb_map_update_exact(LbProcess *process,Eterm map_term,const Eterm *keys,
                                const Eterm *values,size_t count,Eterm *result)
{
    return lb_map_update_exact_missing(process,map_term,keys,values,count,result,NULL);
}
