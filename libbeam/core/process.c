/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1996-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Owned process/host boundary around generated BEAM interpreter instructions.
 * Based on emu/beam_emu.c process_main's NO_JUMP_TABLE dispatch, register swaps,
 * continuation return, and native BIF call/epilogue. No scheduler/TLS singleton.
 */
#include "process_internal.h"
const BeamInstr lb_host_return[1]={op_normal_exit};
int lb_instruction_supported(unsigned op)
{
    switch(op) {
#include "lb_dispatch_support.inc"
    default:return 0;
    }
}
LbProcessStatus lb_process_create(const LbCodeEntry *entry,const Eterm *args,size_t arity,size_t words,LbProcess **out)
{
    LbProcess *p; LbCodeModule *module; LbAllocDomain *domain; void *memory; size_t i;
    if(!out) return LB_PROCESS_INVALID; *out=NULL;
    if(!entry || (arity && !args) || arity!=entry->entry->info.mfa.arity || words>64u*1024u*1024u/sizeof(Eterm)) return LB_PROCESS_INVALID;
    module=entry->module; domain=module->space->domain;
    if(module->space->loading || module->users==SIZE_MAX) return LB_PROCESS_INVALID;
    for(i=0;i<arity;++i) {
        if(!is_small(args[i]) && !is_nil(args[i]) && !is_atom(args[i])) return LB_PROCESS_INVALID;
        if(is_atom(args[i]) && atom_val(args[i])>=lb_atoms_count(module->space->atoms)) return LB_PROCESS_INVALID;
    }
    if(words<S_RESERVED+1) words=S_RESERVED+1;
    if(lb_alloc_domain_allocate(domain,sizeof(*p),&memory)!=LB_ALLOC_OK) return LB_PROCESS_NO_MEMORY;
    p=memory; memset(p,0,sizeof(*p));
    if(lb_alloc_domain_allocate(domain,words*sizeof(Eterm),&memory)!=LB_ALLOC_OK) { lb_release(domain,p); return LB_PROCESS_NO_MEMORY; }
    p->entry_module=module; p->heap=p->htop=memory; p->hend=p->heap+words; p->stop=p->hend-1;
    *p->stop=(Eterm)lb_host_return;
    for(i=0;i<MAX_REG;++i) p->reg[i]=i<arity?args[i]:NIL;
    p->fvalue=p->freason=NIL; p->arity=arity; p->current=&entry->entry->info.mfa;
    p->i=entry->entry->dispatch.addresses[0]; p->status=LB_PROCESS_READY;
    ++module->users; *out=p; return LB_PROCESS_READY;
}
LbProcessStatus lb_process_create_binary(const LbCodeEntry *entry,const void *bytes,size_t size,
                                        size_t words,LbProcess **out)
{
    LbProcess *p;
    LbProcessStatus status;
    Eterm argument=NIL;
    if(!out) return LB_PROCESS_INVALID;
    *out=NULL;
    if(size>LB_MAX_BINARY_BYTES || (size && !bytes)) return LB_PROCESS_INVALID;
    status=lb_process_create(entry,&argument,1,words,&p);
    if(status!=LB_PROCESS_READY) return status;
    if(!lb_heap_reserve(p,lb_bitstring_heap_words(size*8),1) ||
       lb_bitstring_build(p->entry_module->space->domain,&p->off_heap,&p->htop,
                          bytes,size*8,0,&p->reg[0])!=LB_ALLOC_OK) {
        lb_process_destroy(p); return LB_PROCESS_NO_MEMORY;
    }
    *out=p; return LB_PROCESS_READY;
}
void lb_process_destroy(LbProcess *p)
{
    LbCodeModule *module; LbAllocDomain *d;
    if(!p) return;
    if(p->running) abort(); /* internal serialized API misuse, not a guest path */
    module=p->entry_module; d=module->space->domain;
    lb_offheap_clear(&p->off_heap);
    lb_release(d,p->heap); lb_release(d,p);
    if(!module->users) abort(); --module->users;
}
LbProcessStatus lb_process_collect(LbProcess *p,size_t need)
{
    if(!p || p->running) return LB_PROCESS_INVALID;
    return lb_process_collect_live(p,need,p->status==LB_PROCESS_DONE?1:MAX_REG)?p->status:LB_PROCESS_NO_MEMORY;
}
Eterm lb_process_result(const LbProcess *p) { return p && p->status==LB_PROCESS_DONE?p->reg[0]:THE_NON_VALUE; }
Eterm lb_process_exception(const LbProcess *p) { return p && p->status==LB_PROCESS_EXCEPTION?p->freason:THE_NON_VALUE; }
size_t lb_process_collections(const LbProcess *p) { return p?p->collections:0; }
static int pc_valid(LbProcess *p,const BeamInstr *pc)
{
    LbCodeModule *m; Uint address=(Uint)pc; size_t i;
    if(pc==lb_host_return) return 1;
    for(i=0;i<LB_NATIVE_COUNT;++i) if(pc==p->entry_module->space->natives[i].dispatch.addresses[0]) return 1;
    if(address%sizeof(BeamInstr)) return 0;
    for(m=p->entry_module->space->modules;m;m=m->next)
        if(address>=(Uint)m->words && address<(Uint)(m->words+m->word_count)) return 1;
    return 0;
}
static const LbMFA *entry_mfa(LbProcess *p,const BeamInstr *pc)
{
    LbCodeSpace *space=p->entry_module->space;
    LbCodeModule *module; Export *e; size_t i;
    for(e=space->exports;e;e=e->next)
        if(e->dispatch.addresses[0]==pc) return &e->info.mfa;
    /* Local calls can yield at non-exported entries. Use the emitted native
     * function directory, not an export stub or a fabricated scheduling MFA.
     * memcpy avoids aliasing instruction-word storage as an LbMFA object. */
    for(module=space->modules;module;module=module->next)
        for(i=0;i<lb_code_module_function_count(module);++i) {
            const BeamInstr *info=(const BeamInstr *)module->words[i];
            if(info+sizeof(LbCodeInfo)/sizeof(BeamInstr)==pc) {
                memcpy(&p->local_mfa,info+offsetof(LbCodeInfo,mfa)/sizeof(BeamInstr),sizeof(p->local_mfa));
                return &p->local_mfa;
            }
        }
    return NULL;
}
#define OpCase(name) case op_##name
#define BeamCodeAddr(word) (word)
#define ERTS_UNLIKELY(test) (test)
#define x(i) reg[(i)]
#define xb(offset) reg[(offset)/sizeof(Eterm)]
#define yb(offset) E[(offset)/sizeof(Eterm)]
#define Qb(word) (word)
#define y(i) E[(i)]
#define Ib(word) (word)
#define tb(word) (word)
#define fb(word) ((Sint)(int32_t)(word))
#define cp_val(word) ((const BeamInstr *)(word))
#define make_blank(slot) ((slot)=NIL)
#define ADD_BYTE_OFFSET(pointer,offset) ((Eterm *)((unsigned char *)(pointer)+(offset)))
#define SET_I(pc) do { I=(pc); } while(0)
#define VALID_INSTR(op) ((op)<NUM_SPECIFIC_OPS)
#define CHECK_TERM(term) ASSERT(!is_non_value(term))
#define CHECK_ARGS(pc) ASSERT(pc_valid(c_p,(pc)))
#define HEAP_SPACE_VERIFIED(n) do { if((Uint)(E-HTOP)<(Uint)(n)+S_RESERVED) goto malformed_code; c_p->heap_reserved=(n); } while(0)
#define SWAPOUT do { c_p->htop=HTOP; c_p->stop=E; c_p->i=I; } while(0)
#define SWAPIN do { HTOP=c_p->htop; E=c_p->stop; } while(0)
#define Goto(op) do { next_op=(op); goto dispatch; } while(0)
#define GotoPF(op) Goto(op)
#define BADMATCH am_badmatch
#define EXC_CASE_CLAUSE am_case_clause
LbProcessStatus lb_process_run(LbProcess *c_p,size_t budget,unsigned reductions)
{
    Eterm *reg,*E,*HTOP;
    const BeamInstr *I;
    BeamInstr next_op;
    Sint FCALLS,neg_o_reds=0;
    if(!c_p || c_p->running || c_p->entry_module->space->loading) return LB_PROCESS_INVALID;
    if(c_p->status!=LB_PROCESS_READY && c_p->status!=LB_PROCESS_YIELDED) return c_p->status;
    if(reductions<4) return LB_PROCESS_INVALID;
    c_p->running=1; reg=c_p->reg; I=c_p->i; E=c_p->stop; HTOP=c_p->htop; FCALLS=reductions;
    next_op=*I;
dispatch:
    if(!pc_valid(c_p,I) || !VALID_INSTR(next_op)) goto malformed_code;
    if(!budget--) goto context_switch3;
    switch(next_op) {
#include "lb_dispatch_generated.inc"
    OpCase(call_bif_W): {
        /* Owned adaptation of generated call_bif_W and nif_bif__epilogue. The
         * closed positive catalog has heavy allocating, nontrapping BIFs only. */
        const LbMFA *mfa=entry_mfa(c_p,I);
        LbBifFn function; Eterm value;
        if(!mfa) goto malformed_code;
        if(FCALLS<=1) goto context_switch;
        c_p->current=mfa; c_p->arity=mfa->arity; c_p->heap_reserved=0;
        SWAPOUT; memcpy(&function,&I[1],sizeof(function));
        value=function(c_p,reg,I); SWAPIN; --FCALLS;
        if(is_non_value(value)) {
            if(c_p->status==LB_PROCESS_NO_MEMORY) goto memory_failure;
            goto find_func_info;
        }
        x(0)=value; SET_I(cp_val(*E)); *E=NIL; Goto(*I);
    }
    OpCase(normal_exit):
        c_p->status=LB_PROCESS_DONE; c_p->arity=1; goto stop;
    OpCase(i_func_info_IaaI):
        c_p->freason=am_function_clause; goto find_func_info;
    default:goto malformed_code;
    }
context_switch:
    c_p->current=entry_mfa(c_p,I);
    if(!c_p->current) goto malformed_code;
    c_p->arity=c_p->current->arity;
context_switch3:
    c_p->status=LB_PROCESS_YIELDED; goto stop;
find_func_info:
    c_p->status=LB_PROCESS_EXCEPTION; goto stop;
memory_failure:
    /* The failed collection left the old roots intact. */
    c_p->status=LB_PROCESS_NO_MEMORY; goto stop;
malformed_code:
    c_p->status=LB_PROCESS_INVALID;
stop:
    SWAPOUT; c_p->running=0; return c_p->status;
}
