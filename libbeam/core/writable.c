/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1999-2025. All Rights Reserved.
 * Copyright Ericsson AB 2017-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Adapted erl_bits.c build_writable_bitstring / erts_bs_append_checked and
 * bs_instrs.tab's fresh heap/refcounted storage setup.
 * Allocation and collection are fallible preparation; publication is infallible.
 * No implicit scheduler bit state, allocator reallocation, or heap fragments.
 */
#include "writable.h"
#include "binary_internal.h"
#include "bit_copy.h"
#include "process_internal.h"

static size_t grown_capacity(size_t bytes)
{
    /* Native doubling / 20% growth, bounded by the owned allocation profile.
     * Integer arithmetic avoids depending on the caller's floating environment. */
    size_t grown=bytes>(1u<<24)?bytes+bytes/5:2*bytes;
    return grown>LB_MAX_BINARY_BYTES?LB_MAX_BINARY_BYTES:grown;
}
static int overhead_fits(LbProcess *p,size_t added)
{
    uint64_t words=added/sizeof(Eterm),total=p->off_heap.overhead;
    if(p->writable.overhead>UINT64_MAX-total) return 0;
    total+=p->writable.overhead;
    return words<=UINT64_MAX-total;
}
static int reserve(LbProcess *p,size_t words,size_t extra,size_t binary_bytes)
{
    size_t need;
    if(extra<p->heap_reserved) extra=p->heap_reserved;
    if(!lb_size_add(words,extra,&need)) return 0;
    return binary_bytes?lb_heap_reserve_binary(p,need,binary_bytes,MAX_REG):lb_heap_reserve(p,need,MAX_REG);
}
static Eterm wrap(LbProcess *p,LbBinary *binary,size_t bits,size_t apparent)
{
    LbSubBits *sub=(LbSubBits *)p->htop;
    LbBinRef *ref=(LbBinRef *)(p->htop+LB_SUB_BITS_WORDS);
    binary->intern.flags=LB_BINARY_WRITABLE|LB_BINARY_ACTIVE_WRITER;
    binary->intern.apparent_size=apparent;
    ref->thing_word=LB_HEADER_BIN_REF; ref->val=binary;
    ref->next=p->writable.first; p->writable.first=ref;
    p->writable.overhead+=(Uint)binary->orig_size/sizeof(Eterm);
    lb_sub_bits_init(sub,LB_SUB_WRITABLE,make_boxed((Eterm *)ref),binary->orig_bytes,0,bits);
    p->htop+=LB_REFC_BITS_WORDS;
    return make_boxed((Eterm *)sub);
}
LbWritableStatus lb_writable_init(LbProcess *p,size_t capacity,Eterm *out)
{
    LbBinary *binary;
    if(!p || !out) return LB_WRITABLE_INVALID;
    if(capacity>LB_MAX_BINARY_BYTES) return LB_WRITABLE_LIMIT;
    if(!overhead_fits(p,capacity)) return LB_WRITABLE_NO_MEMORY;
    binary=lb_binary_allocate(p->entry_module->space->domain,capacity);
    if(!binary) return LB_WRITABLE_NO_MEMORY;
    memset(binary->orig_bytes,0,capacity);
    if(!reserve(p,LB_REFC_BITS_WORDS,0,0)) {
        lb_binary_release(binary); return LB_WRITABLE_NO_MEMORY;
    }
    *out=wrap(p,binary,0,capacity);
    return LB_WRITABLE_OK;
}
LbWritableStatus lb_writable_init_term(LbProcess *p,Eterm requested,Eterm *out)
{
    size_t capacity=1024,collections; LbWritableStatus status;
    if(!p || !out) return LB_WRITABLE_INVALID;
    if(is_small(requested) && signed_val(requested)>=0) capacity=(size_t)signed_val(requested);
    collections=p->collections; p->last_native_cost=1;
    /* One reduction per attempted KiB initialization, in addition to the
     * actual collector charge. Allocation failure may cost less; callbacks
     * and a single bounded memset are not a hard-latency guarantee. */
    if(capacity<=LB_MAX_BINARY_BYTES) p->last_native_cost+=(capacity+1023)/1024;
    status=lb_writable_init(p,capacity,out);
    if(p->collections!=collections) p->last_native_cost+=p->last_gc_cost;
    return status;
}
LbWritableStatus lb_binary_begin(LbProcess *p,size_t bits,size_t extra,LbBitWriter *writer,Eterm *out)
{
    size_t words,bytes; LbBinary *binary=NULL; unsigned char *data; Eterm result;
    if(!p || !writer || !out) return LB_WRITABLE_INVALID;
    words=lb_bitstring_heap_words(bits);
    if(!words) return LB_WRITABLE_LIMIT;
    if(extra>SIZE_MAX-words || p->heap_reserved>SIZE_MAX-words) return LB_WRITABLE_NO_MEMORY;
    bytes=(bits+7)/8;
    if(bytes>LB_ONHEAP_BINARY_LIMIT) {
        if(!overhead_fits(p,bytes)) return LB_WRITABLE_NO_MEMORY;
        binary=lb_binary_allocate(p->entry_module->space->domain,bytes);
        if(!binary) return LB_WRITABLE_NO_MEMORY;
        memset(binary->orig_bytes,0,bytes);
    }
    /* Allocation precedes the final fallible reservation. A detached backing
     * has no heap pointers; no input term is borrowed across collection. */
    if(!reserve(p,words,extra,binary?bytes:0)) {
        if(binary) lb_binary_release(binary);
        return LB_WRITABLE_NO_MEMORY;
    }
    if(binary) {
        data=binary->orig_bytes;
        result=lb_binary_wrap(&p->off_heap,&p->htop,binary,bits,0);
    } else {
        Eterm *hp=p->htop;
        hp[0]=_make_header(words-1,HEAP_BITS_SUBTAG); hp[1]=bits;
        data=(unsigned char *)(hp+2); memset(data,0,(words-2)*sizeof(Eterm));
        result=make_boxed(hp); p->htop+=words;
    }
    *writer=(LbBitWriter){data,bytes,0}; *out=result;
    return LB_WRITABLE_OK;
}
static LbBinary *writable_backing(Eterm source)
{
    LbSubBits *sub;
    LbBinRef *ref;
    if(*boxed_val(source)!=LB_HEADER_SUB_BITS) return NULL;
    sub=(LbSubBits *)boxed_val(source);
    if((sub->base_flags&3)!=LB_SUB_WRITABLE) return NULL;
    ref=(LbBinRef *)boxed_val(sub->orig);
    if(!(ref->val->intern.flags&LB_BINARY_WRITABLE)) return NULL;
    if(sub->start || atomic_load(&ref->val->intern.refc)!=1) abort();
    return ref->val;
}
static LbWritableStatus prepare_backing(LbProcess *p,LbBinary *old,
                                         LbBitstringView view,size_t bytes,LbBinary **out)
{
    size_t capacity=grown_capacity(bytes);
    LbBinary *replacement;
    if(!old && capacity<256) capacity=256;
    if(!overhead_fits(p,capacity)) return LB_WRITABLE_NO_MEMORY;
    replacement=lb_binary_allocate(p->entry_module->space->domain,capacity);
    if(!replacement) return LB_WRITABLE_NO_MEMORY;
    memset(replacement->orig_bytes,0,capacity);
    /* All bytes are initialized. Reallocation retains the old capacity;
     * immutable/unaligned sources copy only their visible bit window. */
    if(old) memcpy(replacement->orig_bytes,old->orig_bytes,(size_t)old->orig_size);
    else if(!lb_bits_copy(view.data,(view.bit_offset+view.bit_size+7)/8,
                         view.bit_offset,replacement->orig_bytes,capacity,0,view.bit_size)) abort();
    *out=replacement; return LB_WRITABLE_OK;
}
static Eterm extend_owned(LbProcess *p,Eterm source,LbBinary *replacement,size_t used)
{
    LbSubBits *previous=(LbSubBits *)boxed_val(source);
    LbBinRef *ref=(LbBinRef *)boxed_val(previous->orig);
    LbSubBits *sub=(LbSubBits *)p->htop;
    LbBinary *binary=ref->val;
    /* Commit: revoke the old view before another append can observe it.
     * Every old view is volatile and follows this same (possibly moved)
     * BinRef, so replacing its allocation preserves all old prefixes. */
    previous->base_flags&=~LB_SUB_MUTABLE;
    if(replacement) {
        p->writable.overhead-=(Uint)binary->orig_size/sizeof(Eterm);
        p->writable.overhead+=(Uint)replacement->orig_size/sizeof(Eterm);
        ref->val=replacement; lb_binary_release(binary); binary=replacement;
    }
    binary->intern.flags=LB_BINARY_WRITABLE|LB_BINARY_ACTIVE_WRITER;
    binary->intern.apparent_size=(used+7)/8;
    lb_sub_bits_init(sub,LB_SUB_WRITABLE,previous->orig,binary->orig_bytes,0,used);
    p->htop+=LB_SUB_BITS_WORDS; return make_boxed((Eterm *)sub);
}
LbWritableStatus lb_writable_append(LbProcess *p,Eterm source,size_t bits,
                                   size_t extra,unsigned unit,LbBitWriter *writer,Eterm *out)
{
    LbBitstringView view;
    LbBinary *old,*replacement=NULL,*binary;
    size_t used,bytes,words;
    Eterm result;
    if(!p || !writer || !out || !unit || unit>256) return LB_WRITABLE_INVALID;
    if(!lb_bitstring_view(source,&view)) return LB_WRITABLE_TYPE;
    if(view.bit_size%unit) return LB_WRITABLE_UNIT;
    if(bits>8u*(size_t)LB_MAX_BINARY_BYTES || view.bit_size>8u*(size_t)LB_MAX_BINARY_BYTES-bits)
        return LB_WRITABLE_LIMIT;
    used=view.bit_size+bits; bytes=(used+7)/8;
    old=writable_backing(source);
    words=bits?(old?LB_SUB_BITS_WORDS:LB_REFC_BITS_WORDS):0;
    if(bits && (!old || (size_t)old->orig_size<bytes)) {
        LbWritableStatus status=prepare_backing(p,old,view,bytes,&replacement);
        if(status!=LB_WRITABLE_OK) return status;
    }
    /* The incoming term may be a temporary, not an X register. Root it before
     * the final fallible step, and never reuse heap pointers across collection. */
    if(p->construction_root!=NIL) abort();
    p->construction_root=source;
    if(!reserve(p,words,extra,0)) {
        p->construction_root=NIL;
        if(replacement) lb_binary_release(replacement);
        return LB_WRITABLE_NO_MEMORY;
    }
    source=p->construction_root; p->construction_root=NIL;
    if(!bits) {
        *out=source; *writer=(LbBitWriter){0}; return LB_WRITABLE_OK;
    }
    binary=replacement?replacement:old;
    result=old?extend_owned(p,source,replacement,used):wrap(p,binary,used,bytes);
    *writer=(LbBitWriter){binary->orig_bytes,bytes,view.bit_size}; *out=result;
    return LB_WRITABLE_OK;
}

static Eterm *source_slot(LbProcess *p,LbBinarySource source)
{
    if(source.kind==LB_BINARY_SOURCE_X && source.index<MAX_REG) return &p->reg[source.index];
    if(source.kind==LB_BINARY_SOURCE_Y && source.index<(size_t)(p->hend-p->stop)) return &p->stop[source.index];
    return NULL;
}
static LbWritableStatus private_replacement(LbProcess *p,LbBinary *old,size_t bytes,LbBinary **out)
{
    size_t capacity=grown_capacity(bytes),copy=(size_t)old->orig_size;
    LbBinary *binary;
    if(!overhead_fits(p,capacity)) return LB_WRITABLE_NO_MEMORY;
    binary=lb_binary_allocate(p->entry_module->space->domain,capacity);
    if(!binary) return LB_WRITABLE_NO_MEMORY;
    memset(binary->orig_bytes,0,capacity);
    if(copy>capacity) copy=capacity;
    memcpy(binary->orig_bytes,old->orig_bytes,copy);
    *out=binary; return LB_WRITABLE_OK;
}
LbWritableStatus lb_writable_private_append(LbProcess *p,LbBinarySource location,size_t bits,
                                           LbBitWriter *writer,Eterm *out)
{
    Eterm *slot,result; LbSubBits *sub; LbBinRef *ref;
    LbBinary *binary,*replacement=NULL; size_t position,used,bytes;
    LbWritableStatus status;
    if(!p || !writer || !out || !(slot=source_slot(p,location))) return LB_WRITABLE_INVALID;
    if(!is_boxed(*slot) || is_literal_ptr(*slot) || *boxed_val(*slot)!=LB_HEADER_SUB_BITS)
        return LB_WRITABLE_TYPE;
    sub=(LbSubBits *)boxed_val(*slot); ref=(LbBinRef *)boxed_val(sub->orig); binary=ref->val;
    if(sub->start || ref->thing_word!=LB_HEADER_BIN_REF || (sub->base_flags&3)==LB_SUB_MUTABLE)
        return LB_WRITABLE_INVALID;
    if(binary->intern.flags && ((sub->base_flags&3)!=LB_SUB_WRITABLE ||
       !(binary->intern.flags&LB_BINARY_WRITABLE) || atomic_load(&binary->intern.refc)!=1))
        return LB_WRITABLE_INVALID;
    if(lb_process_has_other_reference(p,*slot,slot)) return LB_WRITABLE_NOT_UNIQUE;
    position=sub->end;
    if(bits>8u*(size_t)LB_MAX_BINARY_BYTES || position>8u*(size_t)LB_MAX_BINARY_BYTES-bits)
        return LB_WRITABLE_LIMIT;
    used=position+bits; bytes=(used+7)/8;
    if(!binary->intern.flags) {
        /* Native frozen/traced fallback: new backing and a new BinRef, even
         * for a zero extension. Heap fragments become fallible heap reservation. */
        status=private_replacement(p,binary,bytes,&replacement);
        if(status!=LB_WRITABLE_OK) return status;
        if(!reserve(p,LB_REFC_BITS_WORDS,0,0)) {
            lb_binary_release(replacement); return LB_WRITABLE_NO_MEMORY;
        }
        result=wrap(p,replacement,used,bytes); binary=replacement;
    } else {
        /* Native unique-header path allocates no heap words. Prepare any new
         * backing before modifying the existing view, chain or reference. */
        if((size_t)binary->orig_size<bytes) {
            status=private_replacement(p,binary,bytes,&replacement);
            if(status!=LB_WRITABLE_OK) return status;
            p->writable.overhead-=(Uint)binary->orig_size/sizeof(Eterm);
            p->writable.overhead+=(Uint)replacement->orig_size/sizeof(Eterm);
            ref->val=replacement; lb_binary_release(binary); binary=replacement;
        }
        sub->end=used; binary->intern.apparent_size=bytes;
        binary->intern.flags=LB_BINARY_WRITABLE|LB_BINARY_ACTIVE_WRITER;
        result=*slot;
    }
    /* A Y slot may have moved in the frozen fallback. Consume the real source
     * root, not the pre-GC pointer; the caller publishes the replacement root. */
    *source_slot(p,location)=NIL;
    *writer=(LbBitWriter){binary->orig_bytes,bytes,position}; *out=result;
    return LB_WRITABLE_OK;
}
