/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@leostera.io>
 * Native iolist_to_binary/1 over owned lists and binary views. */
#include "process_internal.h"
#include "binary_internal.h"
#include "bit_copy.h"
#include "iolist_size.h"
#include <stdlib.h>

typedef struct { Eterm term; unsigned allow_byte; } LbIoListItem;

Eterm lb_bif_byte_size_1(LbProcess *process,Eterm *args,const BeamInstr *instruction)
{
    LbBitstringView view;
    (void)instruction; process->last_native_cost=1;
    if(!lb_bitstring_view(args[0],&view)) { process->freason=LB_EXC_BADARG; return THE_NON_VALUE; }
    return make_small((Sint)((view.bit_size+7)/8));
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
        if(!lb_work_domain_charge(domain,lb_work_bulk_units(size))) { status=LB_ALLOC_WORK_LIMIT; goto done; }
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
        allocation_failed(process,lb_alloc_domain_failure(domain)==LB_ALLOC_LIMIT?
                                  LB_ALLOC_LIMIT:LB_ALLOC_NO_MEMORY);
        return THE_NON_VALUE;
    }
    if(process->collections!=collections) process->last_native_cost+=process->last_gc_cost;
    status=lb_bitstring_build(domain,&process->off_heap,&process->htop,bytes,bits,0,&result);
    if(memory && lb_alloc_domain_release(domain,memory)!=LB_ALLOC_OK) abort();
    if(status!=LB_ALLOC_OK) { allocation_failed(process,status); return THE_NON_VALUE; }
    process->last_native_cost+=lb_work_bulk_units(size);
    return result;
}
