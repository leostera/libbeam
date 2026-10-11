/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1996-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Selected utils.c:eq and erl_bits.c/h comparison algorithms. Native tagged
 * terms and nonrecursive word-stack traversal; owned fallible scratch replaces
 * ERTS's fatal temporary allocator. See otp/term-compare-sources.json.
 */
#include "term_compare.h"
#include <stdlib.h>

typedef unsigned char byte;
static byte get_bit(byte b,size_t offset) { return (b>>(7-offset))&1; }
static int compare_unaligned(const byte *a_ptr,Uint a_offs,const byte *b_ptr,Uint b_offs,Uint size)
{
    Uint lshift,rshift; byte a_bit,b_bit,a,b; int cmp;
    assert(((a_offs|b_offs|size)&7)!=0 && ((a_offs|b_offs)&~(Uint)7)==0 && size>0);
    a=*a_ptr++; b=*b_ptr++;
    if(a_offs) {
        for(;;) {
            a_bit=get_bit(a,a_offs); b_bit=get_bit(b,b_offs);
            if((cmp=a_bit-b_bit)!=0) return cmp;
            if(--size==0) return 0;
            if(++b_offs==8) { b_offs=0; b=*b_ptr++; }
            if(++a_offs==8) { a_offs=0; a=*a_ptr++; break; }
        }
    }
    if(size>=8) {
        lshift=b_offs; rshift=8-lshift;
        for(;;) {
            byte b_cmp=(byte)(b<<lshift);
            /* Native code eagerly reads the next byte. Exact-sized owned
             * payloads need no padding: with zero shift and a final full byte,
             * the lookahead contributes no bits and must not be read. */
            if(lshift || size>8) { b=*b_ptr++; b_cmp|=b>>rshift; }
            if((cmp=a-b_cmp)!=0) return cmp;
            size-=8; if(size<8) break;
            a=*a_ptr++;
        }
        if(size==0) return 0;
        a=*a_ptr++;
    }
    for(;;) {
        a_bit=get_bit(a,a_offs); b_bit=get_bit(b,b_offs);
        if((cmp=a_bit-b_bit)!=0) return cmp;
        if(--size==0) return 0;
        ++a_offs; assert(a_offs<8);
        if(++b_offs==8) { b_offs=0; b=*b_ptr++; }
    }
}
int lb_bitstrings_equal(LbBitstringView a,LbBitstringView b)
{
    const byte *ap,*bp;
    if(a.bit_size!=b.bit_size) return 0;
    if(!a.bit_size) return 1;
    ap=a.data+a.bit_offset/8; bp=b.data+b.bit_offset/8;
    if(((a.bit_offset|b.bit_offset|a.bit_size)&7)==0)
        return !memcmp(ap,bp,a.bit_size/8);
    return !compare_unaligned(ap,a.bit_offset&7,bp,b.bit_offset&7,a.bit_size);
}

typedef struct { Uint local[16],*words; size_t count,capacity; } WordStack;
static void clear_stack(LbAllocDomain *domain,WordStack *stack)
{
    if(stack->words!=stack->local && lb_alloc_domain_release(domain,stack->words)!=LB_ALLOC_OK) abort();
}
static int reserve(LbAllocDomain *domain,WordStack *stack,size_t need)
{
    const size_t max_words=64u*1024u*1024u/sizeof(Uint);
    size_t capacity=stack->capacity; void *memory;
    if(need<=capacity-stack->count) return 1;
    do { if(capacity>max_words/2) return 0; capacity*=2; } while(need>capacity-stack->count);
    if(lb_alloc_domain_allocate(domain,capacity*sizeof(Uint),&memory)!=LB_ALLOC_OK) return 0;
    memcpy(memory,stack->words,stack->count*sizeof(Uint)); clear_stack(domain,stack);
    stack->words=memory; stack->capacity=capacity; return 1;
}
#define PUSH2(x,y) do { if(!reserve(domain,&stack,2)) goto no_memory; \
    stack.words[stack.count++]=(x); stack.words[stack.count++]=(y); } while(0)
#define PUSH3(x,y,z) do { if(!reserve(domain,&stack,3)) goto no_memory; \
    stack.words[stack.count++]=(x); stack.words[stack.count++]=(y); stack.words[stack.count++]=(z); } while(0)
#define POP() (assert(stack.count),stack.words[--stack.count])
LbAllocStatus lb_term_equal(LbAllocDomain *domain,Eterm a,Eterm b,int *equal)
{
    WordStack stack; Sint sz; Eterm *aa,*bb;
    if(!domain || !equal) return LB_ALLOC_INVALID;
    stack.words=stack.local; stack.count=0; stack.capacity=16;
tailrecur:
    if(a==b) goto pop_next;
tailrecur_ne:
    switch(primary_tag(a)) {
    case TAG_PRIMARY_LIST:
        if(is_list(b)) {
            Eterm *aval=list_val(a),*bval=list_val(b);
            for(;;) {
                Eterm atmp=CAR(aval),btmp=CAR(bval);
                if(atmp!=btmp) {
                    PUSH2(CDR(bval),CDR(aval)); a=atmp; b=btmp; goto tailrecur_ne;
                }
                atmp=CDR(aval); btmp=CDR(bval);
                if(atmp==btmp) goto pop_next;
                if(!is_list(atmp) || !is_list(btmp)) { a=atmp; b=btmp; goto tailrecur_ne; }
                aval=list_val(atmp); bval=list_val(btmp);
            }
        }
        break;
    case TAG_PRIMARY_BOXED:
        switch(*boxed_val(a)&_TAG_HEADER_MASK) {
        case ARITYVAL_SUBTAG:
            aa=tuple_val(a);
            if(!is_boxed(b) || *boxed_val(b)!=*aa) goto not_equal;
            bb=tuple_val(b); sz=(Sint)arityval(*aa);
            if(!sz) goto pop_next;
            ++aa; ++bb; goto term_array;
        case HEAP_BITS_SUBTAG:case SUB_BITS_SUBTAG: {
            LbBitstringView av,bv;
            if(!lb_bitstring_view(a,&av)) abort();
            if(!lb_bitstring_view(b,&bv)) goto not_equal;
            if(lb_bitstrings_equal(av,bv)) goto pop_next;
            break;
        }
        case POS_BIG_SUBTAG:case NEG_BIG_SUBTAG: {
            size_t i;
            if(!is_boxed(b)) goto not_equal;
            aa=boxed_val(a); bb=boxed_val(b);
            if(*aa!=*bb) goto not_equal;
            i=*aa>>_HEADER_ARITY_OFFS;
            while(i--) if(*++aa!=*++bb) goto not_equal;
            goto pop_next;
        }
        case FLOAT_SUBTAG:
            /* Native exact equality compares FloatDef.fdw, not C double ==.
             * In particular +0.0 and -0.0 are distinct exact terms. */
            if(is_boxed(b) && (*boxed_val(b)&_TAG_HEADER_MASK)==FLOAT_SUBTAG &&
               boxed_val(a)[1]==boxed_val(b)[1]) goto pop_next;
            break;
        default: abort(); /* unadmitted boxed type, not an equality miss */
        }
        break;
    case TAG_PRIMARY_IMMED1: break;
    default: abort(); /* non-term internal header cannot be a guest operand */
    }
    goto not_equal;
term_array:
    assert(sz>0);
    {
        Eterm *ap=aa,*bp=bb; Sint i=sz;
        for(;;) {
            if(*ap!=*bp) break;
            if(--i==0) goto pop_next;
            ++ap; ++bp;
        }
        a=*ap; b=*bp;
        if(is_immed(a) && is_immed(b)) goto not_equal;
        if(i>1) PUSH3((Uint)(i-1),(Uint)(bp+1),(Uint)(ap+1)|TAG_PRIMARY_HEADER);
        goto tailrecur_ne;
    }
pop_next:
    if(stack.count) {
        Uint something=POP();
        if(primary_tag(something)==TAG_PRIMARY_HEADER) {
            aa=(Eterm *)something; bb=(Eterm *)POP(); sz=(Sint)POP(); goto term_array;
        }
        a=something; b=POP(); goto tailrecur;
    }
    clear_stack(domain,&stack); *equal=1; return LB_ALLOC_OK;
not_equal:
    clear_stack(domain,&stack); *equal=0; return LB_ALLOC_OK;
no_memory:
    clear_stack(domain,&stack); return LB_ALLOC_NO_MEMORY;
}
