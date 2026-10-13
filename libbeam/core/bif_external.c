/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1996-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Owned, transactional binary_to_term data profile. Parsing uses the same
 * native-derived ETF arena as module preparation, never another serving VM.
 */
#include "process_internal.h"
#include "beam_program_internal.h"
#include "alloc_internal.h"
#include "work_budget_internal.h"
#include "bit_copy.h"

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
