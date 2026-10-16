/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1996-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Owned process/host boundary around generated BEAM interpreter instructions.
 * Based on emu/beam_emu.c process_main's NO_JUMP_TABLE dispatch, register swaps,
 * continuation return, and native BIF call/epilogue. No scheduler/TLS singleton.
 */
#include "process_internal.h"
#include "exit_signal.h"
#include "apply.h"
#include "binary_match.h"
#define ErlSubBits LbSubBits
#define HEADER_SUB_BITS LB_HEADER_SUB_BITS
#define ERL_SUB_BITS_SIZE LB_SUB_BITS_WORDS
#define bitstring_val boxed_val
#define make_bitstring(sub) make_boxed((Eterm *)(sub))
#define HEAP_TOP(p) ((p)->htop)
typedef unsigned char byte;
#define BYTE_OFFSET(bits) ((bits)/8)
#define erl_sub_bits_get_base(sub) ((const byte *)((sub)->base_flags&~LB_SUB_WRITABLE))
#define ERTS_FALLTHROUGH() __attribute__((fallthrough))
#define erl_sub_bits_is_match_context lb_is_match_context
#define is_bitstring_header(h) (((h)&_TAG_HEADER_MASK)==HEAP_BITS_SUBTAG || (h)==LB_HEADER_SUB_BITS)
static int tuple_field_range(Eterm tuple,Uint byte_offset,Uint count)
{
    Uint first,arity;
    if(!is_tuple(tuple) || !count || byte_offset<sizeof(Eterm) || byte_offset%sizeof(Eterm)) return 0;
    first=byte_offset/sizeof(Eterm); arity=arityval(*tuple_val(tuple));
    return first<=arity && count-1<=arity-first;
}
#include "term_compare.h"
#include "writable.h"
#include "binary_build.h"
#include "map_runtime.h"
#include "big.h"
#include "flatmap.h"
#include "reference.h"
#include "stacktrace.h"
#include "raise_trace.h"
#include "alias.h"
#include "receive.h"
#include <math.h>
#include <time.h>
#include <fenv.h>
#define is_not_ref(term) (!lb_is_reference(term))
#define is_not_float(term) (!(is_boxed(term) && *boxed_val(term)==_make_header(1,FLOAT_SUBTAG)))
#define is_not_bitstring(term) (!lb_build_is_bitstring(term))
#define is_not_port(term) (!(((term)&_TAG_IMMED1_MASK)==_TAG_IMMED1_PORT || \
    (is_boxed(term) && ((*boxed_val(term)&_TAG_HEADER_MASK)==EXTERNAL_PORT_SUBTAG))))
#define erl_send lb_erl_send
#define TRAP (1u<<LB_EXC_OFFSET)
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
#define MAP_STATUS(status) do { \
    LbMapStatus lb_map_status_=(status); \
    if(lb_map_status_!=LB_MAP_OK) { c_p->heap_reserved=0; \
        if(lb_map_status_==LB_MAP_NO_MEMORY) goto memory_failure; \
        if(lb_map_status_==LB_MAP_LIMIT) goto resource_limit; \
        if(lb_map_status_==LB_MAP_WORK_LIMIT) goto work_failure; \
        if(lb_map_status_==LB_MAP_UNSUPPORTED) goto execution_refused; \
        goto malformed_code; \
    } \
} while(0)
int lb_instruction_supported(unsigned op)
{
    switch(op) {
    case op_i_load_nif: return 1; /* reviewed capability boundary, never NIF execution */
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
    lb_message_queue_init(&p->messages); p->receive_root=NIL;
    p->entry_module=module; p->heap=p->htop=memory; p->hend=p->heap+words; p->stop=p->hend-1;
    *p->stop=(Eterm)lb_host_return; p->bin_vheap_sz=LB_BINARY_VHEAP_MIN_WORDS;
    for(i=0;i<MAX_REG;++i) p->reg[i]=NIL;
    p->fvalue=p->exception_value=p->construction_root=p->native_time_root=p->ftrace=p->caught_trace=NIL; p->freason=LB_EXC_NULL;
    p->local_mfa=*mfa; p->arity=mfa->arity; p->current=&p->local_mfa;
    p->i=pc; p->status=LB_PROCESS_READY; p->error_handler=am_error_handler; p->group_leader=NIL;
    ++module->users; *out=p; return LB_PROCESS_READY;
}
static int private_mfa(const LbCodeModule *module,const BeamInstr *pc,LbMFA *mfa)
{
    if(!lb_code_module_ready(module) || !module->words) return 0;
    for(size_t i=0;i<lb_code_module_function_count(module);++i) {
        if(!lb_work_domain_charge(module->space->domain,1)) return 0;
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
    if(lb_fun_is_external(fun)) return LB_PROCESS_REFUSED;
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
    if(!lb_work_domain_charge(module->space->domain,needed+lb_work_bulk_units(needed))) {
        status=LB_PROCESS_WORK_LIMIT; goto failure;
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
static LbProcessStatus spawn_allocation_failure(LbAllocDomain *domain)
{
    switch(lb_alloc_domain_failure(domain)) {
    case LB_ALLOC_WORK_LIMIT:return LB_PROCESS_WORK_LIMIT;
    case LB_ALLOC_LIMIT:return LB_PROCESS_LIMIT;
    default:return LB_PROCESS_NO_MEMORY;
    }
}
LbProcessStatus lb_process_create_mfa(LbProcess *parent,const LbSpawnMfa *start,size_t words,LbProcess **out)
{
    if(!out) return LB_PROCESS_INVALID; *out=NULL;
    if(!parent || !start || !is_atom(start->module) || !is_atom(start->function) || start->arity>MAX_ARG)
        return LB_PROCESS_INVALID;
    LbCodeSpace *space=parent->entry_module->space; Export *entry;
    for(entry=space->exports;entry;entry=entry->next) {
        if(!lb_work_domain_charge(space->domain,1)) return LB_PROCESS_WORK_LIMIT;
        ++parent->last_native_cost;
        if(entry->info.mfa.module==start->module && entry->info.mfa.function==start->function && entry->info.mfa.arity==start->arity) break;
    }
    /* Do not publish a pretend child for absent code or a native-only export.
     * Those startup paths need their own real exception/dispatch consumers. */
    if(!entry || !entry->owner || !entry->owner->published) return LB_PROCESS_REFUSED;
    size_t needed=0; Eterm cursor=start->arguments;
    for(unsigned i=0;i<start->arity;++i) {
        size_t size;
        if(!is_list(cursor)) return LB_PROCESS_INVALID;
        if(!lb_flat_size(space->domain,CAR(list_val(cursor)),&size)) return spawn_allocation_failure(space->domain);
        if(!lb_size_add(needed,size,&needed)) return LB_PROCESS_LIMIT;
        cursor=CDR(list_val(cursor));
    }
    if(!is_nil(cursor)) return LB_PROCESS_INVALID;
    if(!lb_size_add(needed,S_RESERVED+1,&needed) || needed>64u*1024u*1024u/sizeof(Eterm) ||
       words>64u*1024u*1024u/sizeof(Eterm)) return LB_PROCESS_LIMIT;
    if(words<needed) words=needed;
    LbProcess *child; LbProcessStatus status=create_owned(entry->owner,entry->dispatch.addresses[0],&entry->info.mfa,words,&child);
    if(status!=LB_PROCESS_READY) return status==LB_PROCESS_NO_MEMORY?spawn_allocation_failure(space->domain):status;
    cursor=start->arguments;
    for(unsigned i=0;i<start->arity;++i) {
        LbAllocStatus held=lb_fun_retain_term(child,CAR(list_val(cursor)),&parent->last_native_cost);
        if(held!=LB_ALLOC_OK) {
            status=held==LB_ALLOC_WORK_LIMIT?LB_PROCESS_WORK_LIMIT:held==LB_ALLOC_NO_MEMORY?LB_PROCESS_NO_MEMORY:
                held==LB_ALLOC_LIMIT?LB_PROCESS_LIMIT:LB_PROCESS_REFUSED;
            goto failure;
        }
        cursor=CDR(list_val(cursor));
    }
    if(!lb_work_domain_charge(space->domain,needed+lb_work_bulk_units(needed))) { status=LB_PROCESS_WORK_LIMIT; goto failure; }
    cursor=start->arguments;
    for(unsigned i=0;i<start->arity;++i) {
        child->reg[i]=lb_copy_flat(CAR(list_val(cursor)),&child->htop,&child->off_heap);
        if(is_non_value(child->reg[i])) { status=LB_PROCESS_NO_MEMORY; goto failure; }
        cursor=CDR(list_val(cursor));
    }
    Eterm pid; status=lb_process_identity(child,&pid);
    if(status!=LB_PROCESS_READY) goto failure;
    *out=child; return LB_PROCESS_READY;
failure:
    lb_process_destroy(child);
    return status==LB_PROCESS_NO_MEMORY?spawn_allocation_failure(space->domain):status;
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
    lb_alias_deactivate_process(p);
    lb_link_exit(p,0);
    lb_monitor_exit(p,0);
    lb_registry_remove(p);
    if(p->identity.owner && lb_pid_remove(p->identity.owner,&p->identity)!=LB_PID_OK) abort();
    lb_receive_marker_destroy(p);
    lb_message_queue_clear(&p->messages);
    if(p->exit_signal) lb_message_destroy(p->exit_signal);
    lb_fun_leases_clear(p);
    while(p->catch_frames) {
        LbCatchFrame *frame=p->catch_frames; p->catch_frames=frame->previous;
        lb_release(d,frame);
    }
    lb_decimal_destroy(p->decimal);
    lb_release(p->entry_module->space->domain,p->dictionary);
    lb_offheap_clear(&p->off_heap);
    lb_offheap_clear(&p->writable);
    lb_work_process_release(p);
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
static int pc_valid_work(LbProcess *p,const BeamInstr *pc,size_t *validation_work)
{
    LbCodeModule *m; Uint address=(Uint)pc; size_t i,work=0;
    if(validation_work) *validation_work=0;
    if(pc==lb_host_return) return 1;
    if(pc==p->entry_module->space->apply_export.dispatch.addresses[0] ||
       pc==p->entry_module->space->apply_fun_export.dispatch.addresses[0]) return 1;
    if(address%sizeof(BeamInstr)) return 0;
    m=p->pc_module;
    if(m && m->words && address>=(Uint)m->words && address<(Uint)(m->words+m->word_count)) return 1;
    if(pc==p->pc_native) return 1;
    for(m=p->entry_module->space->modules;m;m=m->next) {
        work+=2; /* Pay only when an execution edge changes code ranges. */
        if(m->words && address>=(Uint)m->words && address<(Uint)(m->words+m->word_count)) {
            p->pc_module=m;
            if(validation_work) *validation_work=work;
            return 1;
        }
    }
    for(i=0;i<LB_NATIVE_COUNT;++i) {
        ++work;
        if(pc==p->entry_module->space->natives[i].dispatch.addresses[0]) {
            p->pc_native=pc;
            if(validation_work) *validation_work=work;
            return 1;
        }
    }
    if(validation_work) *validation_work=work;
    return 0;
}
static int pc_valid(LbProcess *p,const BeamInstr *pc)
{ return pc_valid_work(p,pc,NULL); }
static const LbMFA *module_entry_mfa(LbProcess *p,LbCodeModule *module,const BeamInstr *pc)
{
    for(size_t i=0;i<module->export_count;++i) {
        if(!lb_work_domain_charge(module->space->domain,1)) return NULL;
        if(module->exports[i].dispatch.addresses[0]==pc) return &module->exports[i].info.mfa;
    }
    return private_mfa(module,pc,&p->local_mfa)?&p->local_mfa:NULL;
}
static const LbMFA *entry_mfa(LbProcess *p,const BeamInstr *pc)
{
    LbCodeSpace *space=p->entry_module->space;
    LbCodeModule *module=p->pc_module; Uint address=(Uint)pc;
    if(pc==space->apply_export.dispatch.addresses[0]) return &space->apply_export.info.mfa;
    if(pc==space->apply_fun_export.dispatch.addresses[0]) return &space->apply_fun_export.info.mfa;
    /* Module ranges and their function directories are immutable after
     * publication; only a code-range transition scans the namespace. */
    if(module && module->words && address>=(Uint)module->words &&
       address<(Uint)(module->words+module->word_count)) return module_entry_mfa(p,module,pc);
    for(size_t i=0;i<LB_NATIVE_COUNT;++i) {
        if(!lb_work_domain_charge(space->domain,1)) return NULL;
        if(pc==space->natives[i].dispatch.addresses[0]) return &space->natives[i].info.mfa;
    }
    for(module=space->modules;module;module=module->next) {
        if(!lb_work_domain_charge(space->domain,1)) return NULL;
        if(!module->words || address<(Uint)module->words || address>=(Uint)(module->words+module->word_count)) continue;
        p->pc_module=module;
        return module_entry_mfa(p,module,pc);
    }
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
#define is_not_atom(term) (!is_atom(term))
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
#define MAP_PUT_TERM_REG(term,desc) do { \
    Eterm lb_target_=(desc); \
    switch(loader_tag(lb_target_)) { \
    case LOADER_X_REG:x(loader_x_reg_index(lb_target_))=(term);break; \
    case LOADER_Y_REG:y(loader_y_reg_index(lb_target_))=(term);break; \
    default:goto malformed_code; \
    } \
} while(0)
#define Ib(word) (word)
#define tb(word) (word)
#define fb(word) ((Sint)(int32_t)(word))
#define jb(word) fb(word)
#define db(word) (word)
#define FLOAT_REG(offset) (c_p->float_regs[(offset)/sizeof(double)])
#define FLOAT_REG_VALID(offset) (((offset)%sizeof(double))==0 && (offset)/sizeof(double)<MAX_REG)
#define is_both_small(a,b) (is_small(a) && is_small(b))
#define is_value(value) (!is_non_value(value))
#define cp_val(word) ((const BeamInstr *)(word))
#define make_blank(slot) ((slot)=NIL)
#define ADD_BYTE_OFFSET(pointer,offset) ((Eterm *)((unsigned char *)(pointer)+(offset)))
#define SET_I(pc) do { const BeamInstr *next_pc=(pc); lb_trace_callsite(c_p,I,next_pc); I=next_pc; } while(0)
#define VALID_INSTR(op) ((op)<NUM_SPECIFIC_OPS)
#define CHECK_TERM(term) ASSERT(!is_non_value(term))
#define CHECK_ARGS(pc) ASSERT(pc_valid(c_p,(pc)))
#define HEAP_SPACE_VERIFIED(n) do { if((Uint)(E-HTOP)<(Uint)(n)+S_RESERVED) goto malformed_code; c_p->heap_reserved=(n); } while(0)
#define SWAPOUT do { c_p->htop=HTOP; c_p->stop=E; c_p->i=I; } while(0)
#define SWAPIN do { HTOP=c_p->htop; E=c_p->stop; } while(0)
#define Goto(op) do { next_op=(op); goto dispatch; } while(0)
#define GotoPF(op) Goto(op)
#define BADARG LB_EXC_BADARG
#define BADARITH LB_EXC_BADARITH
#define SMALL_ZERO make_small(0)
#define SYSTEM_LIMIT LB_EXC_SYSTEM_LIMIT
#define BSF_LITTLE 2
#define BSF_SIGNED 4
#define BADMATCH LB_EXC_BADMATCH
#define EXC_CASE_CLAUSE LB_EXC_CASE_CLAUSE
typedef enum { LB_LENGTH_MORE, LB_LENGTH_DONE, LB_LENGTH_BADARG, LB_LENGTH_SYSTEM_LIMIT, LB_LENGTH_WORK_LIMIT } LbLengthStep;
static LbLengthStep lb_length_step(LbProcess *p,Uint live,Sint *reductions)
{
    Eterm *state=p->reg+live; Eterm tail=state[0]; Sint count;
    unsigned steps=0;
    if(!is_small(state[1])) return LB_LENGTH_BADARG;
    count=signed_val(state[1]);
    while(is_list(tail) && steps<64u && *reductions>0) {
        if(count>=MAX_SMALL) return LB_LENGTH_SYSTEM_LIMIT;
        if(!lb_work_domain_charge(p->entry_module->space->domain,1)) {
            p->status=LB_PROCESS_WORK_LIMIT; return LB_LENGTH_WORK_LIMIT;
        }
        tail=CDR(list_val(tail)); ++count; ++steps; --*reductions;
        state[0]=tail; state[1]=make_small(count);
    }
    if(is_nil(tail)) return LB_LENGTH_DONE;
    return is_list(tail)?LB_LENGTH_MORE:LB_LENGTH_BADARG;
}
LbProcessStatus lb_process_run(LbProcess *c_p,size_t budget,unsigned reductions)
{
    Eterm *reg,*E,*HTOP;
    const BeamInstr *I;
    BeamInstr next_op;
    Sint FCALLS,neg_o_reds=0;
    Uint bitdata=c_p?c_p->bitdata:0;
    if(!c_p || c_p->running || c_p->entry_module->space->loading) return LB_PROCESS_INVALID;
    if(c_p->status!=LB_PROCESS_READY && c_p->status!=LB_PROCESS_YIELDED) return c_p->status;
    if(reductions<4) return LB_PROCESS_INVALID;
    if(c_p->waiting) return LB_PROCESS_INVALID;
    if(c_p->receive_message) c_p->flags|=F_DELAY_GC;
    c_p->status=LB_PROCESS_READY; /* A completed slice is resumable, not a persistent BIF-yield signal. */
    c_p->running=1; reg=c_p->reg; I=c_p->i; E=c_p->stop; HTOP=c_p->htop; FCALLS=reductions;
    if(c_p->exit_signal) {
        SWAPOUT; c_p->status=lb_exit_signal_receive(c_p); SWAPIN; goto stop;
    }
    next_op=*I;
dispatch:
    if(c_p->exit_signal) {
        SWAPOUT; c_p->status=lb_exit_signal_receive(c_p); SWAPIN; goto stop;
    }
    if(!budget--) goto context_switch3;
    /* Candidate examination is sender-causal, before import/removal/matching. */
    if(next_op==op_i_loop_rec_f && !lb_work_receive_begin(c_p,*c_p->messages.save)) goto resource_limit;
    if(!lb_work_domain_charge(c_p->entry_module->space->domain,1)) goto work_failure;
    /* Cache the last immutable module range so the steady-state PC guard is
     * constant-time. Charge full native/private-directory scans only on a
     * code-range transition, not inside CHECK_ARGS' Debug-only validation. */
    size_t validation_work=0;
    if(!pc_valid_work(c_p,I,&validation_work)) goto malformed_code;
    if(validation_work && !lb_work_domain_charge(c_p->entry_module->space->domain,validation_work)) goto work_failure;
    if(!VALID_INSTR(next_op) || (c_p->decimal && next_op!=op_call_bif_W)) goto malformed_code;
    {
        Uint cost; LbAllocStatus held=lb_fun_prepare_instruction(c_p,I,&cost);
        FCALLS-=(Sint)cost;
        if(held==LB_ALLOC_NO_MEMORY) goto memory_failure;
        if(held==LB_ALLOC_WORK_LIMIT) goto work_failure;
        if(held!=LB_ALLOC_OK) goto malformed_code;
    }
    switch(next_op) {
    OpCase(i_load_nif): {
        /* Native ops.tab lowers direct load_nif/2 calls to this instruction.
         * The authorized policy boundary replaces the entire effectful native
         * operation, before code-modification permission or library access. */
        SWAPOUT;
        Eterm denied=lb_bif_deny_load_nif_2(c_p,reg,I);
        SWAPIN; FCALLS-=(Sint)c_p->last_native_cost;
        assert(is_non_value(denied));
        (void)denied;
        if(c_p->status==LB_PROCESS_WORK_LIMIT) goto work_failure;
        goto capability_denied;
    }
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
        if(lb_alloc_domain_failure(c_p->entry_module->space->domain)==LB_ALLOC_WORK_LIMIT) goto work_failure;
        if(c_p->decimal) {
            /* Reenter this actual native trampoline on the next worker slice.
             * Heap collection cannot invalidate the independent continuation. */
            if(!is_non_value(value) || c_p->status==LB_PROCESS_NO_MEMORY || c_p->status==LB_PROCESS_EXCEPTION) goto malformed_code;
            goto context_switch;
        }
        if(is_non_value(value)) {
            if(c_p->status==LB_PROCESS_YIELDED) { I=c_p->i; goto context_switch; }
            if(c_p->status==LB_PROCESS_INVALID) goto malformed_code;
            if(c_p->status==LB_PROCESS_NO_MEMORY) goto memory_failure;
            if(c_p->status==LB_PROCESS_LIMIT) goto resource_limit;
            if(c_p->status==LB_PROCESS_REFUSED) goto execution_refused;
            if(c_p->status==LB_PROCESS_CAPABILITY_DENIED) goto capability_denied;
            if(c_p->status==LB_PROCESS_ENVIRONMENT) goto environment_failure;
            goto find_func_info;
        }
        x(0)=value; SET_I(cp_val(*E)); *E=NIL;
        if(c_p->yield_requested) { c_p->yield_requested=0; c_p->arity=1; goto context_switch3; }
        Goto(*I);
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
    c_p->flags &= ~(1u<<13); /* native F_DELAY_GC cannot cross schedule-out */
    c_p->status=LB_PROCESS_YIELDED; goto stop;
find_func_info: {
    if(c_p->status==LB_PROCESS_CAPABILITY_DENIED) goto capability_denied;
    if(lb_alloc_domain_failure(c_p->entry_module->space->domain)==LB_ALLOC_WORK_LIMIT) goto work_failure;
    /* Expand guest exception reasons once, then either unwind to an owned
     * catch target or publish the terminal exception without host handlers. */
    LbAllocStatus expanded;
    Uint exception_class=LB_GET_EXC_CLASS(c_p->freason);
    if((exception_class!=LB_EXTAG_ERROR && exception_class!=LB_EXTAG_EXIT && exception_class!=LB_EXTAG_THROWN) ||
       (c_p->freason&(LB_EXF_RESTORE_NFUNC|LB_EXF_PANIC|LB_EXF_NATIVE)) ||
       ((c_p->freason&LB_EXF_THROWN) && exception_class!=LB_EXTAG_THROWN)) goto malformed_code;
    Eterm reason=c_p->fvalue;
    if(c_p->freason&LB_EXF_ARGLIST) {
        if(!is_tuple(reason)) goto malformed_code;
        Eterm *tuple=tuple_val(reason);
        if(arityval(tuple[0])!=((c_p->freason&LB_EXF_HAS_EXT_INFO)?3u:2u)) goto malformed_code;
        reason=tuple[1];
    }
    SWAPOUT; expanded=lb_exception_expand(c_p,c_p->freason,reason,&c_p->exception_value); SWAPIN;
    if(expanded==LB_ALLOC_NO_MEMORY) goto memory_failure;
    if(expanded!=LB_ALLOC_OK) goto malformed_code;
    if(c_p->catch_frames) {
        LbCatchFrame *frame=c_p->catch_frames;
        size_t stack_words=(size_t)(c_p->hend-c_p->heap);
        Eterm caught_class=lb_exception_class(c_p->freason);
        Eterm caught_reason;
        Eterm caught_trace=c_p->ftrace;
        if(!is_atom(caught_class) || frame->stack_offset>stack_words) goto malformed_code;
        if(exception_class==LB_EXTAG_ERROR && (c_p->freason&LB_EXF_SAVETRACE)) {
            Eterm exception_and_trace;
            LbProcessStatus old_status=c_p->status;
            c_p->status=LB_PROCESS_EXCEPTION; SWAPOUT;
            LbProcessStatus trace_status=lb_error_down_reason(c_p,&exception_and_trace);
            SWAPIN; c_p->status=old_status;
            if(trace_status==LB_PROCESS_WORK_LIMIT) goto work_failure;
            if(trace_status==LB_PROCESS_LIMIT) goto resource_limit;
            if(trace_status==LB_PROCESS_NO_MEMORY) {
                if(lb_alloc_domain_failure(c_p->entry_module->space->domain)==LB_ALLOC_LIMIT) goto resource_limit;
                if(lb_alloc_domain_failure(c_p->entry_module->space->domain)==LB_ALLOC_WORK_LIMIT) goto work_failure;
                goto memory_failure;
            }
            if(trace_status==LB_PROCESS_REFUSED) goto execution_refused;
            if(trace_status!=LB_PROCESS_READY || !is_tuple(exception_and_trace) || arityval(*tuple_val(exception_and_trace))!=2) goto malformed_code;
            caught_trace=tuple_val(exception_and_trace)[2];
        } else if(c_p->ftrace!=NIL) {
            LbRaisedTrace supplied;
            if(!lb_raise_trace_unpack(c_p->ftrace,&supplied)) goto malformed_code;
            caught_trace=supplied.symbolic;
        }
        c_p->caught_trace=caught_trace;
        caught_reason=c_p->exception_value;
        E= c_p->hend-frame->stack_offset;
        if(E<c_p->stop || E>=c_p->hend || frame->y_offset/sizeof(Eterm)>=(size_t)(c_p->hend-E) ||
           !pc_valid(c_p,frame->failure_pc)) goto malformed_code;
        reg[0]=THE_NON_VALUE; reg[1]=caught_reason; reg[2]=caught_trace; reg[3]=caught_class;
        c_p->fvalue=NIL; c_p->ftrace=NIL; c_p->exception_value=NIL;
        I=frame->failure_pc; Goto(*I);
    }
    if(!(c_p->freason&(LB_EXF_HAS_EXT_INFO|LB_EXF_ARGLIST))) c_p->fvalue=NIL;
    /* Only uncaught throws are promoted to nocatch; caught throws retain their
     * class and value in the X registers at the local handler. */
    if(c_p->freason&LB_EXF_THROWN) {
        size_t need;
        if(!lb_work_domain_charge(c_p->entry_module->space->domain,1)) goto work_failure;
        if(!lb_size_add(3,c_p->heap_reserved,&need)) goto resource_limit;
        SWAPOUT;
        int reserved=lb_heap_reserve(c_p,need,MAX_REG);
        SWAPIN;
        if(!reserved) goto memory_failure;
        c_p->exception_value=TUPLE2(HTOP,am_nocatch,c_p->exception_value); HTOP+=3;
        c_p->freason=LB_EXC_ERROR|(c_p->freason&LB_EXF_HAS_EXT_INFO);
        if(c_p->ftrace!=NIL) c_p->freason&=~LB_EXF_SAVETRACE;
    }
    c_p->status=LB_PROCESS_EXCEPTION; goto stop;
}
capability_denied:
    assert(c_p->capability_failure);
    c_p->status=LB_PROCESS_CAPABILITY_DENIED; goto stop;
execution_refused:
    c_p->status=LB_PROCESS_REFUSED; goto stop;
environment_failure:
    c_p->status=LB_PROCESS_ENVIRONMENT; goto stop;
resource_limit:
    c_p->status=LB_PROCESS_LIMIT; goto stop;
work_failure:
    c_p->status=LB_PROCESS_WORK_LIMIT; goto stop;
memory_failure:
    /* The failed collection left the old roots intact. Native call setup and
     * generated allocation paths also land here; preserve the domain's cause
     * rather than turning an accounted denial into callback OOM. */
    if(lb_alloc_domain_failure(c_p->entry_module->space->domain)==LB_ALLOC_LIMIT) goto resource_limit;
    if(lb_alloc_domain_failure(c_p->entry_module->space->domain)==LB_ALLOC_WORK_LIMIT) goto work_failure;
    c_p->status=LB_PROCESS_NO_MEMORY; goto stop;
malformed_code:
    c_p->status=LB_PROCESS_INVALID;
stop:
    if(FCALLS<(Sint)reductions) {
        uint64_t consumed=(uint64_t)((Sint)reductions-FCALLS);
        c_p->reductions_total=UINT64_MAX-c_p->reductions_total<consumed?
            UINT64_MAX:c_p->reductions_total+consumed;
    }
    /* Terminal monitor capture may collect. Publish the active heap/stack/PC
     * first and reload afterward; local interpreter aliases are not roots. */
    SWAPOUT;
    c_p->flags &= ~(1u<<13);
    /* Guest exit removes identity before publishing completion. The retained
     * process/result allocation may outlive that logical death. */
    LbAllocStatus failure=lb_alloc_domain_failure(c_p->entry_module->space->domain);
    if(failure==LB_ALLOC_WORK_LIMIT) c_p->status=LB_PROCESS_WORK_LIMIT;
    if(c_p->status!=LB_PROCESS_YIELDED) {
        lb_link_exit(c_p,c_p->status!=LB_PROCESS_WORK_LIMIT &&
                     c_p->status!=LB_PROCESS_CAPABILITY_DENIED && failure!=LB_ALLOC_LIMIT);
        failure=lb_alloc_domain_failure(c_p->entry_module->space->domain);
        lb_monitor_exit(c_p,c_p->status!=LB_PROCESS_WORK_LIMIT &&
                        c_p->status!=LB_PROCESS_CAPABILITY_DENIED && failure!=LB_ALLOC_LIMIT);
        lb_registry_remove(c_p);
        c_p->receive_message=NULL; c_p->receive_root=NIL;
        lb_message_queue_clear(&c_p->messages);
    }
    if(c_p->status!=LB_PROCESS_YIELDED && c_p->identity.owner &&
       lb_pid_remove(c_p->identity.owner,&c_p->identity)!=LB_PID_OK) abort();
    c_p->bitdata=bitdata;
    SWAPIN;
    SWAPOUT; c_p->running=0; return c_p->status;
}
