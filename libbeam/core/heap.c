/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1996-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Full-copy collector adapted from erl_gc.h move_cons/move_boxed and erl_gc.c
 * sweep. Explicit heap/stack/X/result/exception roots replace global Process
 * machinery. Ordinary immutable binary references join tuple/list/big/float
 * layouts. No generational collector, writable binaries, funs or maps yet.
 */
#include "process_internal.h"
#define MAX_HEAP_WORDS (64u*1024u*1024u/sizeof(Eterm))
static size_t boxed_words(Eterm header)
{
    size_t arity=(size_t)(header>>_HEADER_ARITY_OFFS);
    ASSERT(is_header(header));
    switch(header&_HEADER_SUBTAG_MASK) {
    case ARITYVAL_SUBTAG:return arity?arity+1:2; /* owned empty-tuple read-ahead */
    case POS_BIG_SUBTAG:case NEG_BIG_SUBTAG:case FLOAT_SUBTAG:case HEAP_BITS_SUBTAG:return arity+1;
    case BIN_REF_SUBTAG:ASSERT(header==LB_HEADER_BIN_REF); return LB_BIN_REF_WORDS;
    case SUB_BITS_SUBTAG:ASSERT(header==LB_HEADER_SUB_BITS); return LB_SUB_BITS_WORDS;
    default:abort(); /* internal invariant: these representations are not admitted */
    }
}
static void move_root(Eterm *root,Eterm **top)
{
    Eterm term=*root,*ptr,*dst=*top;
    size_t words;
    if(is_boxed(term) && !is_literal_ptr(term)) {
        ptr=boxed_val(term);
        if(!is_header(*ptr)) { *root=*ptr; return; }
        words=boxed_words(*ptr);
        memcpy(dst,ptr,words*sizeof(Eterm));
        *root=make_boxed(dst); *ptr=*root; *top+=words;
    } else if(is_list(term) && !is_literal_ptr(term)) {
        ptr=list_val(term);
        if(is_non_value(ptr[0])) { *root=ptr[1]; return; }
        dst[0]=ptr[0]; dst[1]=ptr[1]; *root=make_list(dst);
        ptr[0]=THE_NON_VALUE; ptr[1]=*root; *top+=2;
    }
}
int lb_process_collect_live(LbProcess *p,size_t need,size_t live)
{
    LbAllocDomain *domain=p->entry_module->space->domain;
    Eterm *heap,*top,*scan,*stop,*old=p->heap;
    size_t stack=(size_t)(p->hend-p->stop),used=(size_t)(p->htop-p->heap),size,i;
    void *memory;
    if(need<p->heap_reserved) need=p->heap_reserved;
    if(live>MAX_REG || !lb_size_add(used,stack,&size) || !lb_size_add(size,need,&size) ||
       !lb_size_add(size,S_RESERVED,&size) || size>MAX_HEAP_WORDS) return 0;
    if(size<16) size=16;
    if(size<=MAX_HEAP_WORDS/2) size*=2;
    if(lb_alloc_domain_allocate(domain,size*sizeof(Eterm),&memory)!=LB_ALLOC_OK) return 0;
    heap=top=memory; stop=heap+size-stack;
    memcpy(stop,p->stop,stack*sizeof(Eterm));
    /* Commit: tospace capacity bounds all unique live objects by the old used
     * heap. No allocation/failure follows the first forwarding write. */
    for(i=0;i<live;++i) move_root(&p->reg[i],&top);
    for(i=live;i<MAX_REG;++i) p->reg[i]=NIL;
    move_root(&p->fvalue,&top); move_root(&p->freason,&top);
    for(i=0;i<stack;++i) move_root(&stop[i],&top); /* CP words are primary headers */
    scan=heap;
    while(scan<top) {
        Eterm value=*scan;
        if(is_header(value)) {
            if(is_arity_value(value)) scan+=(value>>_HEADER_ARITY_OFFS)?1:2;
            else {
                if(value==LB_HEADER_SUB_BITS) {
                    LbSubBits *sub=(LbSubBits *)scan;
                    /* Ordinary immutable binaries keep their data address;
                     * only the heap BinRef moves. No match context is admitted. */
                    ASSERT(!(sub->base_flags&3));
                    move_root(&sub->orig,&top);
                    ASSERT(*boxed_val(sub->orig)==LB_HEADER_BIN_REF);
                }
                scan+=boxed_words(value);
            }
        } else { move_root(scan,&top); ++scan; }
    }
    lb_offheap_sweep(&p->off_heap);
    ASSERT(top<=stop && (size_t)(stop-top)>=need+S_RESERVED);
    p->heap=heap; p->htop=top; p->stop=stop; p->hend=heap+size;
    p->last_gc_cost=1+(Uint)(top-heap)/16; ++p->collections;
    lb_release(domain,old); return 1;
}
int lb_heap_reserve(LbProcess *p,size_t need,size_t live)
{
    if(need>MAX_HEAP_WORDS) return 0;
    return (size_t)(p->stop-p->htop)>=need+S_RESERVED || lb_process_collect_live(p,need,live);
}
/* size_object/copy_struct style nonrecursive traversal for passive literal
 * metadata copied into a process. It never installs forwarding words in shared
 * immutable literals. Scratch is fallible and physically freed before return. */
int lb_flat_size(LbAllocDomain *domain,Eterm term,size_t *out)
{
    Eterm local[64],*todo=local;
    size_t count=1,capacity=64,total=0;
    int ok=0;
    todo[0]=term;
    while(count) {
        Eterm value=todo[--count],*children=NULL;
        size_t n=0,words=0;
        if(is_list(value)) { children=list_val(value); n=words=2; }
        else if(is_boxed(value)) {
            Eterm *p=boxed_val(value); words=boxed_words(*p);
            if(is_arity_value(*p)) { n=(size_t)(*p>>_HEADER_ARITY_OFFS); children=p+1; }
            else if(*p==LB_HEADER_SUB_BITS) words=LB_REFC_BITS_WORDS;
            else if(*p==LB_HEADER_BIN_REF) goto done; /* not a user term */
        } else if(!is_immed(value)) goto done;
        if(words>MAX_HEAP_WORDS-total) goto done;
        total+=words;
        if(n>capacity-count) {
            Eterm *next; void *memory; size_t new_capacity=capacity;
            while(new_capacity<count+n) { if(new_capacity>MAX_HEAP_WORDS/2) goto done; new_capacity*=2; }
            if(lb_alloc_domain_allocate(domain,new_capacity*sizeof(Eterm),&memory)!=LB_ALLOC_OK) goto done;
            next=memory; memcpy(next,todo,count*sizeof(Eterm));
            if(todo!=local) lb_release(domain,todo);
            todo=next; capacity=new_capacity;
        }
        if(n) { memcpy(todo+count,children,n*sizeof(Eterm)); count+=n; }
    }
    *out=total; ok=1;
done:
    if(todo!=local) lb_release(domain,todo);
    return ok;
}
static int copy_root(Eterm *root,Eterm **top,LbOffHeap *offheap)
{
    Eterm *src,*dst=*top; size_t words;
    if(is_boxed(*root)) {
        src=boxed_val(*root);
        if(*src==LB_HEADER_SUB_BITS) return lb_bitstring_copy_ref((const LbSubBits *)src,offheap,top,root);
        words=boxed_words(*src); *root=make_boxed(dst);
    } else if(is_list(*root)) { src=list_val(*root); words=2; *root=make_list(dst); }
    else return 1;
    memcpy(dst,src,words*sizeof(Eterm)); *top+=words; return 1;
}
Eterm lb_copy_flat(Eterm value,Eterm **top,LbOffHeap *offheap)
{
    Eterm *begin=*top,*scan=begin;
    LbBinRef *checkpoint=offheap->first;
    if(!copy_root(&value,top,offheap)) goto fail;
    while(scan<*top) {
        if(is_header(*scan)) {
            if(is_arity_value(*scan)) scan+=(*scan>>_HEADER_ARITY_OFFS)?1:2;
            else scan+=boxed_words(*scan); /* SubBits and BinRef already paired */
        } else {
            if(!copy_root(scan,top,offheap)) goto fail;
            ++scan;
        }
    }
    return value;
fail:
    lb_offheap_rollback(offheap,checkpoint); *top=begin; return THE_NON_VALUE;
}
