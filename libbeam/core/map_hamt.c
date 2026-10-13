/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 2014-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Bitmap-HAMT layout and hashing adapted from OTP erl_map.h, erl_map.c and
 * erl_term_hashing.c. Immediate and proper byte-list keys only; full-hash
 * collisions and other key classes remain explicitly unsupported.
 */
#include "map_hamt.h"
#include "flatmap.h"
#include "process_internal.h"
#include "term_compare.h"
#include <stdlib.h>
#include <string.h>
#define LB_MAP_HEADER_TAG_SHIFT 2u
#define LB_MAP_HEADER_ARITY_SHIFT 8u
#define LB_MAP_HEADER_VALUE_SHIFT 16u
#define LB_MAP_TYPE(header) (((header)>>_HEADER_ARITY_OFFS)&3u)
#define LB_MAP_VALUE(header) (((header)>>LB_MAP_HEADER_VALUE_SHIFT)&0xffffu)
#define LB_MAP_HEADER(type,arity,value) \
    _make_header(((((Uint)(value)<<LB_MAP_HEADER_ARITY_SHIFT)|(arity))<<LB_MAP_HEADER_TAG_SHIFT)|(type),MAP_SUBTAG)
#define LB_HAMT_NODE_BITMAP 1u
#define LB_HAMT_HEAD_ARRAY 2u
#define LB_HAMT_HEAD_BITMAP 3u
#define LB_HAMT_LEVELS 16u
#define LB_HAMT_ARRAY_WORDS 18u

typedef struct { LbHamtAllocate allocate; void *context; Uint pointer_tag; } BuildContext;
static unsigned popcount16(unsigned value)
{
    unsigned count=0;
    for(;value;value&=value-1) ++count;
    return count;
}
static uint64_t mix64(uint64_t hash)
{
    hash^=hash>>33; hash*=UINT64_C(0xFF51AFD7ED558CCD);
    hash^=hash>>33; hash*=UINT64_C(0xC4CEB9FE1A85EC53);
    hash^=hash>>33; return hash;
}
int lb_hamt_is_root(Eterm term)
{
    if(!is_boxed(term)) return 0;
    Uint header=*boxed_val(term);
    if((header&_HEADER_SUBTAG_MASK)!=MAP_SUBTAG) return 0;
    unsigned type=LB_MAP_TYPE(header);
    return type==LB_HAMT_HEAD_ARRAY || type==LB_HAMT_HEAD_BITMAP;
}
int lb_hamt_is_node(Eterm term)
{
    return is_boxed(term) && ((*boxed_val(term)&_HEADER_SUBTAG_MASK)==MAP_SUBTAG) &&
        LB_MAP_TYPE(*boxed_val(term))==LB_HAMT_NODE_BITMAP;
}
size_t lb_hamt_size(Eterm term)
{
    if(!lb_hamt_is_root(term)) return 0;
    return (size_t)boxed_val(term)[1];
}
size_t lb_hamt_boxed_words(const Eterm *pointer)
{
    Uint header=pointer[0]; unsigned type=LB_MAP_TYPE(header),bits=LB_MAP_VALUE(header);
    if((header&_HEADER_SUBTAG_MASK)!=MAP_SUBTAG) abort();
    if(type==LB_HAMT_HEAD_ARRAY) return LB_HAMT_ARRAY_WORDS;
    if(type==LB_HAMT_HEAD_BITMAP) return 2u+popcount16(bits);
    if(type==LB_HAMT_NODE_BITMAP) return 1u+popcount16(bits);
    abort();
}
static uint64_t mix_alpha(uint64_t alpha,uint64_t beta,uint64_t value,uint64_t *ticks)
{
    value*=UINT64_C(0x87C37B91114253D5); value=(value<<31)|(value>>33);
    value*=UINT64_C(0x4CF5AD432745937F); alpha^=value;
    alpha=(alpha<<27)|(alpha>>37); alpha+=beta; alpha=alpha*5+UINT64_C(0x52DCE729);
    ++*ticks; return alpha;
}
static uint64_t mix_beta(uint64_t alpha,uint64_t beta,uint64_t value,uint64_t *ticks)
{
    value*=UINT64_C(0x4CF5AD432745937F); value=(value<<33)|(value>>31);
    value*=UINT64_C(0x87C37B91114253D5); beta^=value;
    beta=(beta<<31)|(beta>>33); beta+=alpha; beta=beta*5+UINT64_C(0x38495AB5);
    ++*ticks; return beta;
}
static uint64_t finish_hash(uint64_t alpha,uint64_t beta,uint64_t ticks)
{
    alpha^=ticks; beta^=ticks; alpha+=beta; beta+=alpha;
    alpha=mix64(alpha); beta=mix64(beta); alpha+=beta; beta+=alpha;
    return alpha^beta;
}
static LbHamtStatus hash_key(LbProcess *process,Eterm key,uint64_t *hash)
{
    if(!hash) return LB_HAMT_INVALID;
    if(is_immed(key)) { *hash=mix64((uint64_t)key); return LB_HAMT_OK; }
    if(is_list(key)) {
        uint64_t alpha=0,beta=0,ticks=0,value=0; unsigned bytes=0; Eterm term=key;
        while(is_list(term)) {
            if(process) {
                if(!lb_work_domain_charge(process->entry_module->space->domain,1))
                    return LB_HAMT_WORK_LIMIT;
                if(process->last_native_cost<UINTPTR_MAX) ++process->last_native_cost;
            }
            Eterm *cell=list_val(term),character=cell[0];
            if(!is_small(character) || signed_val(character)<0 || signed_val(character)>255)
                return LB_HAMT_UNSUPPORTED;
            value=(value<<8)|(uint64_t)signed_val(character); ++bytes;
            if(bytes==4) {
                alpha=mix_alpha(alpha,beta,5u|((uint64_t)bytes<<8)|(value<<32),&ticks);
                value=0; bytes=0;
            }
            term=cell[1];
        }
        if(!is_nil(term)) return LB_HAMT_UNSUPPORTED;
        if(bytes) {
            alpha=mix_alpha(alpha,beta,5u|((uint64_t)bytes<<8)|(value<<32),&ticks);
        }
        /* The terminal NIL participates as an immediate in OTP's loop. */
        alpha=mix_alpha(alpha,beta,1,&ticks);
        beta=mix_beta(alpha,beta,NIL,&ticks);
        *hash=finish_hash(alpha,beta,ticks); return LB_HAMT_OK;
    }
    return LB_HAMT_UNSUPPORTED;
}
LbHamtStatus lb_hamt_hash_key(Eterm key,uint64_t *hash)
{ return hash_key(NULL,key,hash); }
/* Sort in trie traversal order: OTP consumes the least-significant nibble first. */
static uint64_t swizzle(uint64_t hash)
{
    uint64_t result=0;
    for(unsigned i=0;i<16;++i) { result=(result<<4)|(hash&15u); hash>>=4; }
    return result;
}
static int pair_hash_order(const void *left,const void *right)
{
    const LbHamtPair *a=left,*b=right;
    uint64_t ah=swizzle(a->hash),bh=swizzle(b->hash);
    if(ah<bh) return -1;
    if(ah>bh) return 1;
    if(a->key<b->key) return -1;
    if(a->key>b->key) return 1;
    return 0;
}
static Eterm *allocate_words(BuildContext *context,size_t words)
{ return words?context->allocate(context->context,words):NULL; }
static Eterm boxed_literal(Eterm *pointer,const BuildContext *context)
{ return make_boxed(pointer)|context->pointer_tag; }
static Eterm list_literal(Eterm *pointer,const BuildContext *context)
{ return make_list(pointer)|context->pointer_tag; }
static Eterm build_level(BuildContext *context,const LbHamtPair *pairs,size_t count,
                         unsigned depth,int root,LbHamtStatus *status)
{
    Eterm children[16]; size_t child_count=0,index=0; unsigned bitmap=0,shift,base;
    if(count==1 && !root) {
        Eterm *cell=allocate_words(context,2);
        if(!cell) { *status=LB_HAMT_NO_MEMORY; return THE_NON_VALUE; }
        cell[0]=pairs[0].key; cell[1]=pairs[0].value;
        return list_literal(cell,context);
    }
    if(depth>=LB_HAMT_LEVELS) { *status=LB_HAMT_UNSUPPORTED; return THE_NON_VALUE; }
    shift=depth*4;
    while(index<count) {
        unsigned slot=(unsigned)((pairs[index].hash>>shift)&0xfu);
        size_t end=index+1;
        while(end<count && ((pairs[end].hash>>shift)&0xfu)==slot) ++end;
        bitmap|=1u<<slot;
        children[slot]=build_level(context,pairs+index,end-index,depth+1,0,status);
        if(*status!=LB_HAMT_OK) return THE_NON_VALUE;
        ++child_count; index=end;
    }
    if(root && bitmap==0xffffu) {
        Eterm *node=allocate_words(context,LB_HAMT_ARRAY_WORDS);
        if(!node) { *status=LB_HAMT_NO_MEMORY; return THE_NON_VALUE; }
        node[0]=LB_MAP_HEADER(LB_HAMT_HEAD_ARRAY,1,0xffffu); node[1]=count;
        for(unsigned slot=0;slot<16;++slot) node[slot+2]=children[slot];
        return boxed_literal(node,context);
    }
    base=root?2u:1u;
    Eterm *node=allocate_words(context,base+child_count);
    if(!node) { *status=LB_HAMT_NO_MEMORY; return THE_NON_VALUE; }
    node[0]=LB_MAP_HEADER(root?LB_HAMT_HEAD_BITMAP:LB_HAMT_NODE_BITMAP,
                          root?1u:0u,bitmap);
    if(root) node[1]=count;
    size_t output=base;
    for(unsigned slot=0;slot<16;++slot) if(bitmap&(1u<<slot)) node[output++]=children[slot];
    return boxed_literal(node,context);
}
LbHamtStatus lb_hamt_build(LbHamtPair *pairs,size_t count,LbHamtAllocate allocate,
                           void *context,Uint pointer_tag,Eterm *result)
{
    BuildContext builder; LbHamtStatus status=LB_HAMT_OK;
    if(!pairs || count<=LB_FLATMAP_MAX_SIZE || !allocate || !result) return LB_HAMT_INVALID;
    qsort(pairs,count,sizeof(*pairs),pair_hash_order);
    /* OTP collision nodes are deliberately not approximated. This profile
     * rejects a full-hash collision rather than publishing a non-native node. */
    for(size_t i=1;i<count;++i) if(pairs[i-1].hash==pairs[i].hash)
        return LB_HAMT_UNSUPPORTED;
    builder=(BuildContext){allocate,context,pointer_tag};
    Eterm root=build_level(&builder,pairs,count,0,1,&status);
    if(status==LB_HAMT_OK) *result=root;
    return status;
}
LbHamtStatus lb_hamt_get(LbProcess *process,Eterm map,Eterm key,Eterm *value)
{
    uint64_t hash; Eterm *node; LbHamtStatus status;
    if(!process || !value || !lb_hamt_is_root(map)) return LB_HAMT_INVALID;
    *value=THE_NON_VALUE;
    status=hash_key(process,key,&hash); if(status!=LB_HAMT_OK) return status;
    node=boxed_val(map);
    for(unsigned depth=0;depth<LB_HAMT_LEVELS;++depth) {
        if(!lb_work_domain_charge(process->entry_module->space->domain,1)) {
            process->status=LB_PROCESS_WORK_LIMIT; return LB_HAMT_WORK_LIMIT;
        }
        if(process->last_native_cost<UINTPTR_MAX) ++process->last_native_cost;
        Uint header=node[0]; unsigned type=LB_MAP_TYPE(header),bitmap,slot;
        size_t base,index;
        if(depth==0 && type==LB_HAMT_HEAD_ARRAY) {
            bitmap=0xffffu; base=2;
        } else if((depth==0 && type==LB_HAMT_HEAD_BITMAP) ||
                  (depth>0 && type==LB_HAMT_NODE_BITMAP)) {
            bitmap=LB_MAP_VALUE(header); base=depth==0?2u:1u;
        } else return LB_HAMT_INVALID;
        unsigned shift=depth*4;
        slot=(unsigned)((hash>>shift)&0xfu);
        if(!(bitmap&(1u<<slot))) return LB_HAMT_MISSING;
        index=popcount16(bitmap&((1u<<slot)-1u));
        Eterm child=node[base+index];
        if(is_list(child)) {
            Eterm *pair=list_val(child); int equal=0;
            LbAllocStatus compared=lb_term_equal(process->entry_module->space->domain,pair[0],key,&equal);
            if(compared==LB_ALLOC_WORK_LIMIT) { process->status=LB_PROCESS_WORK_LIMIT; return LB_HAMT_WORK_LIMIT; }
            if(compared==LB_ALLOC_NO_MEMORY) return LB_HAMT_NO_MEMORY;
            if(compared!=LB_ALLOC_OK) return LB_HAMT_INVALID;
            if(!equal) return LB_HAMT_MISSING;
            *value=pair[1]; return LB_HAMT_OK;
        }
        if(!lb_hamt_is_node(child)) return LB_HAMT_INVALID;
        node=boxed_val(child);
    }
    return LB_HAMT_INVALID;
}
