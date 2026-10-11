/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1999-2025. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Adapted binary allocation/wrap/copy/sweep algorithms. See
 * otp/binary-sources.json and otp/writable-sources.json. No global initialization
 * or magic resources. Pin/sweep deliberately retain capacity until release.
 */
#include "binary_internal.h"
#include <stdlib.h>

typedef union {
    struct { LbAllocDomain *domain; } owner;
    max_align_t alignment;
} LbBinaryOwner;

size_t lb_binary_allocation_size(size_t bytes)
{
    if(bytes>LB_MAX_BINARY_BYTES) return 0;
    return sizeof(LbBinaryOwner)+offsetof(LbBinary,orig_bytes)+bytes;
}
LbBinary *lb_binary_allocate(LbAllocDomain *domain,size_t bytes)
{
    LbBinaryOwner *owner;
    LbBinary *binary;
    void *memory;
    size_t size=lb_binary_allocation_size(bytes);
    if(!size || lb_alloc_domain_allocate(domain,size,&memory)!=LB_ALLOC_OK) return NULL;
    owner=memory; owner->owner.domain=domain; binary=(LbBinary *)(owner+1);
    binary->intern.flags=0; binary->intern.apparent_size=0;
    atomic_init(&binary->intern.refc,1); binary->orig_size=(Sint)bytes;
    return binary;
}
int lb_binary_retain(LbBinary *binary)
{
    Uint count=atomic_load_explicit(&binary->intern.refc,memory_order_relaxed);
    if(binary->intern.flags) return 0; /* Explicit pin is required before sharing. */
    while(count && count<UINTPTR_MAX) {
        if(atomic_compare_exchange_weak_explicit(&binary->intern.refc,&count,count+1,
                                                memory_order_relaxed,memory_order_relaxed)) return 1;
    }
    return 0;
}
void lb_binary_release(LbBinary *binary)
{
    Uint count;
    Uint flags=binary->intern.flags;
    if(flags && flags!=LB_BINARY_WRITABLE && flags!=(LB_BINARY_WRITABLE|LB_BINARY_ACTIVE_WRITER)) abort();
    count=atomic_fetch_sub_explicit(&binary->intern.refc,1,memory_order_acq_rel);
    if(!count || (flags && count!=1)) abort();
    if(count==1) {
        LbBinaryOwner *owner=(LbBinaryOwner *)((unsigned char *)binary-sizeof(LbBinaryOwner));
        if(lb_alloc_domain_release(owner->owner.domain,owner)!=LB_ALLOC_OK) abort();
    }
}
size_t lb_bitstring_heap_words(size_t bits)
{
    if(bits>8u*(size_t)LB_MAX_BINARY_BYTES) return 0;
    return bits<=8*LB_ONHEAP_BINARY_LIMIT ? 2+(bits+63)/64 : LB_REFC_BITS_WORDS;
}
void lb_sub_bits_init(LbSubBits *sub,Uint flags,Eterm orig,const void *base,Uint offset,Uint bits)
{
    /* erl_sub_bits_init: low pointer bits are flags; compensate unalignment
     * with the bit offset. Match contexts remain outside this profile. */
    Uint adjustment=(Uint)base&3;
    assert(is_boxed(orig) && (flags==0 || flags==LB_SUB_VOLATILE || flags==LB_SUB_WRITABLE));
    sub->thing_word=LB_HEADER_SUB_BITS;
    sub->start=offset+adjustment*8; sub->end=sub->start+bits;
    sub->base_flags=((Uint)base-adjustment)|flags; sub->orig=orig;
}
static int overhead_fits(const LbOffHeap *offheap,size_t bytes)
{
    return bytes/sizeof(Eterm)<=UINT64_MAX-offheap->overhead;
}
static void link_ref(LbOffHeap *offheap,LbBinRef *ref)
{
    uint64_t words=(Uint)ref->val->orig_size/sizeof(Eterm);
    assert(overhead_fits(offheap,(size_t)ref->val->orig_size));
    ref->next=offheap->first; offheap->first=ref; offheap->overhead+=words;
}
static Eterm wrap_binary(LbOffHeap *offheap,Eterm **heap,LbBinary *binary,size_t bits,Uint flags)
{
    LbBinRef *ref=(LbBinRef *)*heap;
    LbSubBits *sub=(LbSubBits *)(*heap+LB_BIN_REF_WORDS);
    ref->thing_word=LB_HEADER_BIN_REF; ref->val=binary;
    lb_sub_bits_init(sub,0,make_boxed((Eterm *)ref)|flags,binary->orig_bytes,0,bits);
    link_ref(offheap,ref); *heap+=LB_REFC_BITS_WORDS;
    return make_boxed((Eterm *)sub)|flags;
}
LbAllocStatus lb_bitstring_build(LbAllocDomain *domain,LbOffHeap *offheap,Eterm **heap,
                                const void *bytes,size_t bits,Uint flags,Eterm *out)
{
    size_t words=lb_bitstring_heap_words(bits),size;
    unsigned char *data;
    LbBinary *binary=NULL;
    Eterm result;
    if(!domain || !offheap || !heap || !*heap || !_is_taggable_pointer(*heap) || !out || !words ||
       (bits && !bytes) || (flags!=0 && flags!=TAG_LITERAL_PTR)) return LB_ALLOC_INVALID;
    size=(bits+7)/8; /* bounded above before rounding */
    if(size>LB_ONHEAP_BINARY_LIMIT) {
        if(!overhead_fits(offheap,size)) return LB_ALLOC_NO_MEMORY;
        binary=lb_binary_allocate(domain,size);
        if(!binary) return LB_ALLOC_NO_MEMORY;
        data=binary->orig_bytes;
    } else {
        (*heap)[0]=_make_header(words-1,HEAP_BITS_SUBTAG); (*heap)[1]=bits;
        data=(unsigned char *)(*heap+2);
        memset(data,0,(words-2)*sizeof(Eterm));
    }
    if(size) {
        memcpy(data,bytes,size);
        if(bits&7) data[size-1]&=(unsigned char)(0xffu<<(8-(bits&7)));
    }
    if(binary) result=wrap_binary(offheap,heap,binary,bits,flags);
    else { result=make_boxed(*heap)|flags; *heap+=words; }
    *out=result; return LB_ALLOC_OK;
}
void lb_offheap_rollback(LbOffHeap *offheap,LbBinRef *stop)
{
    while(offheap->first!=stop) {
        LbBinRef *ref=offheap->first;
        uint64_t words;
        if(!ref || ref->thing_word!=LB_HEADER_BIN_REF) abort();
        words=(Uint)ref->val->orig_size/sizeof(Eterm);
        if(words>offheap->overhead) abort();
        offheap->first=ref->next; offheap->overhead-=words;
        lb_binary_release(ref->val);
    }
}
void lb_offheap_clear(LbOffHeap *offheap)
{
    lb_offheap_rollback(offheap,NULL);
    assert(offheap->overhead==0);
}
void lb_offheap_sweep(LbOffHeap *offheap)
{
    /* erl_gc.c:sweep_off_heap's fullsweep. Moved headers transfer their one
     * reference; unforwarded headers are dead. No increment for GC movement. */
    LbBinRef **previous=&offheap->first,*ref=offheap->first;
    offheap->overhead=0;
    while(ref) {
        if(is_boxed(ref->thing_word)) {
            *previous=ref=(LbBinRef *)boxed_val(ref->thing_word);
            assert(ref->thing_word==LB_HEADER_BIN_REF && !ref->val->intern.flags);
            offheap->overhead+=(Uint)ref->val->orig_size/sizeof(Eterm);
            previous=&ref->next; ref=ref->next;
        } else {
            if(ref->thing_word!=LB_HEADER_BIN_REF) abort();
            lb_binary_release(ref->val);
            *previous=ref=ref->next;
        }
    }
}
int lb_bitstring_copy_ref(const LbSubBits *source,LbOffHeap *offheap,Eterm **heap,Eterm *out)
{
    const LbBinRef *from=(const LbBinRef *)boxed_val(source->orig);
    LbSubBits *sub=(LbSubBits *)*heap;
    LbBinRef *ref=(LbBinRef *)(*heap+LB_SUB_BITS_WORDS);
    assert(source->thing_word==LB_HEADER_SUB_BITS && (source->base_flags&3)!=LB_SUB_MUTABLE);
    assert(from->thing_word==LB_HEADER_BIN_REF && !from->val->intern.flags);
    if(!overhead_fits(offheap,(size_t)from->val->orig_size) || !lb_binary_retain(from->val)) return 0;
    /* copy_struct's paired outer/inner copy, no write to immutable source. */
    *sub=*source; *ref=*from; sub->orig=make_boxed((Eterm *)ref);
    if(source->base_flags&LB_SUB_VOLATILE) sub->base_flags=(Uint)from->val->orig_bytes;
    link_ref(offheap,ref); *heap+=LB_REFC_BITS_WORDS; *out=make_boxed((Eterm *)sub);
    return 1;
}
int lb_bitstring_view(Eterm value,LbBitstringView *view)
{
    Eterm *pointer;
    if(!view) return 0;
    *view=(LbBitstringView){0};
    if(!is_boxed(value)) return 0;
    pointer=boxed_val(value);
    if(*pointer==LB_HEADER_SUB_BITS) {
        const LbSubBits *sub=(const LbSubBits *)pointer;
        assert((sub->base_flags&3)!=LB_SUB_MUTABLE && sub->start<=sub->end);
        if(sub->base_flags&LB_SUB_VOLATILE) {
            const LbBinRef *ref=(const LbBinRef *)boxed_val(sub->orig);
            assert(ref->thing_word==LB_HEADER_BIN_REF);
            /* Native volatile lookup without cache mutation: read-only
             * equality/view consumers remain read-only. */
            view->data=ref->val->orig_bytes;
        } else view->data=(const unsigned char *)sub->base_flags;
        view->bit_offset=sub->start; view->bit_size=sub->end-sub->start;
        return 1;
    }
    if((*pointer&_TAG_HEADER_MASK)==HEAP_BITS_SUBTAG) {
        view->data=(const unsigned char *)(pointer+2); view->bit_size=pointer[1]; return 1;
    }
    return 0;
}

int lb_bitstring_pin(Eterm value)
{
    LbBitstringView view; LbSubBits *sub; LbBinRef *ref;
    if(!lb_bitstring_view(value,&view)) return 0;
    if(*boxed_val(value)!=LB_HEADER_SUB_BITS) return 1;
    sub=(LbSubBits *)boxed_val(value);
    if(!(sub->base_flags&LB_SUB_VOLATILE)) return 1;
    ref=(LbBinRef *)boxed_val(sub->orig);
    if(ref->val->intern.flags) {
        if(atomic_load(&ref->val->intern.refc)!=1) abort();
        ref->val->intern.flags=0;
    }
    sub->base_flags=(Uint)ref->val->orig_bytes;
    return 1;
}

void lb_writable_sweep(LbOffHeap *writable,LbOffHeap *normal)
{
    LbBinRef **link=&writable->first,*ref=writable->first;
    writable->overhead=0;
    while(ref) {
        if(!is_boxed(ref->thing_word)) {
            LbBinRef *next=ref->next;
            if(ref->thing_word!=LB_HEADER_BIN_REF) abort();
            lb_binary_release(ref->val); *link=ref=next; continue;
        }
        *link=ref=(LbBinRef *)boxed_val(ref->thing_word);
        if(ref->thing_word!=LB_HEADER_BIN_REF) abort();
        if(ref->val->intern.flags) {
            if(!(ref->val->intern.flags&LB_BINARY_WRITABLE) || atomic_load(&ref->val->intern.refc)!=1) abort();
            ref->val->intern.flags&=~LB_BINARY_ACTIVE_WRITER;
            writable->overhead+=(Uint)ref->val->orig_size/sizeof(Eterm);
            link=&ref->next; ref=ref->next;
        } else {
            LbBinRef *next=ref->next;
            link_ref(normal,ref); *link=ref=next;
        }
    }
}
