/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1996-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Owned process/host boundary around generated BEAM interpreter instructions.
 * Based on emu/beam_emu.c process_main's NO_JUMP_TABLE dispatch, register swaps,
 * continuation return, and native BIF call/epilogue. No scheduler/TLS singleton.
 */
#include "process_internal.h"
#include "term_compare.h"
#include "writable.h"
#include "binary_build.h"
#include "reference.h"
#define is_not_ref(term) (!lb_is_reference(term))
#define is_any_fun(term) (lb_fun(term)!=NULL)
#define is_function2(term,arity) lb_fun_is_arity(term,arity)
#define erl_is_function(process,term,arity) lb_fun_test_arity(process,term,arity)
typedef LbBifFn ErtsBifFunc;
typedef LbBifFn BifFunction;
typedef LbFunEntry ErlFunEntry;
typedef LbFun ErlFunThing;
#define ERL_FUN_SIZE LB_FUN_WORDS
#define MAKE_FUN_HEADER(arity,free,external) (LB_FUN_HEADER(arity,free)|((Uint)!!(external)<<LB_FUN_KIND_SHIFT))
#define fun_arity(fun) lb_fun_arity(fun)
#define make_fun(fun) make_boxed((Eterm *)(fun))
const BeamInstr lb_host_return[1]={op_normal_exit};
int lb_instruction_supported(unsigned op)
{
    switch(op) {
#include "lb_dispatch_support.inc"
    default:return 0;
    }
}
static LbProcessStatus create_owned(LbCodeModule *module,const BeamInstr *pc,const LbMFA *mfa,
                                   size_t words,LbProcess **out)
{
    LbProcess *p; LbAllocDomain *domain=module->space->domain; void *memory; size_t i;
    if(module->space->loading || module->users==SIZE_MAX) return LB_PROCESS_INVALID;
    if(words<S_RESERVED+1) words=S_RESERVED+1;
    if(lb_alloc_domain_allocate(domain,sizeof(*p),&memory)!=LB_ALLOC_OK) return LB_PROCESS_NO_MEMORY;
    p=memory; memset(p,0,sizeof(*p));
    if(lb_alloc_domain_allocate(domain,words*sizeof(Eterm),&memory)!=LB_ALLOC_OK) { lb_release(domain,p); return LB_PROCESS_NO_MEMORY; }
    lb_message_queue_init(&p->messages);
    p->entry_module=module; p->heap=p->htop=memory; p->hend=p->heap+words; p->stop=p->hend-1;
    *p->stop=(Eterm)lb_host_return; p->bin_vheap_sz=LB_BINARY_VHEAP_MIN_WORDS;
    for(i=0;i<MAX_REG;++i) p->reg[i]=NIL;
    p->fvalue=p->exception_value=p->construction_root=NIL; p->freason=LB_EXC_NULL;
    p->local_mfa=*mfa; p->arity=mfa->arity; p->current=&p->local_mfa;
    p->i=pc; p->status=LB_PROCESS_READY;
    ++module->users; *out=p; return LB_PROCESS_READY;
}
static int private_mfa(const LbCodeModule *module,const BeamInstr *pc,LbMFA *mfa)
{
    for(size_t i=0;i<lb_code_module_function_count(module);++i) {
        const BeamInstr *info=(const BeamInstr *)module->words[i];
        if(info+sizeof(LbCodeInfo)/sizeof(BeamInstr)!=pc) continue;
        memcpy(mfa,info+offsetof(LbCodeInfo,mfa)/sizeof(BeamInstr),sizeof(*mfa)); return 1;
    }
    return 0;
}
/* erl_create_process's owned heap/register/code/PID preparation for a local
 * closure entry. The executor must prepare its queue node before this call
 * and publish runnable membership afterwards; no helper VM or export stub. */
LbProcessStatus lb_process_create_fun(LbProcess *parent,Eterm value,size_t words,LbProcess **out)
{
    if(!out) return LB_PROCESS_INVALID; *out=NULL;
    if(!parent) return LB_PROCESS_INVALID;
    parent->last_native_cost=1;
    const LbFun *fun=lb_fun(value);
    if(!fun || lb_fun_arity(fun)!=0) return LB_PROCESS_INVALID;
    const LbFunEntry *entry=fun->entry.fun; LbCodeModule *module=entry->owner;
    if(!module || module->space!=parent->entry_module->space || !module->published ||
       entry->pend_purge_address) return LB_PROCESS_REFUSED;
    size_t free=lb_fun_free(fun),needed=0; LbMFA mfa;
    if(!private_mfa(module,entry->dispatch.addresses[0],&mfa) || mfa.arity!=free) return LB_PROCESS_INVALID;
    for(size_t i=0;i<free;++i) {
        size_t size;
        if(!lb_flat_size(module->space->domain,fun->env[i],&size)) return LB_PROCESS_NO_MEMORY;
        if(!lb_size_add(needed,size,&needed)) return LB_PROCESS_LIMIT;
        parent->last_native_cost+=1+size/16;
    }
    if(!lb_size_add(needed,S_RESERVED+1,&needed) || needed>64u*1024u*1024u/sizeof(Eterm) ||
       words>64u*1024u*1024u/sizeof(Eterm)) return LB_PROCESS_LIMIT;
    if(words<needed) words=needed;
    LbProcess *child; LbProcessStatus status=create_owned(module,entry->dispatch.addresses[0],&mfa,words,&child);
    if(status!=LB_PROCESS_READY) return status;
    for(size_t i=0;i<free;++i) {
        LbAllocStatus held=lb_fun_retain_term(child,fun->env[i],&parent->last_native_cost);
        if(held!=LB_ALLOC_OK) {
            status=held==LB_ALLOC_NO_MEMORY?LB_PROCESS_NO_MEMORY:LB_PROCESS_REFUSED; goto failure;
        }
    }
    for(size_t i=0;i<free;++i) {
        child->reg[i]=lb_copy_flat(fun->env[i],&child->htop,&child->off_heap);
        if(is_non_value(child->reg[i])) { status=LB_PROCESS_NO_MEMORY; goto failure; }
    }
    /* Identity is the final fallible acquisition. There is no runnable task
     * or caller-visible PID until the fully prepared child is returned. */
    Eterm pid; status=lb_process_identity(child,&pid);
    if(status!=LB_PROCESS_READY) goto failure;
    *out=child; return LB_PROCESS_READY;
failure:
    lb_process_destroy(child); return status;
}
LbProcessStatus lb_process_create(const LbCodeEntry *entry,const Eterm *args,size_t arity,size_t words,LbProcess **out)
{
    if(!out) return LB_PROCESS_INVALID; *out=NULL;
    if(!entry || (arity && !args) || arity!=entry->entry->info.mfa.arity || words>64u*1024u*1024u/sizeof(Eterm)) return LB_PROCESS_INVALID;
    LbCodeModule *module=entry->module;
    for(size_t i=0;i<arity;++i) {
        if(!is_small(args[i]) && !is_nil(args[i]) && !is_atom(args[i])) return LB_PROCESS_INVALID;
        if(is_atom(args[i]) && atom_val(args[i])>=lb_atoms_count(module->space->atoms)) return LB_PROCESS_INVALID;
    }
    LbProcessStatus status=create_owned(module,entry->entry->dispatch.addresses[0],&entry->entry->info.mfa,words,out);
    if(status==LB_PROCESS_READY) for(size_t i=0;i<arity;++i) (*out)->reg[i]=args[i];
    return status;
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
    if(p->identity.owner && lb_pid_remove(p->identity.owner,&p->identity)!=LB_PID_OK) abort();
    lb_message_queue_clear(&p->messages);
    lb_fun_leases_clear(p);
    lb_decimal_destroy(p->decimal);
    lb_offheap_clear(&p->off_heap);
    lb_offheap_clear(&p->writable);
    lb_release(d,p->heap); lb_release(d,p);
    if(!module->users) abort(); --module->users;
}
LbProcessStatus lb_process_collect(LbProcess *p,size_t need)
{
    if(!p || p->running) return LB_PROCESS_INVALID;
    return lb_process_collect_live(p,need,p->status==LB_PROCESS_DONE?1:MAX_REG)?p->status:LB_PROCESS_NO_MEMORY;
}
Eterm lb_process_result(const LbProcess *p) { return p && p->status==LB_PROCESS_DONE?p->reg[0]:THE_NON_VALUE; }
Eterm lb_process_exception(const LbProcess *p) { return p && p->status==LB_PROCESS_EXCEPTION?lb_exception_atom(p->freason):THE_NON_VALUE; }
Eterm lb_process_exception_value(const LbProcess *p) { return p && p->status==LB_PROCESS_EXCEPTION?p->exception_value:THE_NON_VALUE; }
Eterm lb_process_exception_info(const LbProcess *p) { return p && p->status==LB_PROCESS_EXCEPTION && (p->freason&LB_EXF_HAS_EXT_INFO)?p->fvalue:THE_NON_VALUE; }
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
    LbCodeModule *module; Export *e;
    for(e=space->exports;e;e=e->next)
        if(e->dispatch.addresses[0]==pc) return &e->info.mfa;
    /* Local calls can yield at non-exported entries. Use the emitted native
     * function directory, not an export stub or a fabricated scheduling MFA.
     * memcpy avoids aliasing instruction-word storage as an LbMFA object. */
    for(module=space->modules;module;module=module->next)
        if(private_mfa(module,pc,&p->local_mfa)) return &p->local_mfa;
    return NULL;
}
static int32_t lb_packed_label(const BeamInstr *table,size_t index)
{
    int32_t offset; memcpy(&offset,(const unsigned char *)table+index*sizeof(offset),sizeof(offset)); return offset;
}
#define OpCase(name) case op_##name
#define BeamCodeAddr(word) (word)
#define ERTS_UNLIKELY(test) (test)
#define ERTS_LIKELY(test) (test)
#define BEAM_IS_TUPLE(term) is_boxed(term)
#define is_not_list(term) (!is_list(term))
#define is_not_tuple(term) (!is_tuple(term))
#define x(i) reg[(i)]
#define xb(offset) reg[(offset)/sizeof(Eterm)]
#define yb(offset) E[(offset)/sizeof(Eterm)]
#define Qb(word) (word)
#define Sb(offset) (((offset)&1)?yb((offset)-1):xb(offset))
#define is_not_nil(term) (!is_nil(term))
#define y(i) E[(i)]
#define REG_TARGET_PTR(target) (((target)&1)?&yb((target)-1):&xb(target))
#define GetSource(raw,dst) do { \
    (dst)=(raw); \
    switch(loader_tag(dst)) { \
    case LOADER_X_REG:(dst)=x(loader_x_reg_index(dst));break; \
    case LOADER_Y_REG:(dst)=y(loader_y_reg_index(dst));break; \
    } \
    CHECK_TERM(dst); \
} while(0)
#define Ib(word) (word)
#define tb(word) (word)
#define fb(word) ((Sint)(int32_t)(word))
#define jb(word) fb(word)
#define db(word) (word)
#define is_both_small(a,b) (is_small(a) && is_small(b))
#define is_value(value) (!is_non_value(value))
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
#define BADARG LB_EXC_BADARG
#define SYSTEM_LIMIT LB_EXC_SYSTEM_LIMIT
#define BSF_LITTLE 2
#define BADMATCH LB_EXC_BADMATCH
#define EXC_CASE_CLAUSE LB_EXC_CASE_CLAUSE
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
    if(!pc_valid(c_p,I) || !VALID_INSTR(next_op) || (c_p->decimal && next_op!=op_call_bif_W)) goto malformed_code;
    if(!budget--) goto context_switch3;
    {
        Uint cost; LbAllocStatus held=lb_fun_prepare_instruction(c_p,I,&cost);
        FCALLS-=(Sint)cost;
        if(held==LB_ALLOC_NO_MEMORY) goto memory_failure;
        if(held!=LB_ALLOC_OK) goto malformed_code;
    }
    switch(next_op) {
#include "lb_dispatch_generated.inc"
    OpCase(call_bif_W): {
        /* Owned adaptation of generated call_bif_W and nif_bif__epilogue. The
         * closed positive catalog has heavy allocating BIFs. Only decimal
         * formatting owns a resumable native continuation in this profile. */
        const LbMFA *mfa=entry_mfa(c_p,I);
        LbBifFn function; Eterm value;
        if(!mfa) goto malformed_code;
        if(FCALLS<=1) goto context_switch;
        c_p->current=mfa; c_p->arity=mfa->arity; c_p->heap_reserved=0;
        SWAPOUT; memcpy(&function,&I[1],sizeof(function));
        if(c_p->decimal && function!=lb_bif_integer_to_binary_1) goto malformed_code;
        c_p->last_native_cost=0;
        value=function(c_p,reg,I); SWAPIN; FCALLS-=1+(Sint)c_p->last_native_cost;
        if(c_p->decimal) {
            /* Reenter this actual native trampoline on the next worker slice.
             * Heap collection cannot invalidate the independent continuation. */
            if(!is_non_value(value) || c_p->status==LB_PROCESS_NO_MEMORY || c_p->status==LB_PROCESS_EXCEPTION) goto malformed_code;
            goto context_switch;
        }
        if(is_non_value(value)) {
            if(c_p->status==LB_PROCESS_NO_MEMORY) goto memory_failure;
            if(c_p->status==LB_PROCESS_LIMIT) goto resource_limit;
            if(c_p->status==LB_PROCESS_REFUSED) goto execution_refused;
            if(c_p->status==LB_PROCESS_ENVIRONMENT) goto environment_failure;
            goto find_func_info;
        }
        x(0)=value; SET_I(cp_val(*E)); *E=NIL; Goto(*I);
    }
    OpCase(normal_exit):
        c_p->status=LB_PROCESS_DONE; c_p->arity=1; goto stop;
    OpCase(i_func_info_IaaI):
        c_p->freason=LB_EXC_FUNCTION_CLAUSE; goto find_func_info;
    default:goto malformed_code;
    }
context_switch:
    c_p->current=entry_mfa(c_p,I);
    if(!c_p->current) goto malformed_code;
    c_p->arity=c_p->current->arity;
context_switch3:
    c_p->status=LB_PROCESS_YIELDED; goto stop;
find_func_info: {
    /* Materialize the native error value, not handle_error/stacktrace/catch.
     * Keep the original code/flags and extended payload for future handling.
     * No producer of other exception classes or arglist wrappers is admitted. */
    LbAllocStatus expanded;
    if(LB_GET_EXC_CLASS(c_p->freason)!=LB_EXTAG_ERROR ||
       (c_p->freason&(LB_EXF_ARGLIST|LB_EXF_RESTORE_NFUNC|LB_EXF_PANIC|LB_EXF_THROWN|LB_EXF_NATIVE))) goto malformed_code;
    SWAPOUT; expanded=lb_exception_expand(c_p,c_p->freason,c_p->fvalue,&c_p->exception_value); SWAPIN;
    if(expanded==LB_ALLOC_NO_MEMORY) goto memory_failure;
    if(expanded!=LB_ALLOC_OK) goto malformed_code;
    if(!(c_p->freason&LB_EXF_HAS_EXT_INFO)) c_p->fvalue=NIL;
    c_p->status=LB_PROCESS_EXCEPTION; goto stop;
}
execution_refused:
    c_p->status=LB_PROCESS_REFUSED; goto stop;
environment_failure:
    c_p->status=LB_PROCESS_ENVIRONMENT; goto stop;
resource_limit:
    c_p->status=LB_PROCESS_LIMIT; goto stop;
memory_failure:
    /* The failed collection left the old roots intact. */
    c_p->status=LB_PROCESS_NO_MEMORY; goto stop;
malformed_code:
    c_p->status=LB_PROCESS_INVALID;
stop:
    /* Guest exit removes identity before publishing completion. The retained
     * process/result allocation may outlive that logical death. */
    if(c_p->status!=LB_PROCESS_YIELDED && c_p->identity.owner &&
       lb_pid_remove(c_p->identity.owner,&c_p->identity)!=LB_PID_OK) abort();
    SWAPOUT; c_p->running=0; return c_p->status;
}
