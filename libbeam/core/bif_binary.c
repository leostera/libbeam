/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@leostera.io>
 * Native iolist_to_binary/1 over owned lists and binary views. */
#include "process_internal.h"
#include "binary_internal.h"
#include "binary_match.h"
#include "bit_copy.h"
#include "iolist_size.h"
#include "md5.h"
#include <limits.h>
#include <stdlib.h>

typedef struct { Eterm term; unsigned allow_byte; } LbIoListItem;

static int bit_size_of(Eterm value,Uint *bits)
{
    LbBitstringView view;
    if(is_boxed(value) && *boxed_val(value)==LB_HEADER_SUB_BITS) {
        const LbSubBits *context=(const LbSubBits *)boxed_val(value);
        if(lb_is_match_context(context)) {
            if(context->start>context->end) return 0;
            *bits=context->end-context->start; return 1;
        }
    }
    if(!lb_bitstring_view(value,&view)) return 0;
    *bits=view.bit_size; return 1;
}

Eterm lb_bif_byte_size_1(LbProcess *process,Eterm *args,const BeamInstr *instruction)
{
    Uint bits;
    (void)instruction; process->last_native_cost=1;
    if(!bit_size_of(args[0],&bits)) { process->freason=LB_EXC_BADARG; return THE_NON_VALUE; }
    return make_small((Sint)(bits/8+(bits%8!=0)));
}

Eterm lb_bif_bit_size_1(LbProcess *p,Eterm *args,const BeamInstr *instruction)
{
    Uint bits; (void)instruction; p->last_native_cost=0;
    _Static_assert(LB_MAX_BINARY_BYTES<=MAX_SMALL/8,"owned bit counts fit small integers");
    if(!lb_work_domain_charge(p->entry_module->space->domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    p->last_native_cost=1;
    if(!bit_size_of(args[0],&bits)) { p->freason=LB_EXC_BADARG; return THE_NON_VALUE; }
    return make_small((Sint)bits);
}

static void allocation_failed(LbProcess *process,LbAllocStatus status)
{
    if(status==LB_ALLOC_LIMIT) process->status=LB_PROCESS_LIMIT;
    else if(status==LB_ALLOC_WORK_LIMIT) process->status=LB_PROCESS_WORK_LIMIT;
    else process->status=LB_PROCESS_NO_MEMORY;
}

/* binary.c:binary_to_list_1/3. The range is one-based and inclusive;
 * empty binaries are valid only for arity one. Parse before reserving, then
 * reacquire the moving binary view before publishing any cons cells. */
static Eterm binary_list(LbProcess *p,Eterm *args,size_t arity)
{
    LbAllocDomain *domain=p->entry_module->space->domain;
    LbBitstringView view; size_t start=0,count,words,need,work;
    p->last_native_cost=1;
    if(args!=p->reg) { p->status=LB_PROCESS_INVALID; return THE_NON_VALUE; }
    if(!lb_work_domain_charge(domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    if(!lb_bitstring_view(args[0],&view) || (view.bit_size&7)) goto badarg;
    size_t source_bits=view.bit_size;
    count=source_bits/8;
    if(arity==3) {
        /* Every valid index fits a small under the owned binary-size bound. */
        _Static_assert(LB_MAX_BINARY_BYTES<=MAX_SMALL,"binary indices fit small integers");
        if(!is_small(args[1]) || !is_small(args[2])) goto badarg;
        Sint first=signed_val(args[1]),last=signed_val(args[2]);
        if(first<1 || last<first || (Uint)last>count) goto badarg;
        start=(size_t)first-1; count=(size_t)(last-first)+1;
    }
    if(!count) return NIL;
    if(!lb_size_mul(count,2,&words) || !lb_size_add(words,p->heap_reserved,&need) || !lb_size_mul(count,3,&work)) {
        p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE;
    }
    if(!lb_work_domain_charge(domain,work)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    p->last_native_cost+=work;
    size_t before=p->collections;
    if(!lb_heap_reserve(p,need,arity)) {
        LbAllocStatus cause=lb_alloc_domain_failure(domain);
        allocation_failed(p,cause==LB_ALLOC_OK?LB_ALLOC_LIMIT:cause); return THE_NON_VALUE;
    }
    if(p->collections!=before) p->last_native_cost+=p->last_gc_cost;
    size_t end;
    if(!lb_bitstring_view(p->reg[0],&view) || view.bit_size!=source_bits ||
       !lb_size_add(view.bit_offset,view.bit_size,&end)) abort();
    Eterm result=NIL;
    for(size_t i=count;i>0;--i) {
        size_t bit=view.bit_offset+(start+i-1)*8,index=bit/8,shift=bit%8;
        if(bit>end || end-bit<8) abort();
        unsigned byte=view.data[index];
        if(shift) byte=((byte<<shift)|(view.data[index+1]>>(8-shift)))&255u;
        result=CONS(p->htop,make_small(byte),result); p->htop+=2;
    }
    return result;
badarg:
    p->freason=LB_EXC_BADARG; return THE_NON_VALUE;
}
Eterm lb_bif_binary_to_list_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{ (void)pc; return binary_list(p,args,1); }
Eterm lb_bif_binary_to_list_3(LbProcess *p,Eterm *args,const BeamInstr *pc)
{ (void)pc; return binary_list(p,args,3); }
Eterm lb_bif_bitstring_to_list_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    LbAllocDomain *domain=p->entry_module->space->domain;
    LbBitstringView view; size_t bits,full,remainder,count,words,tail_words,need,work;
    size_t collections=p->collections; void *tail_bytes=NULL; Eterm tail=NIL,result; LbAllocStatus status;
    (void)pc; p->last_native_cost=1;
    if(args!=p->reg) { p->status=LB_PROCESS_INVALID; return THE_NON_VALUE; }
    if(!lb_work_domain_charge(domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    if(!lb_bitstring_view(args[0],&view)) { p->freason=LB_EXC_BADARG; return THE_NON_VALUE; }
    bits=view.bit_size; full=bits/8; remainder=bits&7; count=full+(remainder?1:0);
    tail_words=remainder?lb_bitstring_heap_words(remainder):0;
    if(!lb_size_mul(full,2,&words) || !lb_size_add(words,tail_words,&words) ||
       (remainder && !lb_size_add(words,2,&words)) ||
       !lb_size_add(words,p->heap_reserved,&need) || !lb_size_mul(count,3,&work)) {
        p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE;
    }
    if(!lb_work_domain_charge(domain,(Uint)work)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    p->last_native_cost+=work;
    if(remainder) {
        status=lb_alloc_domain_allocate(domain,1,&tail_bytes);
        if(status!=LB_ALLOC_OK) { allocation_failed(p,status); return THE_NON_VALUE; }
    }
    if(!lb_heap_reserve(p,need,MAX_REG)) {
        if(tail_bytes && lb_alloc_domain_release(domain,tail_bytes)!=LB_ALLOC_OK) abort();
        allocation_failed(p,lb_alloc_domain_failure(domain)); return THE_NON_VALUE;
    }
    if(p->collections!=collections) p->last_native_cost+=p->last_gc_cost;
    if(!lb_bitstring_view(p->reg[0],&view) || view.bit_size!=bits) abort();
    if(remainder) {
        size_t source_end;
        if(!lb_size_add(view.bit_offset,view.bit_size,&source_end) || !lb_size_add(source_end,7,&source_end) ||
           !lb_bits_copy(view.data,source_end/8,view.bit_offset+full*8,tail_bytes,1,0,remainder)) abort();
        status=lb_bitstring_build(domain,&p->off_heap,&p->htop,tail_bytes,remainder,0,&tail);
        if(tail_bytes && lb_alloc_domain_release(domain,tail_bytes)!=LB_ALLOC_OK) abort();
        if(status!=LB_ALLOC_OK) { allocation_failed(p,status); return THE_NON_VALUE; }
        p->last_native_cost+=1+lb_work_bulk_units(1);
        tail=CONS(p->htop,tail,NIL); p->htop+=2;
    }
    size_t end;
    if(!lb_size_add(view.bit_offset,view.bit_size,&end)) abort();
    result=tail;
    for(size_t i=full;i>0;--i) {
        size_t bit=view.bit_offset+(i-1)*8,index=bit/8,shift=bit%8;
        if(bit>end || end-bit<8) abort();
        unsigned byte=view.data[index];
        if(shift) byte=((byte<<shift)|(view.data[index+1]>>(8-shift)))&255u;
        result=CONS(p->htop,make_small(byte),result); p->htop+=2;
    }
    return result;
}

static LbAllocStatus push_item(LbProcess *process,LbIoListItem **items,size_t *count,
                               size_t *capacity,LbIoListItem item)
{
    LbAllocDomain *domain=process->entry_module->space->domain;
    if(*count==*capacity) {
        size_t next,bytes; void *memory=NULL;
        if(*capacity>SIZE_MAX/2 || !lb_size_mul(*capacity*2,sizeof(**items),&bytes)) return LB_ALLOC_LIMIT;
        next=*capacity*2;
        LbAllocStatus status=lb_alloc_domain_allocate(domain,bytes,&memory);
        if(status!=LB_ALLOC_OK) return status;
        memcpy(memory,*items,*count*sizeof(**items));
        if(lb_alloc_domain_release(domain,*items)!=LB_ALLOC_OK) abort();
        *items=memory; *capacity=next;
    }
    (*items)[(*count)++]=item; return LB_ALLOC_OK;
}

/* Validates and optionally copies one complete iodata value without collecting.
 * The explicit pending stack preserves roots while binary bytes are copied. */
static LbAllocStatus walk_iolist(LbProcess *process,Eterm input,unsigned char *output,
                                size_t output_capacity,size_t *length,int *valid)
{
    LbAllocDomain *domain=process->entry_module->space->domain;
    LbIoListItem *items=NULL; size_t count=0,capacity=16,total=0,bytes;
    LbAllocStatus status=lb_size_mul(capacity,sizeof(*items),&bytes)?LB_ALLOC_OK:LB_ALLOC_LIMIT;
    *valid=0; *length=0;
    if(status==LB_ALLOC_OK) status=lb_alloc_domain_allocate(domain,bytes,(void **)&items);
    if(status!=LB_ALLOC_OK) return status;
    items[count++]=(LbIoListItem){input,0};
    while(count) {
        LbIoListItem item=items[--count]; Eterm term=item.term;
        if(!lb_work_domain_charge(domain,1)) { status=LB_ALLOC_WORK_LIMIT; goto done; }
        ++process->last_native_cost;
        if(is_nil(term)) continue;
        if(is_list(term)) {
            Eterm *cell=list_val(term);
            status=push_item(process,&items,&count,&capacity,(LbIoListItem){cell[1],0});
            if(status!=LB_ALLOC_OK) goto done;
            status=push_item(process,&items,&count,&capacity,(LbIoListItem){cell[0],1});
            if(status!=LB_ALLOC_OK) goto done;
            continue;
        }
        if(is_small(term)) {
            Sint byte=signed_val(term);
            if(!item.allow_byte || byte<0 || byte>255) goto invalid;
            if(total==LB_MAX_BINARY_BYTES) goto too_large;
            if(output) output[total]=(unsigned char)byte;
            ++total; continue;
        }
        LbBitstringView view;
        if(!lb_bitstring_view(term,&view) || (view.bit_size&7)) goto invalid;
        size_t size=view.bit_size/8;
        if(size>LB_MAX_BINARY_BYTES-total) goto too_large;
        Uint bulk=lb_work_bulk_units(size);
        if(!lb_work_domain_charge(domain,bulk)) { status=LB_ALLOC_WORK_LIMIT; goto done; }
        process->last_native_cost+=bulk;
        if(output && size) {
            size_t source_bytes;
            if(!lb_size_add(view.bit_offset,view.bit_size,&source_bytes) ||
               !lb_size_add(source_bytes,7,&source_bytes) ||
               !lb_bits_copy(view.data,source_bytes/8,view.bit_offset,output,output_capacity,
                             total*8,view.bit_size)) { status=LB_ALLOC_INVALID; goto done; }
        }
        total+=size;
    }
    *length=total; *valid=1; status=LB_ALLOC_OK;
    goto done;
invalid:
    *valid=0; status=LB_ALLOC_OK; goto done;
too_large:
    *valid=-1; status=LB_ALLOC_OK;
done:
    if(lb_alloc_domain_release(domain,items)!=LB_ALLOC_OK) abort();
    return status;
}

static LbAllocStatus walk_bitstring(LbProcess *process,Eterm input,unsigned char *output,
                                    size_t output_capacity,size_t *bit_length,int *valid)
{
    LbAllocDomain *domain=process->entry_module->space->domain;
    LbIoListItem *items=NULL; size_t count=0,capacity=16,bytes;
    LbAllocStatus status=lb_size_mul(capacity,sizeof(*items),&bytes)?LB_ALLOC_OK:LB_ALLOC_LIMIT;
    size_t total=0,maximum=8u*(size_t)LB_MAX_BINARY_BYTES;
    *valid=0; *bit_length=0;
    if(status==LB_ALLOC_OK) status=lb_alloc_domain_allocate(domain,bytes,(void **)&items);
    if(status!=LB_ALLOC_OK) return status;
    items[count++]=(LbIoListItem){input,0};
    while(count) {
        if(!lb_work_domain_charge(domain,1)) { status=LB_ALLOC_WORK_LIMIT; goto done; }
        ++process->last_native_cost; LbIoListItem item=items[--count]; Eterm term=item.term;
        if(is_nil(term)) continue;
        if(is_list(term)) {
            Eterm *cell=list_val(term);
            status=push_item(process,&items,&count,&capacity,(LbIoListItem){cell[1],0});
            if(status==LB_ALLOC_OK) status=push_item(process,&items,&count,&capacity,(LbIoListItem){cell[0],1});
            if(status!=LB_ALLOC_OK) goto done;
            continue;
        }
        if(is_small(term)) {
            Sint byte=signed_val(term);
            if(!item.allow_byte || byte<0 || byte>255) goto invalid;
            if(total>maximum-8) goto too_large;
            if(output) {
                unsigned char value=(unsigned char)byte;
                if(!lb_bits_copy(&value,1,0,output,output_capacity,total,8)) { status=LB_ALLOC_INVALID; goto done; }
            }
            total+=8; continue;
        }
        LbBitstringView view; size_t source_end,source_bytes,segment_bytes;
        if(!lb_bitstring_view(term,&view)) goto invalid;
        if(view.bit_size>maximum-total) goto too_large;
        segment_bytes=(view.bit_size+7)/8;
        Uint bulk=lb_work_bulk_units(segment_bytes);
        if(!lb_work_domain_charge(domain,bulk)) { status=LB_ALLOC_WORK_LIMIT; goto done; }
        process->last_native_cost+=bulk;
        if(output && view.bit_size) {
            if(!lb_size_add(view.bit_offset,view.bit_size,&source_end) ||
               !lb_size_add(source_end,7,&source_end)) { status=LB_ALLOC_LIMIT; goto done; }
            source_bytes=source_end/8;
            if(!lb_bits_copy(view.data,source_bytes,view.bit_offset,output,output_capacity,
                             total,view.bit_size)) { status=LB_ALLOC_INVALID; goto done; }
        }
        total+=view.bit_size;
    }
    *bit_length=total; *valid=1; status=LB_ALLOC_OK; goto done;
invalid:
    *valid=0; status=LB_ALLOC_OK; goto done;
too_large:
    *valid=-1; status=LB_ALLOC_OK;
done:
    if(lb_alloc_domain_release(domain,items)!=LB_ALLOC_OK) abort();
    return status;
}

Eterm lb_bif_list_to_bitstring_1(LbProcess *process,Eterm *args,const BeamInstr *instruction)
{
    LbAllocDomain *domain=process->entry_module->space->domain;
    unsigned char *bytes=NULL; void *memory=NULL; size_t bits=0,copied_bits=0,byte_count,full_bytes,remainder;
    size_t items,words,need,work,collections=process->collections; int valid=0;
    Eterm result,tail=NIL; LbAllocStatus status;
    (void)instruction; process->last_native_cost=0;
    if(args!=process->reg) { process->status=LB_PROCESS_INVALID; return THE_NON_VALUE; }
    if(!lb_work_domain_charge(domain,1)) { process->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    ++process->last_native_cost;
    status=walk_bitstring(process,args[0],NULL,0,&bits,&valid);
    if(status!=LB_ALLOC_OK) { allocation_failed(process,status); return THE_NON_VALUE; }
    if(valid<0) { process->freason=LB_EXC_SYSTEM_LIMIT; return THE_NON_VALUE; }
    if(!valid) { process->freason=LB_EXC_BADARG; return THE_NON_VALUE; }
    byte_count=(bits+7)/8; full_bytes=bits/8; remainder=bits&7;
    if(byte_count) {
        status=lb_alloc_domain_allocate(domain,byte_count,&memory);
        if(status!=LB_ALLOC_OK) { allocation_failed(process,status); return THE_NON_VALUE; }
        bytes=memory;
    }
    status=walk_bitstring(process,args[0],bytes,byte_count,&copied_bits,&valid);
    if(status!=LB_ALLOC_OK || valid!=1 || copied_bits!=bits) {
        if(memory && lb_alloc_domain_release(domain,memory)!=LB_ALLOC_OK) abort();
        if(status==LB_ALLOC_OK) { process->status=LB_PROCESS_INVALID; return THE_NON_VALUE; }
        allocation_failed(process,status); return THE_NON_VALUE;
    }
    items=full_bytes+(remainder?1:0);
    size_t tail_words=remainder?lb_bitstring_heap_words(remainder):0;
    if(!lb_size_mul(full_bytes,2,&words) || !lb_size_add(words,tail_words,&words) ||
       !lb_size_add(words,process->heap_reserved,&need) || !lb_size_mul(items,3,&work)) {
        if(memory && lb_alloc_domain_release(domain,memory)!=LB_ALLOC_OK) abort();
        process->status=LB_PROCESS_LIMIT; return THE_NON_VALUE;
    }
    if(!lb_work_domain_charge(domain,(Uint)work)) {
        if(memory && lb_alloc_domain_release(domain,memory)!=LB_ALLOC_OK) abort();
        process->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE;
    }
    process->last_native_cost+=work;
    if(!lb_heap_reserve(process,need,MAX_REG)) {
        if(memory && lb_alloc_domain_release(domain,memory)!=LB_ALLOC_OK) abort();
        allocation_failed(process,lb_alloc_domain_failure(domain)); return THE_NON_VALUE;
    }
    if(process->collections!=collections) process->last_native_cost+=process->last_gc_cost;
    if(remainder) {
        unsigned char tail_byte=0;
        if(!lb_bits_copy(bytes,byte_count,full_bytes*8,&tail_byte,1,0,remainder)) abort();
        status=lb_bitstring_build(domain,&process->off_heap,&process->htop,&tail_byte,remainder,0,&tail);
        if(status!=LB_ALLOC_OK) {
            if(memory && lb_alloc_domain_release(domain,memory)!=LB_ALLOC_OK) abort();
            allocation_failed(process,status); return THE_NON_VALUE;
        }
        process->last_native_cost+=1+lb_work_bulk_units(1);
    }
    result=tail;
    for(size_t i=full_bytes;i>0;--i) {
        result=CONS(process->htop,make_small(bytes[i-1]),result); process->htop+=2;
    }
    if(memory && lb_alloc_domain_release(domain,memory)!=LB_ALLOC_OK) abort();
    return result;
}

Eterm lb_bif_binary_part_3(LbProcess *process,Eterm *args,const BeamInstr *instruction)
{
    LbAllocDomain *domain=process->entry_module->space->domain;
    LbBitstringView view; Sint start,length; size_t total,offset,size,bits,words,need,source_bytes;
    unsigned char *copy=NULL; void *memory=NULL; Eterm result; LbAllocStatus status;
    (void)instruction; process->last_native_cost=1;
    if(!lb_bitstring_view(args[0],&view) || (view.bit_size&7) ||
       !is_small(args[1]) || !is_small(args[2])) goto badarg;
    start=signed_val(args[1]); length=signed_val(args[2]); total=view.bit_size/8;
    if(length<0) goto badarg;
    if(start<0) { if(start<-(Sint)total) goto badarg; start+=(Sint)total; }
    if(start<0 || (Uint)start>total || (Uint)length>total-(size_t)start) goto badarg;
    offset=(size_t)start; size=(size_t)length;
    if(!lb_size_mul(size,8,&bits) || !(words=lb_bitstring_heap_words(bits)) ||
       !lb_size_add(words,process->heap_reserved,&need)) { process->freason=LB_EXC_SYSTEM_LIMIT; return THE_NON_VALUE; }
    if(size) {
        if(!lb_work_domain_charge(domain,1+lb_work_bulk_units(size))) {
            process->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE;
        }
        status=lb_alloc_domain_allocate(domain,size,&memory);
        if(status!=LB_ALLOC_OK) { allocation_failed(process,status); return THE_NON_VALUE; }
        copy=memory;
        if(!lb_size_add(view.bit_offset,view.bit_size,&source_bytes) ||
           !lb_size_add(source_bytes,7,&source_bytes) ||
           !lb_bits_copy(view.data,source_bytes/8,view.bit_offset+offset*8,copy,size,0,bits)) {
            if(lb_alloc_domain_release(domain,memory)!=LB_ALLOC_OK) abort();
            process->status=LB_PROCESS_INVALID; return THE_NON_VALUE;
        }
    }
    size_t collections=process->collections;
    if(!lb_heap_reserve(process,need,MAX_REG)) {
        if(memory && lb_alloc_domain_release(domain,memory)!=LB_ALLOC_OK) abort();
        allocation_failed(process,lb_alloc_domain_failure(domain)==LB_ALLOC_LIMIT?LB_ALLOC_LIMIT:LB_ALLOC_NO_MEMORY);
        return THE_NON_VALUE;
    }
    if(process->collections!=collections) process->last_native_cost+=process->last_gc_cost;
    status=lb_bitstring_build(domain,&process->off_heap,&process->htop,copy,bits,0,&result);
    if(memory && lb_alloc_domain_release(domain,memory)!=LB_ALLOC_OK) abort();
    if(status!=LB_ALLOC_OK) { allocation_failed(process,status); return THE_NON_VALUE; }
    process->last_native_cost+=lb_work_bulk_units(size); return result;
badarg:
    process->freason=LB_EXC_BADARG; return THE_NON_VALUE;
}

int lb_iolist_size_add(LbIolistSize *size,size_t bytes)
{
    if(bytes>LB_MAX_BINARY_BYTES) return 0;
    Uint next=size->low+(Uint)bytes;
    if(next<size->low) { if(size->high==UINTPTR_MAX) return 0; ++size->high; }
    size->low=next; return 1;
}
Eterm lb_iolist_size_result(LbProcess *p,LbIolistSize size)
{
    if(!size.high && size.low<=MAX_SMALL) return make_small((Sint)size.low);
    size_t words=size.high?3:2,need,before=p->collections;
    if(!lb_size_add(words,p->heap_reserved,&need)) { p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE; }
    if(!lb_heap_reserve(p,need,MAX_REG)) {
        LbAllocStatus cause=lb_alloc_domain_failure(p->entry_module->space->domain);
        allocation_failed(p,cause==LB_ALLOC_OK?LB_ALLOC_LIMIT:cause); return THE_NON_VALUE;
    }
    if(p->collections!=before) p->last_native_cost+=p->last_gc_cost;
    Eterm *hp=p->htop; hp[0]=_make_header(words-1,POS_BIG_SUBTAG); hp[1]=size.low;
    if(size.high) hp[2]=size.high;
    p->htop+=words; return make_boxed(hp);
}
Eterm lb_bif_iolist_size_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)pc; p->last_native_cost=1;
    LbAllocDomain *domain=p->entry_module->space->domain;
    if(!lb_work_domain_charge(domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    Eterm input=args[0]; LbBitstringView view;
    if(is_nil(input)) return make_small(0);
    if(!is_list(input)) {
        if(!lb_bitstring_view(input,&view) || (view.bit_size&7)) { p->freason=LB_EXC_BADARG; return THE_NON_VALUE; }
        return lb_iolist_size_result(p,(LbIolistSize){view.bit_size/8,0});
    }
    size_t count=0,capacity=16; LbIoListItem *items=NULL;
    LbAllocStatus status=lb_alloc_domain_allocate(domain,capacity*sizeof(*items),(void **)&items);
    if(status!=LB_ALLOC_OK) { allocation_failed(p,status); return THE_NON_VALUE; }
    LbIolistSize size={0}; int valid=1;
    items[count++]=(LbIoListItem){input,0};
    /* Borrowed terms remain in a nonmoving parent until all pending views have
     * been released. Counting reads metadata, not the binary's bytes. */
    while(count) {
        if(!lb_work_domain_charge(domain,1)) { status=LB_ALLOC_WORK_LIMIT; break; }
        ++p->last_native_cost;
        LbIoListItem item=items[--count]; Eterm term=item.term; size_t bytes;
        if(is_nil(term)) continue;
        if(is_list(term)) {
            Eterm *cell=list_val(term);
            status=push_item(p,&items,&count,&capacity,(LbIoListItem){cell[1],0});
            if(status==LB_ALLOC_OK) status=push_item(p,&items,&count,&capacity,(LbIoListItem){cell[0],1});
            if(status!=LB_ALLOC_OK) break;
            continue;
        }
        if(is_small(term)) {
            Sint byte=signed_val(term);
            if(!item.allow_byte || byte<0 || byte>255) { valid=0; break; }
            bytes=1;
        } else {
            if(!lb_bitstring_view(term,&view) || (view.bit_size&7)) { valid=0; break; }
            bytes=view.bit_size/8;
        }
        if(!lb_iolist_size_add(&size,bytes)) { status=LB_ALLOC_LIMIT; break; }
    }
    if(lb_alloc_domain_release(domain,items)!=LB_ALLOC_OK) abort();
    if(status!=LB_ALLOC_OK) { allocation_failed(p,status); return THE_NON_VALUE; }
    if(!valid) { p->freason=LB_EXC_BADARG; return THE_NON_VALUE; }
    return lb_iolist_size_result(p,size);
}
Eterm lb_bif_list_to_binary_1(LbProcess *process,Eterm *args,const BeamInstr *instruction)
{ return lb_bif_iolist_to_binary_1(process,args,instruction); }
Eterm lb_bif_iolist_to_iovec_1(LbProcess *process,Eterm *args,const BeamInstr *instruction)
{
    LbAllocDomain *domain=process->entry_module->space->domain;
    unsigned char *bytes=NULL; size_t size=0,bits,words,need,collections=process->collections;
    int valid=0; Eterm binary; void *memory=NULL; LbAllocStatus status;
    (void)instruction; process->last_native_cost=0;
    status=walk_iolist(process,args[0],NULL,0,&size,&valid);
    if(status!=LB_ALLOC_OK) { allocation_failed(process,status); return THE_NON_VALUE; }
    if(valid<0) { process->freason=LB_EXC_SYSTEM_LIMIT; return THE_NON_VALUE; }
    if(!valid) { process->freason=LB_EXC_BADARG; return THE_NON_VALUE; }
    if(size==0) return NIL;
    if(!lb_size_mul(size,8,&bits) || !(words=lb_bitstring_heap_words(bits)) ||
       !lb_size_add(words,2,&need) || !lb_size_add(need,process->heap_reserved,&need)) {
        process->freason=LB_EXC_SYSTEM_LIMIT; return THE_NON_VALUE;
    }
    status=lb_alloc_domain_allocate(domain,size,(void **)&memory);
    if(status!=LB_ALLOC_OK) { allocation_failed(process,status); return THE_NON_VALUE; }
    bytes=memory; size_t copied=0;
    status=walk_iolist(process,args[0],bytes,size,&copied,&valid);
    if(status!=LB_ALLOC_OK || valid!=1 || copied!=size) {
        if(lb_alloc_domain_release(domain,memory)!=LB_ALLOC_OK) abort();
        if(status==LB_ALLOC_OK) { process->status=LB_PROCESS_INVALID; return THE_NON_VALUE; }
        allocation_failed(process,status); return THE_NON_VALUE;
    }
    if(!lb_work_domain_charge(domain,2)) {
        if(lb_alloc_domain_release(domain,memory)!=LB_ALLOC_OK) abort();
        process->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE;
    }
    process->last_native_cost+=2;
    if(!lb_heap_reserve(process,need,MAX_REG)) {
        if(lb_alloc_domain_release(domain,memory)!=LB_ALLOC_OK) abort();
        allocation_failed(process,lb_alloc_domain_failure(domain)); return THE_NON_VALUE;
    }
    if(process->collections!=collections) process->last_native_cost+=process->last_gc_cost;
    status=lb_bitstring_build(domain,&process->off_heap,&process->htop,bytes,bits,0,&binary);
    if(lb_alloc_domain_release(domain,memory)!=LB_ALLOC_OK) abort();
    if(status!=LB_ALLOC_OK) { allocation_failed(process,status); return THE_NON_VALUE; }
    process->last_native_cost+=1+lb_work_bulk_units(size);
    process->htop[0]=binary; process->htop[1]=NIL;
    Eterm result=make_list(process->htop); process->htop+=2; return result;
}
Eterm lb_bif_iolist_to_binary_1(LbProcess *process,Eterm *args,const BeamInstr *instruction)
{
    LbAllocDomain *domain=process->entry_module->space->domain;
    unsigned char *bytes=NULL; size_t size=0,bits,words,need,collections=process->collections;
    int valid=0; Eterm result; void *memory=NULL; LbAllocStatus status;
    (void)instruction; process->last_native_cost=0;
    status=walk_iolist(process,args[0],NULL,0,&size,&valid);
    if(status!=LB_ALLOC_OK) { allocation_failed(process,status); return THE_NON_VALUE; }
    if(valid<0) { process->freason=LB_EXC_SYSTEM_LIMIT; return THE_NON_VALUE; }
    if(!valid) { process->freason=LB_EXC_BADARG; return THE_NON_VALUE; }
    if(!lb_size_mul(size,8,&bits) || !(words=lb_bitstring_heap_words(bits)) ||
       !lb_size_add(words,process->heap_reserved,&need)) {
        process->freason=LB_EXC_SYSTEM_LIMIT; return THE_NON_VALUE;
    }
    if(size) {
        status=lb_alloc_domain_allocate(domain,size,(void **)&memory);
        if(status!=LB_ALLOC_OK) { allocation_failed(process,status); return THE_NON_VALUE; }
        bytes=memory;
    }
    size_t copied=0;
    status=walk_iolist(process,args[0],bytes,size,&copied,&valid);
    if(status!=LB_ALLOC_OK || valid!=1 || copied!=size) {
        if(memory && lb_alloc_domain_release(domain,memory)!=LB_ALLOC_OK) abort();
        if(status==LB_ALLOC_OK) { process->status=LB_PROCESS_INVALID; return THE_NON_VALUE; }
        allocation_failed(process,status); return THE_NON_VALUE;
    }
    if(!lb_heap_reserve(process,need,MAX_REG)) {
        if(memory && lb_alloc_domain_release(domain,memory)!=LB_ALLOC_OK) abort();
        allocation_failed(process,lb_alloc_domain_failure(domain));
        return THE_NON_VALUE;
    }
    if(process->collections!=collections) process->last_native_cost+=process->last_gc_cost;
    status=lb_bitstring_build(domain,&process->off_heap,&process->htop,bytes,bits,0,&result);
    if(memory && lb_alloc_domain_release(domain,memory)!=LB_ALLOC_OK) abort();
    if(status!=LB_ALLOC_OK) { allocation_failed(process,status); return THE_NON_VALUE; }
    process->last_native_cost+=1+lb_work_bulk_units(size);
    return result;
}
Eterm lb_bif_decode_packet_3(LbProcess *p,Eterm *args,const BeamInstr *instruction)
{
    (void)instruction; LbAllocDomain *domain=p->entry_module->space->domain;
    p->last_native_cost=1;
    if(!lb_work_domain_charge(domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    unsigned header=0,max_packet=0; int raw=0;
    if(is_small(args[0])) {
        Sint type=signed_val(args[0]);
        if(type==0) raw=1; else if(type==1) header=1; else if(type==2) header=2; else if(type==4) header=4;
        else { p->freason=LB_EXC_BADARG; return THE_NON_VALUE; }
    } else if(args[0]==am_raw) raw=1;
    else { p->freason=LB_EXC_BADARG; return THE_NON_VALUE; }
    if(!lb_is_binary(args[1]) || (!is_list(args[2]) && !is_nil(args[2]))) {
        p->freason=LB_EXC_BADARG; return THE_NON_VALUE;
    }
    Eterm options=args[2];
    while(is_list(options)) {
        if(!lb_work_domain_charge(domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
        ++p->last_native_cost; Eterm option=CAR(list_val(options));
        if(!is_tuple(option) || arityval(*tuple_val(option))!=2 || tuple_val(option)[1]!=am_packet_size ||
           !is_small(tuple_val(option)[2]) || signed_val(tuple_val(option)[2])<0 ||
           (uint64_t)signed_val(tuple_val(option)[2])>UINT_MAX) { p->freason=LB_EXC_BADARG; return THE_NON_VALUE; }
        max_packet=(unsigned)signed_val(tuple_val(option)[2]); options=CDR(list_val(options));
    }
    if(!is_nil(options)) { p->freason=LB_EXC_BADARG; return THE_NON_VALUE; }
    LbBitstringView view;
    if(!lb_bitstring_view(args[1],&view) || (view.bit_size&7)) { p->freason=LB_EXC_BADARG; return THE_NON_VALUE; }
    if(!raw) {
        if(!lb_work_domain_charge(domain,header)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
        p->last_native_cost+=header;
    }
    size_t input_size=view.bit_size/8,packet_size=0,body_offset=0,body_size=0;
    int incomplete=0,invalid=0;
    if(raw) {
        if(!input_size) incomplete=1;
        else { packet_size=input_size; body_size=input_size; }
    } else if(input_size<header) incomplete=1;
    else {
        uint64_t payload=0;
        for(unsigned i=0;i<header;++i) payload=(payload<<8)|view.data[view.bit_offset/8+i];
        if(max_packet && payload>max_packet) invalid=1;
        else if(payload>SIZE_MAX-header) invalid=1;
        else {
            packet_size=(size_t)payload+header;
            if(packet_size>input_size) incomplete=1;
            else { body_offset=header; body_size=(size_t)payload; }
        }
    }
    size_t need_words=4,body_bits=0,rest_bits=0,words,collections=p->collections;
    if(incomplete) need_words=3;
    else if(invalid) need_words=3;
    else {
        if(!lb_size_mul(body_size,8,&body_bits) || !lb_size_mul(input_size-packet_size,8,&rest_bits)) {
            p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE;
        }
        size_t body_words=lb_bitstring_heap_words(body_bits),rest_words=lb_bitstring_heap_words(rest_bits);
        if(!body_words || !rest_words || !lb_size_add(body_words,rest_words,&need_words) ||
           !lb_size_add(need_words,4,&need_words)) { p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE; }
    }
    Uint tuple_work=incomplete || invalid?3:4;
    if(!lb_work_domain_charge(domain,tuple_work)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    p->last_native_cost+=tuple_work;
    if(!lb_size_add(need_words,p->heap_reserved,&words)) { p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE; }
    if(!lb_heap_reserve(p,words,MAX_REG)) { allocation_failed(p,lb_alloc_domain_failure(domain)); return THE_NON_VALUE; }
    if(p->collections!=collections) p->last_native_cost+=p->last_gc_cost;
    if(incomplete || invalid) {
        Eterm *tuple=p->htop; tuple[0]=make_arityval(2);
        tuple[1]=incomplete?am_more:am_error;
        if(invalid) tuple[2]=am_invalid;
        else if(input_size<header || packet_size==0) tuple[2]=am_undefined;
        else tuple[2]=make_small((Sint)packet_size);
        Eterm result=make_tuple(tuple); p->htop+=3; return result;
    }
    if(!lb_bitstring_view(args[1],&view)) abort();
    const unsigned char *source=view.data+view.bit_offset/8;
    Eterm *checkpoint=p->htop; LbBinRef *offheap_checkpoint=p->off_heap.first;
    Eterm body,rest;
    LbAllocStatus status=lb_bitstring_build(domain,&p->off_heap,&p->htop,source+body_offset,body_bits,0,&body);
    if(status==LB_ALLOC_OK) p->last_native_cost+=1+lb_work_bulk_units(body_size);
    if(status==LB_ALLOC_OK) {
        status=lb_bitstring_build(domain,&p->off_heap,&p->htop,source+packet_size,rest_bits,0,&rest);
        if(status==LB_ALLOC_OK) p->last_native_cost+=1+lb_work_bulk_units(input_size-packet_size);
    }
    if(status!=LB_ALLOC_OK) {
        p->htop=checkpoint; lb_offheap_rollback(&p->off_heap,offheap_checkpoint);
        allocation_failed(p,status); return THE_NON_VALUE;
    }
    Eterm *tuple=p->htop; tuple[0]=make_arityval(3); tuple[1]=am_ok; tuple[2]=body; tuple[3]=rest;
    Eterm result=make_tuple(tuple); p->htop+=4; return result;
}
static int md5_state_from_term(Eterm term,erts_md5_state *state)
{
    LbBitstringView view;
    if(!lb_is_binary(term) || !lb_bitstring_view(term,&view) ||
       (view.bit_size&7) || view.bit_size!=sizeof(*state)*8) return 0;
    memcpy(state,view.data+view.bit_offset/8,sizeof(*state));
    return state->len<64 && state->tot_len%64==0;
}
static Eterm md5_state_binary(LbProcess *p,const erts_md5_state *state)
{
    LbAllocDomain *domain=p->entry_module->space->domain; size_t bits,words,need,collections=p->collections;
    if(!lb_size_mul(sizeof(*state),8,&bits)) { p->freason=LB_EXC_SYSTEM_LIMIT; return THE_NON_VALUE; }
    words=lb_bitstring_heap_words(bits);
    if(!words || !lb_size_add(words,p->heap_reserved,&need)) { p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE; }
    Uint work=lb_work_bulk_units(sizeof(*state));
    if(!lb_work_domain_charge(domain,work)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    p->last_native_cost+=work;
    if(!lb_heap_reserve(p,need,MAX_REG)) { allocation_failed(p,lb_alloc_domain_failure(domain)); return THE_NON_VALUE; }
    if(p->collections!=collections) p->last_native_cost+=p->last_gc_cost;
    Eterm result; LbAllocStatus status=lb_bitstring_build(domain,&p->off_heap,&p->htop,state,bits,0,&result);
    if(status!=LB_ALLOC_OK) { allocation_failed(p,status); return THE_NON_VALUE; }
    p->last_native_cost+=1+work; return result;
}
Eterm lb_bif_md5_init_0(LbProcess *p,Eterm *args,const BeamInstr *instruction)
{
    (void)args; (void)instruction; p->last_native_cost=1;
    if(!lb_work_domain_charge(p->entry_module->space->domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    erts_md5_state state; memset(&state,0,sizeof(state)); lb_md5_init(&state);
    return md5_state_binary(p,&state);
}
Eterm lb_bif_md5_update_2(LbProcess *p,Eterm *args,const BeamInstr *instruction)
{
    (void)instruction; LbAllocDomain *domain=p->entry_module->space->domain;
    unsigned char *bytes=NULL; void *memory=NULL; size_t size=0,copied=0; int valid=0;
    p->last_native_cost=1;
    if(!lb_work_domain_charge(domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    erts_md5_state state;
    if(!md5_state_from_term(args[0],&state)) { p->freason=LB_EXC_BADARG; return THE_NON_VALUE; }
    LbAllocStatus status=walk_iolist(p,args[1],NULL,0,&size,&valid);
    if(status!=LB_ALLOC_OK) { allocation_failed(p,status); return THE_NON_VALUE; }
    if(valid<0) { p->freason=LB_EXC_SYSTEM_LIMIT; return THE_NON_VALUE; }
    if(!valid) { p->freason=LB_EXC_BADARG; return THE_NON_VALUE; }
    if(state.tot_len>UINT64_MAX-state.len || size>UINT64_MAX-state.tot_len-state.len) {
        p->freason=LB_EXC_SYSTEM_LIMIT; return THE_NON_VALUE;
    }
    if(size) {
        status=lb_alloc_domain_allocate(domain,size,(void **)&memory);
        if(status!=LB_ALLOC_OK) { allocation_failed(p,status); return THE_NON_VALUE; }
        bytes=memory;
    }
    status=walk_iolist(p,args[1],bytes,size,&copied,&valid);
    if(status!=LB_ALLOC_OK || valid!=1 || copied!=size) {
        if(memory && lb_alloc_domain_release(domain,memory)!=LB_ALLOC_OK) abort();
        if(status==LB_ALLOC_OK) { p->status=LB_PROCESS_INVALID; return THE_NON_VALUE; }
        allocation_failed(p,status); return THE_NON_VALUE;
    }
    Uint work=lb_work_bulk_units(size);
    if(!lb_work_domain_charge(domain,work)) {
        if(memory && lb_alloc_domain_release(domain,memory)!=LB_ALLOC_OK) abort();
        p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE;
    }
    p->last_native_cost+=work;
    static const uint8_t empty=0; lb_md5_update(&state,bytes?bytes:&empty,size);
    Eterm result=md5_state_binary(p,&state);
    if(memory && lb_alloc_domain_release(domain,memory)!=LB_ALLOC_OK) abort();
    return result;
}
Eterm lb_bif_md5_final_1(LbProcess *p,Eterm *args,const BeamInstr *instruction)
{
    (void)instruction; LbAllocDomain *domain=p->entry_module->space->domain; p->last_native_cost=1;
    if(!lb_work_domain_charge(domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    erts_md5_state state;
    if(!md5_state_from_term(args[0],&state)) { p->freason=LB_EXC_BADARG; return THE_NON_VALUE; }
    size_t bits=sizeof(uint8_t)*MD5_DIGEST_LENGTH*8,words,need,collections=p->collections;
    Uint work=lb_work_bulk_units(MD5_DIGEST_LENGTH);
    if(!lb_work_domain_charge(domain,work)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    p->last_native_cost+=work;
    uint8_t digest[MD5_DIGEST_LENGTH]; lb_md5_finish(digest,&state);
    words=lb_bitstring_heap_words(bits);
    if(!words || !lb_size_add(words,p->heap_reserved,&need)) { p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE; }
    if(!lb_heap_reserve(p,need,MAX_REG)) { allocation_failed(p,lb_alloc_domain_failure(domain)); return THE_NON_VALUE; }
    if(p->collections!=collections) p->last_native_cost+=p->last_gc_cost;
    Eterm result; LbAllocStatus status=lb_bitstring_build(domain,&p->off_heap,&p->htop,digest,bits,0,&result);
    if(status!=LB_ALLOC_OK) { allocation_failed(p,status); return THE_NON_VALUE; }
    p->last_native_cost+=1+work; return result;
}
Eterm lb_bif_md5_1(LbProcess *process,Eterm *args,const BeamInstr *instruction)
{
    LbAllocDomain *domain=process->entry_module->space->domain;
    unsigned char *bytes=NULL; void *memory=NULL; size_t size=0,need,words,collections=process->collections;
    int valid=0; Eterm result; LbAllocStatus status; uint8_t digest[16];
    (void)instruction; process->last_native_cost=0;
    if(!lb_work_domain_charge(domain,1)) { process->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    ++process->last_native_cost;
    status=walk_iolist(process,args[0],NULL,0,&size,&valid);
    if(status!=LB_ALLOC_OK) { allocation_failed(process,status); return THE_NON_VALUE; }
    if(valid<0) { process->freason=LB_EXC_SYSTEM_LIMIT; return THE_NON_VALUE; }
    if(!valid) { process->freason=LB_EXC_BADARG; return THE_NON_VALUE; }
    if(size) {
        status=lb_alloc_domain_allocate(domain,size,(void **)&memory);
        if(status!=LB_ALLOC_OK) { allocation_failed(process,status); return THE_NON_VALUE; }
        bytes=memory;
    }
    size_t copied=0;
    status=walk_iolist(process,args[0],bytes,size,&copied,&valid);
    if(status!=LB_ALLOC_OK || valid!=1 || copied!=size) {
        if(memory && lb_alloc_domain_release(domain,memory)!=LB_ALLOC_OK) abort();
        if(status==LB_ALLOC_OK) { process->status=LB_PROCESS_INVALID; return THE_NON_VALUE; }
        allocation_failed(process,status); return THE_NON_VALUE;
    }
    Uint hash_work=lb_work_bulk_units(size);
    if(!lb_work_domain_charge(domain,hash_work)) {
        if(memory && lb_alloc_domain_release(domain,memory)!=LB_ALLOC_OK) abort();
        process->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE;
    }
    process->last_native_cost+=hash_work;
    words=lb_bitstring_heap_words(sizeof(digest)*8);
    if(!words || !lb_size_add(words,process->heap_reserved,&need)) {
        if(memory && lb_alloc_domain_release(domain,memory)!=LB_ALLOC_OK) abort();
        process->freason=LB_EXC_SYSTEM_LIMIT; return THE_NON_VALUE;
    }
    if(!lb_heap_reserve(process,need,MAX_REG)) {
        if(memory && lb_alloc_domain_release(domain,memory)!=LB_ALLOC_OK) abort();
        allocation_failed(process,lb_alloc_domain_failure(domain)); return THE_NON_VALUE;
    }
    if(process->collections!=collections) process->last_native_cost+=process->last_gc_cost;
    static const uint8_t empty=0;
    lb_md5(bytes?bytes:&empty,size,digest);
    status=lb_bitstring_build(domain,&process->off_heap,&process->htop,digest,sizeof(digest)*8,0,&result);
    if(memory && lb_alloc_domain_release(domain,memory)!=LB_ALLOC_OK) abort();
    if(status!=LB_ALLOC_OK) { allocation_failed(process,status); return THE_NON_VALUE; }
    process->last_native_cost+=1+lb_work_bulk_units(sizeof(digest)); return result;
}
