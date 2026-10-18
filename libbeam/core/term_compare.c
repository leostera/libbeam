/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1996-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Selected utils.c:eq and erl_bits.c/h comparison algorithms. Native tagged
 * terms and nonrecursive word-stack traversal; owned fallible scratch replaces
 * ERTS's fatal temporary allocator. See otp/term-compare-sources.json.
 */
#include "term_compare.h"
#include "flatmap.h"
#include "reference.h"
#include "fun.h"
#include "big.h"
#include "work_budget.h"
#include <string.h>
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
    if(!lb_work_domain_charge(domain,1)) goto work_limit;
    switch(primary_tag(a)) {
    case TAG_PRIMARY_LIST:
        if(is_list(b)) {
            Eterm *aval=list_val(a),*bval=list_val(b);
            for(;;) {
                if(!lb_work_domain_charge(domain,1)) goto work_limit;
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
        case ARITYVAL_SUBTAG:case RECORD_SUBTAG:
            aa=boxed_val(a);
            if(!is_boxed(b) || *boxed_val(b)!=*aa) goto not_equal;
            bb=boxed_val(b); sz=(Sint)(*aa>>_HEADER_ARITY_OFFS);
            if(!sz) goto pop_next;
            ++aa; ++bb; goto term_array;
        case FUN_SUBTAG: {
            const LbFun *af=lb_fun(a),*bf=lb_fun(b);
            if(!bf || af->thing_word!=bf->thing_word) goto not_equal;
            if(lb_fun_is_external(af)) {
                if(af->entry.exp!=bf->entry.exp) goto not_equal;
            } else if(af->entry.fun!=bf->entry.fun) goto not_equal;
            sz=(Sint)lb_fun_free(af); if(!sz) goto pop_next;
            aa=(Eterm *)af->env; bb=(Eterm *)bf->env; goto term_array;
        }
        case REF_SUBTAG: {
            const LbOrdinaryRef *ar=lb_reference(a),*br=lb_reference(b);
            if(br && ar->num[0]==br->num[0] && ar->num[1]==br->num[1] && ar->num[2]==br->num[2]) goto pop_next;
            break;
        }
        case MAP_SUBTAG: {
            aa=boxed_val(a); (void)lb_map_object_words(aa);
            if(!is_boxed(b) || *boxed_val(b)!=*aa) goto not_equal;
            bb=boxed_val(b); (void)lb_map_object_words(bb);
            size_t offset=lb_map_object_child_offset(aa);
            size_t count=lb_map_object_child_count(aa);
            if(offset==2 && aa[0]!=LB_FLATMAP_HEADER && aa[1]!=bb[1]) goto not_equal;
            if(aa[0]==LB_FLATMAP_HEADER && aa[1]!=bb[1]) goto not_equal;
            if(!count) goto pop_next;
            aa+=offset; bb+=offset; sz=(Sint)count; goto term_array;
        }
        case HEAP_BITS_SUBTAG:case SUB_BITS_SUBTAG: {
            LbBitstringView av,bv;
            if(!lb_bitstring_view(a,&av)) abort();
            if(!lb_bitstring_view(b,&bv)) goto not_equal;
            if(av.bit_size==bv.bit_size && !lb_work_domain_charge(domain,lb_work_bulk_units((av.bit_size+7)/8))) goto work_limit;
            if(lb_bitstrings_equal(av,bv)) goto pop_next;
            break;
        }
        case POS_BIG_SUBTAG:case NEG_BIG_SUBTAG: {
            size_t i;
            if(!is_boxed(b)) goto not_equal;
            aa=boxed_val(a); bb=boxed_val(b);
            if(*aa!=*bb) goto not_equal;
            i=*aa>>_HEADER_ARITY_OFFS;
            while(i--) {
                if(!lb_work_domain_charge(domain,1)) goto work_limit;
                if(*++aa!=*++bb) goto not_equal;
            }
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
            if(!lb_work_domain_charge(domain,1)) goto work_limit;
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
work_limit:
    clear_stack(domain,&stack); return LB_ALLOC_WORK_LIMIT;
}
static int float_value(Eterm term,double *value)
{
    Uint bits;
    if(!is_boxed(term) || (*boxed_val(term)&_TAG_HEADER_MASK)!=FLOAT_SUBTAG) return 0;
    bits=boxed_val(term)[1]; memcpy(value,&bits,sizeof(*value)); return 1;
}
static LbAllocStatus integer_float_equal(LbAllocDomain *domain,Eterm integer,double value,int *equal)
{
    uint64_t bits,significand; Uint small_magnitude=0; int negative,e2; size_t count=0,n; const Uint *digits;
    Uint float_digits[18]={0}; unsigned exponent; memcpy(&bits,&value,sizeof(bits));
    exponent=(unsigned)((bits>>52)&0x7ff); significand=bits&((UINT64_C(1)<<52)-1);
    if(exponent==0x7ff) { *equal=0; return LB_ALLOC_OK; }
    negative=(int)(bits>>63);
    if(exponent) { significand|=UINT64_C(1)<<52; e2=(int)exponent-1023-52; }
    else e2=-1074;
    if(!significand) { *equal=is_small(integer) && signed_val(integer)==0; return LB_ALLOC_OK; }
    if(e2<0) {
        unsigned shift=(unsigned)-e2;
        if(shift>=64 || (significand&((UINT64_C(1)<<shift)-1))) { *equal=0; return LB_ALLOC_OK; }
        significand>>=shift; e2=0;
        if(!significand) { *equal=is_small(integer) && signed_val(integer)==0; return LB_ALLOC_OK; }
    }
    if(e2/64>=18) { *equal=0; return LB_ALLOC_OK; }
    size_t word=(unsigned)e2/64; unsigned bit=(unsigned)e2%64;
    float_digits[word]=significand<<bit; count=word+1;
    if(bit && (significand>>(64-bit))) { float_digits[word+1]=significand>>(64-bit); count=word+2; }
    if(is_small(integer)) {
        Sint value_int=signed_val(integer); negative=value_int<0;
        small_magnitude=negative?(Uint)0-(Uint)value_int:(Uint)value_int;
        digits=&small_magnitude; n=1;
    } else {
        const Uint *boxed=boxed_val(integer);
        negative=(*boxed&_TAG_HEADER_MASK)==NEG_BIG_SUBTAG;
        digits=boxed+1; n=lb_big_size(integer);
    }
    if(negative!=(int)(bits>>63) && (count || (n==1 && digits[0]))) { *equal=0; return LB_ALLOC_OK; }
    if(n!=count) { *equal=0; return LB_ALLOC_OK; }
    if(!lb_work_domain_charge(domain,count?count:1))
        return lb_alloc_domain_failure(domain)==LB_ALLOC_WORK_LIMIT?LB_ALLOC_WORK_LIMIT:LB_ALLOC_NO_MEMORY;
    for(size_t i=0;i<count;++i) if(digits[i]!=float_digits[i]) { *equal=0; return LB_ALLOC_OK; }
    *equal=1; return LB_ALLOC_OK;
}
LbAllocStatus lb_term_loose_equal(LbAllocDomain *domain,Eterm a,Eterm b,int *equal)
{
    double left,right; int left_float=float_value(a,&left),right_float=float_value(b,&right);
    if(!domain || !equal) return LB_ALLOC_INVALID;
    if(left_float && right_float) { *equal=left==right; return LB_ALLOC_OK; }
    if(left_float && lb_is_integer(b)) return integer_float_equal(domain,b,left,equal);
    if(right_float && lb_is_integer(a)) return integer_float_equal(domain,a,right,equal);
    return lb_term_equal(domain,a,b,equal);
}
