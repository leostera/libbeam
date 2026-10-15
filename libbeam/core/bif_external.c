/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1996-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Owned, transactional external-term conversion. Parsing uses the same
 * native-derived ETF arena as module preparation, never another serving VM.
 */
#include "process_internal.h"
#include "beam_program_internal.h"
#include "alloc_internal.h"
#include "work_budget_internal.h"
#include "bit_copy.h"
#include "flatmap.h"
#include "big.h"
#include <string.h>

static int charge(LbProcess *p,uint64_t work)
{
    if(!lb_work_domain_charge(p->entry_module->space->domain,work)) { p->status=LB_PROCESS_WORK_LIMIT; return 0; }
    p->last_native_cost+=work; return 1;
}
static int bind_atoms(LbProcess *p,LbBeamProgram *arena)
{
    if(!arena->name_count) return 1;
    size_t count=arena->name_count,at=0; uint64_t name_bytes=0;
    for(LbNamePatch *patch=arena->names;patch;patch=patch->next) {
        if(!charge(p,1)) return 0;
        name_bytes+=patch->name.size;
    }
    /* Reserve worst-case existing/staged hash-chain comparison work, atom
     * rehashing, and registry walks caused by interleaved transaction storage.
     * Atom/arena limits bound these products below uint64_t. */
    uint64_t atoms=lb_atoms_count(arena->atoms),extra=count+8;
    uint64_t work=2*(atoms+count+1)*(name_bytes+count)+64*(atoms+count+1)
        +extra*(4*(uint64_t)arena->block_count+extra+16);
    if(!charge(p,work)) return 0;
    LbBeamBytes *names=lb_palloc(arena,count,sizeof(*names));
    Eterm *terms=lb_palloc(arena,count,sizeof(*terms));
    if(!names || !terms) return 0;
    for(LbNamePatch *patch=arena->names;patch;patch=patch->next) names[at++]=patch->name;
    LbAtomStatus status=lb_atoms_prepare_names(arena->atoms,names,count,terms,&arena->transaction);
    if(status!=LB_ATOM_OK) {
        lb_pfail(arena,status==LB_ATOM_NO_MEMORY?LB_BEAM_NO_MEMORY:
            status==LB_ATOM_LIMIT?LB_BEAM_LIMIT:status==LB_ATOM_BUSY?LB_BEAM_UNSUPPORTED:LB_BEAM_BAD_FORMAT);
        return 0;
    }
    at=0;
    for(LbNamePatch *patch=arena->names;patch;patch=patch->next) *patch->destination=terms[at++];
    return 1;
}
static void failure(LbProcess *p,LbBeamStatus parsed)
{
    LbAllocStatus cause=lb_alloc_domain_failure(p->entry_module->space->domain);
    if(cause==LB_ALLOC_WORK_LIMIT) p->status=LB_PROCESS_WORK_LIMIT;
    else if(cause==LB_ALLOC_LIMIT) p->status=LB_PROCESS_LIMIT;
    else if(cause==LB_ALLOC_NO_MEMORY || parsed==LB_BEAM_NO_MEMORY) p->status=LB_PROCESS_NO_MEMORY;
    else if(parsed==LB_BEAM_UNSUPPORTED) p->status=LB_PROCESS_REFUSED;
    else if(parsed==LB_BEAM_LIMIT) p->freason=LB_EXC_SYSTEM_LIMIT;
    else p->freason=LB_EXC_BADARG;
}
Eterm lb_bif_binary_to_term_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    LbAllocDomain *domain=p->entry_module->space->domain; LbBitstringView view;
    LbWorkBudget *budget=lb_work_domain_owner(domain); uint64_t before=budget?budget->remaining:0;
    LbBeamProgram *arena=NULL; void *input=NULL,*memory; Eterm decoded=THE_NON_VALUE,result=THE_NON_VALUE;
    (void)pc; p->last_native_cost=0;
    if(!charge(p,1)) return THE_NON_VALUE;
    if(!lb_bitstring_view(args[0],&view) || (view.bit_size&7) || !view.bit_size) {
        p->freason=LB_EXC_BADARG; return THE_NON_VALUE;
    }
    size_t bytes=view.bit_size/8;
    if(bytes>LB_BEAM_MAX_BYTES) { p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE; }
    /* Snapshot before any collection. Unaligned byte-length binaries are
     * valid inputs; all temporary bytes belong to the same accounted domain. */
    if(!charge(p,lb_work_bulk_units(view.bit_size)+4*(uint64_t)domain->block_count+16)) goto done;
    if(lb_alloc_domain_allocate(domain,bytes,&input)!=LB_ALLOC_OK) goto failed;
    size_t end;
    if(!lb_size_add(view.bit_offset,view.bit_size,&end) ||
       !lb_bits_copy(view.data,end/8+(end%8!=0),view.bit_offset,input,bytes,0,view.bit_size)) {
        p->status=LB_PROCESS_INVALID; goto done;
    }
    if(lb_alloc_domain_allocate(domain,sizeof(*arena),&memory)!=LB_ALLOC_OK) goto failed;
    arena=memory; memset(arena,0,sizeof(*arena)); arena->domain=domain; arena->atoms=p->entry_module->space->atoms;
    LbBeamReader reader={input,bytes,0};
    if(!lb_pdecode_etf(arena,&reader,&decoded,LB_ETF_TEMPORARY)) goto failed;
    /* Resource identities and compressed ETF remain outside this initial data
     * profile. In particular, no unresolved export fun can escape the arena. */
    if(arena->external_fun_count) { lb_pfail(arena,LB_BEAM_UNSUPPORTED); goto failed; }
    if(!bind_atoms(p,arena) || !lb_pvalidate_maps(arena,p)) goto failed;
    size_t words,need,collections=p->collections;
    if(!lb_flat_size(domain,decoded,&words)) goto failed;
    if(words>LB_MAX_BINARY_BYTES/sizeof(Eterm) || !lb_size_add(words,p->heap_reserved,&need)) {
        p->status=LB_PROCESS_LIMIT; goto done;
    }
    if(!charge(p,words+lb_work_bulk_units(words))) goto done;
    if(!lb_heap_reserve(p,need,MAX_REG)) goto failed;
    if(p->collections!=collections) p->last_native_cost+=p->last_gc_cost;
    Eterm *top=p->htop; LbBinRef *offheap_before=p->off_heap.first;
    result=lb_copy_flat(decoded,&top,&p->off_heap);
    if(is_non_value(result)) {
        lb_offheap_rollback(&p->off_heap,offheap_before); p->status=LB_PROCESS_NO_MEMORY; goto done;
    }
    /* No fallible operation follows publication of new atom identities. */
    lb_program_commit(arena); p->htop=top; goto done;
failed:
    failure(p,arena?arena->error.status:LB_BEAM_NO_MEMORY);
done:
    if(arena) {
        /* Abort newer staging before reclaiming older decoding allocations. */
        lb_atoms_abort(arena->transaction); arena->transaction=NULL;
        lb_beam_program_destroy(arena);
    }
    if(input && lb_alloc_domain_release(domain,input)!=LB_ALLOC_OK) abort();
    if(budget) p->last_native_cost=(Uint)(before-budget->remaining);
    return result;
}

typedef struct {
    LbProcess *process;
    LbAllocDomain *domain;
    unsigned char *bytes;
    size_t length,capacity;
    LbAllocStatus failure;
    int refused,measure_only;
} LbEtfWriter;
static int writer_reserve(LbEtfWriter *writer,size_t extra)
{
    size_t required;
    if(!lb_size_add(writer->length,extra,&required) || required>LB_MAX_BINARY_BYTES) {
        writer->failure=LB_ALLOC_LIMIT; return 0;
    }
    if(writer->measure_only || required<=writer->capacity) return 1;
    size_t capacity=writer->capacity?writer->capacity:128;
    while(capacity<required) {
        if(capacity>LB_MAX_BINARY_BYTES/2) { capacity=LB_MAX_BINARY_BYTES; break; }
        capacity*=2;
    }
    void *memory=NULL;
    LbAllocStatus status=lb_alloc_domain_allocate(writer->domain,capacity,&memory);
    if(status!=LB_ALLOC_OK) { writer->failure=status; return 0; }
    if(writer->length) memcpy(memory,writer->bytes,writer->length);
    if(writer->bytes && lb_alloc_domain_release(writer->domain,writer->bytes)!=LB_ALLOC_OK) abort();
    writer->bytes=memory; writer->capacity=capacity; return 1;
}
static int writer_bytes(LbEtfWriter *writer,const void *bytes,size_t length)
{
    Uint work=(Uint)length+lb_work_bulk_units(length);
    if(!lb_work_domain_charge(writer->domain,work)) { writer->failure=LB_ALLOC_WORK_LIMIT; return 0; }
    writer->process->last_native_cost+=work;
    if(!writer_reserve(writer,length)) return 0;
    if(length && !writer->measure_only) memcpy(writer->bytes+writer->length,bytes,length);
    writer->length+=length; return 1;
}
static int writer_byte(LbEtfWriter *writer,unsigned value)
{
    unsigned char byte=(unsigned char)value; return writer_bytes(writer,&byte,1);
}
static int writer_u16(LbEtfWriter *writer,uint16_t value)
{
    unsigned char bytes[2]={(unsigned char)(value>>8),(unsigned char)value};
    return writer_bytes(writer,bytes,sizeof(bytes));
}
static int writer_u32(LbEtfWriter *writer,uint32_t value)
{
    unsigned char bytes[4]={(unsigned char)(value>>24),(unsigned char)(value>>16),
        (unsigned char)(value>>8),(unsigned char)value};
    return writer_bytes(writer,bytes,sizeof(bytes));
}
static int encode_term(LbEtfWriter *,Eterm,unsigned);
static int encode_atom(LbEtfWriter *writer,Eterm term)
{
    LbBeamBytes name;
    if(lb_atoms_name(writer->process->entry_module->space->atoms,term,&name)!=LB_ATOM_OK) {
        writer->process->status=LB_PROCESS_INVALID; return 0;
    }
    if(name.size<=255) return writer_byte(writer,119) && writer_byte(writer,(unsigned)name.size) &&
        writer_bytes(writer,name.data,name.size);
    if(name.size>UINT16_MAX) { writer->failure=LB_ALLOC_LIMIT; return 0; }
    return writer_byte(writer,118) && writer_u16(writer,(uint16_t)name.size) &&
        writer_bytes(writer,name.data,name.size);
}
static int encode_integer(LbEtfWriter *writer,Eterm term)
{
    if(is_small(term)) {
        Sint value=signed_val(term);
        if(value>=0 && value<=255) return writer_byte(writer,97) && writer_byte(writer,(unsigned)value);
        if(value>=INT32_MIN && value<=INT32_MAX)
            return writer_byte(writer,98) && writer_u32(writer,(uint32_t)(int32_t)value);
        uint64_t magnitude=value<0?(uint64_t)(-(value+1))+1:(uint64_t)value;
        unsigned char bytes[8]; size_t length=0;
        while(magnitude) { bytes[length++]=(unsigned char)magnitude; magnitude>>=8; }
        return writer_byte(writer,110) && writer_byte(writer,(unsigned)length) &&
            writer_byte(writer,value<0) && writer_bytes(writer,bytes,length);
    }
    Eterm *big=boxed_val(term); size_t digits=lb_big_size(term),length=digits*sizeof(Uint);
    int negative=((*big&_TAG_HEADER_MASK)==NEG_BIG_SUBTAG);
    while(length) {
        Uint digit=big[1+(length-1)/sizeof(Uint)];
        unsigned shift=(unsigned)(((length-1)%sizeof(Uint))*8);
        if(((digit>>shift)&255u)!=0) break;
        --length;
    }
    if(length>UINT32_MAX) { writer->failure=LB_ALLOC_LIMIT; return 0; }
    if(!writer_byte(writer,length<=255?110:111)) return 0;
    if(length<=255) { if(!writer_byte(writer,(unsigned)length)) return 0; }
    else if(!writer_u32(writer,(uint32_t)length)) return 0;
    if(!writer_byte(writer,(unsigned)negative)) return 0;
    for(size_t i=0;i<length;++i) {
        Uint digit=big[1+i/sizeof(Uint)];
        if(!writer_byte(writer,(unsigned)(digit>>(8*(i%sizeof(Uint)))&255u))) return 0;
    }
    return 1;
}
static int encode_bitstring(LbEtfWriter *writer,Eterm term)
{
    LbBitstringView view; size_t length,source_end,source_bytes;
    if(!lb_bitstring_view(term,&view)) return 0;
    length=(view.bit_size+7)/8;
    if(length>UINT32_MAX) { writer->failure=LB_ALLOC_LIMIT; return 0; }
    if(view.bit_size&7) {
        if(!writer_byte(writer,77) || !writer_u32(writer,(uint32_t)length) ||
           !writer_byte(writer,(unsigned)(view.bit_size&7))) return 0;
    } else if(!writer_byte(writer,109) || !writer_u32(writer,(uint32_t)length)) return 0;
    Uint work=(Uint)length+lb_work_bulk_units(length);
    if(!lb_work_domain_charge(writer->domain,work)) { writer->failure=LB_ALLOC_WORK_LIMIT; return 0; }
    writer->process->last_native_cost+=work;
    if(!writer_reserve(writer,length)) return 0;
    if(length) {
        if(!lb_size_add(view.bit_offset,view.bit_size,&source_end) ||
           !lb_size_add(source_end,7,&source_end)) { writer->failure=LB_ALLOC_LIMIT; return 0; }
        source_bytes=source_end/8;
        if(!lb_bits_copy(view.data,source_bytes,view.bit_offset,writer->bytes+writer->length,
                         length,0,view.bit_size)) { writer->failure=LB_ALLOC_INVALID; return 0; }
        if(view.bit_size&7) writer->bytes[writer->length+length-1]&=(unsigned char)(0xffu<<(8-(view.bit_size&7)));
    }
    writer->length+=length; return 1;
}
static int encode_list(LbEtfWriter *writer,Eterm term,unsigned depth)
{
    Eterm cursor=term; size_t count=0;
    while(is_list(cursor)) {
        if(!charge(writer->process,1)) { writer->failure=LB_ALLOC_WORK_LIMIT; return 0; }
        if(count==UINT32_MAX) { writer->failure=LB_ALLOC_LIMIT; return 0; }
        ++count; cursor=CDR(list_val(cursor));
    }
    if(!writer_byte(writer,108) || !writer_u32(writer,(uint32_t)count)) return 0;
    cursor=term;
    for(size_t i=0;i<count;++i) {
        Eterm *cell=list_val(cursor);
        if(!encode_term(writer,CAR(cell),depth+1)) return 0;
        cursor=CDR(cell);
    }
    return encode_term(writer,cursor,depth+1);
}
static int encode_term(LbEtfWriter *writer,Eterm term,unsigned depth)
{
    if(depth>1024) { writer->failure=LB_ALLOC_LIMIT; return 0; }
    if(is_small(term) || lb_is_big(term)) return encode_integer(writer,term);
    if(is_atom(term)) return encode_atom(writer,term);
    if(is_nil(term)) return writer_byte(writer,106);
    if(is_list(term)) return encode_list(writer,term,depth);
    if(is_tuple(term)) {
        Eterm *tuple=tuple_val(term); Uint arity=arityval(tuple[0]);
        if(arity<=255) { if(!writer_byte(writer,104) || !writer_byte(writer,(unsigned)arity)) return 0; }
        else if(!writer_byte(writer,105) || !writer_u32(writer,arity)) return 0;
        for(Uint i=0;i<arity;++i) if(!encode_term(writer,tuple[i+1],depth+1)) return 0;
        return 1;
    }
    if(lb_is_flatmap(term)) {
        Eterm *map=boxed_val(term); size_t size=(size_t)map[1];
        Eterm *keys=tuple_val(map[2]);
        if(!writer_byte(writer,116) || !writer_u32(writer,(uint32_t)size)) return 0;
        for(size_t i=0;i<size;++i)
            if(!encode_term(writer,keys[i+1],depth+1) || !encode_term(writer,map[LB_FLATMAP_WORDS+i],depth+1)) return 0;
        return 1;
    }
    if(is_boxed(term) && ((*boxed_val(term)&_TAG_HEADER_MASK)==_TAG_HEADER_FLOAT)) {
        Uint bits=boxed_val(term)[1]; unsigned char bytes[8];
        for(unsigned i=0;i<8;++i) bytes[i]=(unsigned char)(bits>>(56-8*i));
        return writer_byte(writer,70) && writer_bytes(writer,bytes,sizeof(bytes));
    }
    LbBitstringView view;
    if(lb_bitstring_view(term,&view)) return encode_bitstring(writer,term);
    /* Ports, references, functions, and HAMT traversal require distinct ETF
     * ownership contracts. Refuse them rather than emitting misleading terms. */
    writer->refused=1; return 0;
}
static int encode_options(LbProcess *p,Eterm options)
{
    LbAllocDomain *domain=p->entry_module->space->domain;
    while(is_list(options)) {
        if(!lb_work_domain_charge(domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return 0; }
        ++p->last_native_cost; Eterm option=CAR(list_val(options)); options=CDR(list_val(options));
        if(is_atom(option) && option==am_compressed) { p->status=LB_PROCESS_REFUSED; return 0; }
        if(!is_tuple(option) || arityval(tuple_val(option)[0])!=2) { p->freason=LB_EXC_BADARG; return 0; }
        Eterm *pair=tuple_val(option);
        if(pair[1]==am_minor_version) {
            if(!is_small(pair[2]) || signed_val(pair[2])<0 || signed_val(pair[2])>2) {
                p->freason=LB_EXC_BADARG; return 0;
            }
        } else if(pair[1]==am_compressed) p->status=LB_PROCESS_REFUSED;
        else { p->freason=LB_EXC_BADARG; return 0; }
        if(p->status==LB_PROCESS_REFUSED) return 0;
    }
    if(!is_nil(options)) { p->freason=LB_EXC_BADARG; return 0; }
    return 1;
}
static void writer_failure(LbEtfWriter *writer)
{
    if(writer->refused) { writer->process->status=LB_PROCESS_REFUSED; return; }
    if(writer->failure==LB_ALLOC_WORK_LIMIT) writer->process->status=LB_PROCESS_WORK_LIMIT;
    else if(writer->failure==LB_ALLOC_LIMIT) writer->process->status=LB_PROCESS_LIMIT;
    else if(writer->failure==LB_ALLOC_INVALID) writer->process->status=LB_PROCESS_INVALID;
    else writer->process->status=LB_PROCESS_NO_MEMORY;
}
static Eterm term_to_binary(LbProcess *p,Eterm *args,int arity)
{
    LbEtfWriter writer={.process=p,.domain=p->entry_module->space->domain,
        .failure=LB_ALLOC_NO_MEMORY};
    Eterm result=THE_NON_VALUE; size_t bits,words,need,collections=p->collections;
    p->last_native_cost=0;
    if(!charge(p,1)) return THE_NON_VALUE;
    if(arity==2 && !encode_options(p,args[1])) return THE_NON_VALUE;
    if(!writer_byte(&writer,131) || !encode_term(&writer,args[0],0)) goto failed;
    if(!lb_size_mul(writer.length,8,&bits) || !(words=lb_bitstring_heap_words(bits)) ||
       !lb_size_add(words,p->heap_reserved,&need)) { p->status=LB_PROCESS_LIMIT; goto done; }
    if(!lb_heap_reserve(p,need,MAX_REG)) {
        LbAllocStatus cause=lb_alloc_domain_failure(writer.domain);
        p->status=cause==LB_ALLOC_WORK_LIMIT?LB_PROCESS_WORK_LIMIT:
            cause==LB_ALLOC_LIMIT?LB_PROCESS_LIMIT:LB_PROCESS_NO_MEMORY;
        goto done;
    }
    if(p->collections!=collections) p->last_native_cost+=p->last_gc_cost;
    LbAllocStatus status=lb_bitstring_build(writer.domain,&p->off_heap,&p->htop,
                                            writer.bytes,bits,0,&result);
    p->last_native_cost+=1+lb_work_bulk_units(writer.length);
    if(status!=LB_ALLOC_OK) {
        if(status==LB_ALLOC_WORK_LIMIT) p->status=LB_PROCESS_WORK_LIMIT;
        else if(status==LB_ALLOC_LIMIT) p->status=LB_PROCESS_LIMIT;
        else p->status=LB_PROCESS_NO_MEMORY;
    }
    goto done;
failed:
    writer_failure(&writer);
done:
    if(writer.bytes && lb_alloc_domain_release(writer.domain,writer.bytes)!=LB_ALLOC_OK) abort();
    return result;
}
Eterm lb_bif_external_size_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)pc; LbEtfWriter writer={.process=p,.domain=p->entry_module->space->domain,
        .failure=LB_ALLOC_NO_MEMORY,.measure_only=1};
    p->last_native_cost=0;
    if(!charge(p,1)) return THE_NON_VALUE;
    if(!writer_byte(&writer,131) || !encode_term(&writer,args[0],0)) {
        writer_failure(&writer); return THE_NON_VALUE;
    }
    if(writer.length>(size_t)MAX_SMALL) { p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE; }
    return make_small((Sint)writer.length);
}
Eterm lb_bif_term_to_binary_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{ (void)pc; return term_to_binary(p,args,1); }
Eterm lb_bif_term_to_binary_2(LbProcess *p,Eterm *args,const BeamInstr *pc)
{ (void)pc; return term_to_binary(p,args,2); }
