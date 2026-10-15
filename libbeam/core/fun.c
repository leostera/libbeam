/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1996-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#include "process_internal.h"
#include "fun.h"
#include "flatmap.h"
#include "big.h"
#include "binary.h"
#include "atoms.h"
#include <stdio.h>

/* beam_common.c is_function2 and erl_bif_op.c erl_is_function, restricted to
 * owned local funs. The cold guard retains native badarg pending flags; it
 * performs no allocation and cannot conceal an infrastructure failure. */
int lb_fun_is_arity(Eterm value,Uint arity)
{
    const LbFun *fun=lb_fun(value); return fun && lb_fun_arity(fun)==arity;
}
Eterm lb_fun_test_arity(LbProcess *p,Eterm value,Eterm argument)
{
    Sint arity;
    if(is_small(argument)) {
        arity=signed_val(argument);
        if(arity<0) goto error;
    } else if(lb_is_big(argument) && (*boxed_val(argument)&_HEADER_SUBTAG_MASK)==POS_BIG_SUBTAG) arity=-1;
    else goto error;
    return lb_fun_is_arity(value,(Uint)arity)?am_true:am_false;
error:
    p->freason=LB_EXC_BADARG; return THE_NON_VALUE;
}
static int fun_name_is_plain(LbBeamBytes name,int allow_dot)
{
    for(size_t i=0;i<name.size;++i) {
        unsigned char c=name.data[i];
        if(!((c>='a'&&c<='z') || (c>='A'&&c<='Z') || (c>='0'&&c<='9') ||
             c=='_' || c=='@' || (allow_dot && c=='.'))) return 0;
    }
    return name.size!=0;
}
Eterm lb_bif_fun_to_list_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)pc; LbAllocDomain *domain=p->entry_module->space->domain; p->last_native_cost=1;
    if(!lb_work_domain_charge(domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    const LbFun *fun=lb_fun(args[0]);
    if(!fun) { p->freason=LB_EXC_BADARG; return THE_NON_VALUE; }
    LbBeamBytes module={0},function={0}; int written;
    if(lb_fun_is_external(fun)) {
        const Export *exported=fun->entry.exp;
        if(!exported || lb_atoms_name(p->entry_module->space->atoms,exported->info.mfa.module,&module)!=LB_ATOM_OK ||
           lb_atoms_name(p->entry_module->space->atoms,exported->info.mfa.function,&function)!=LB_ATOM_OK) {
            p->status=LB_PROCESS_INVALID; return THE_NON_VALUE;
        }
        if(!fun_name_is_plain(module,1) || !fun_name_is_plain(function,0)) { p->status=LB_PROCESS_REFUSED; return THE_NON_VALUE; }
    } else {
        const LbFunEntry *entry=fun->entry.fun;
        if(!entry || lb_atoms_name(p->entry_module->space->atoms,entry->module,&module)!=LB_ATOM_OK) {
            p->status=LB_PROCESS_INVALID; return THE_NON_VALUE;
        }
        if(!fun_name_is_plain(module,1)) { p->status=LB_PROCESS_REFUSED; return THE_NON_VALUE; }
    }
    char text[512];
    if(lb_fun_is_external(fun)) {
        written=snprintf(text,sizeof(text),"fun %.*s:%.*s/%u",(int)module.size,(const char *)module.data,
                         (int)function.size,(const char *)function.data,(unsigned)fun->entry.exp->info.mfa.arity);
    } else {
        const LbFunEntry *entry=fun->entry.fun;
        written=snprintf(text,sizeof(text),"#Fun<%.*s.%d.%d>",(int)module.size,(const char *)module.data,
                         entry->old_index,entry->old_uniq);
    }
    if(written<0 || (size_t)written>=sizeof(text)) { p->status=LB_PROCESS_REFUSED; return THE_NON_VALUE; }
    if(lb_fun_is_external(fun)) {
        LbAllocStatus retained=lb_fun_retain_export(p->entry_module->space,&p->fun_leases,
                                                     fun->entry.exp,&p->last_native_cost);
        if(retained!=LB_ALLOC_OK) {
            p->status=retained==LB_ALLOC_WORK_LIMIT?LB_PROCESS_WORK_LIMIT:
                retained==LB_ALLOC_LIMIT?LB_PROCESS_LIMIT:
                retained==LB_ALLOC_NO_MEMORY?LB_PROCESS_NO_MEMORY:LB_PROCESS_REFUSED;
            return THE_NON_VALUE;
        }
    }
    size_t words,need,collections=p->collections;
    if(!lb_size_mul((size_t)written,2,&words) || !lb_size_add(words,p->heap_reserved,&need)) {
        p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE;
    }
    if(!lb_work_domain_charge(domain,words)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    p->last_native_cost+=words;
    if(!lb_heap_reserve(p,need,MAX_REG)) {
        LbAllocStatus cause=lb_alloc_domain_failure(domain);
        p->status=cause==LB_ALLOC_WORK_LIMIT?LB_PROCESS_WORK_LIMIT:
            cause==LB_ALLOC_LIMIT?LB_PROCESS_LIMIT:LB_PROCESS_NO_MEMORY;
        return THE_NON_VALUE;
    }
    if(p->collections!=collections) p->last_native_cost+=p->last_gc_cost;
    Eterm result=NIL;
    for(size_t i=(size_t)written;i>0;--i) {
        p->htop[0]=make_small((unsigned char)text[i-1]); p->htop[1]=result;
        result=make_list(p->htop); p->htop+=2;
    }
    return result;
}
Eterm lb_bif_is_function_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)p; (void)pc; return lb_fun(args[0])?am_true:am_false;
}
Eterm lb_bif_is_function_2(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)pc; return lb_fun_test_arity(p,args[0],args[1]);
}
static LbProcessStatus fun_info_allocation_failure(LbAllocStatus cause)
{
    return cause==LB_ALLOC_WORK_LIMIT?LB_PROCESS_WORK_LIMIT:
        cause==LB_ALLOC_NO_MEMORY?LB_PROCESS_NO_MEMORY:LB_PROCESS_LIMIT;
}
Eterm lb_bif_fun_info_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)pc; p->last_native_cost=0; LbAllocDomain *domain=p->entry_module->space->domain;
    if(!lb_work_domain_charge(domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    ++p->last_native_cost;
    const LbFun *fun=lb_fun(args[0]);
    if(!fun) { p->freason=LB_EXC_BADARG; return THE_NON_VALUE; }
    int external=lb_fun_is_external(fun); size_t free_count=external?0:lb_fun_free(fun);
    size_t field_count=external?5:9, words,need;
    if(!lb_size_mul(field_count,5,&words) || !lb_size_add(words,2*free_count,&words) ||
       (!external && !lb_size_add(words,lb_bitstring_heap_words(128),&words)) ||
       !lb_size_add(words,p->heap_reserved,&need)) { p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE; }
    uint64_t work=(uint64_t)field_count*5+2*(uint64_t)free_count;
    if(!lb_work_domain_charge(domain,work)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    p->last_native_cost+=work; size_t collections=p->collections;
    if(!lb_heap_reserve(p,need,MAX_REG)) { p->status=fun_info_allocation_failure(lb_alloc_domain_failure(domain)); return THE_NON_VALUE; }
    if(p->collections!=collections) p->last_native_cost+=p->last_gc_cost;
    fun=lb_fun(args[0]); external=lb_fun_is_external(fun); const LbFunEntry *local=external?NULL:fun->entry.fun;
    const Export *export=external?fun->entry.exp:NULL; free_count=local?lb_fun_free(fun):0;
    Eterm keys[9]={am_type,am_env,am_arity,am_name,am_uniq,am_index,am_new_uniq,am_new_index,am_module};
    Eterm values[9]={external?am_external:am_local,NIL,make_small(lb_fun_arity(fun)),
        external?export->info.mfa.function:local->function,
        local?make_small(local->old_uniq):am_undefined,
        local?make_small(local->old_index):am_undefined,
        am_undefined,local?make_small(local->index):am_undefined,
        external?export->info.mfa.module:local->module};
    if(local) {
        for(size_t i=free_count;i>0;--i) {
            p->htop[0]=fun->env[i-1]; p->htop[1]=values[1];
            values[1]=make_list(p->htop); p->htop+=2;
        }
        LbAllocStatus status=lb_bitstring_build(domain,&p->off_heap,&p->htop,local->uniq,128,0,&values[6]);
        if(status!=LB_ALLOC_OK) { p->status=fun_info_allocation_failure(status); return THE_NON_VALUE; }
    }
    Eterm result=NIL;
    for(size_t i=0;i<9;++i) {
        if(values[i]==am_undefined) continue;
        Eterm pair=TUPLE2(p->htop,keys[i],values[i]); p->htop+=3;
        result=CONS(p->htop,pair,result); p->htop+=2;
    }
    return result;
}
Eterm lb_bif_fun_info_2(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)pc; p->last_native_cost=0; LbAllocDomain *domain=p->entry_module->space->domain;
    if(!lb_work_domain_charge(domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    ++p->last_native_cost;
    const LbFun *fun=lb_fun(args[0]); Eterm item=args[1];
    if(!fun) { p->freason=LB_EXC_BADARG; return THE_NON_VALUE; }
    int external=lb_fun_is_external(fun); const LbFunEntry *local=external?NULL:fun->entry.fun;
    const Export *export=external?fun->entry.exp:NULL;
    size_t free_count=local?lb_fun_free(fun):0,required=3+2*free_count;
    if(item!=am_type && item!=am_module && item!=am_new_index && item!=am_new_uniq &&
       item!=am_index && item!=am_uniq && item!=am_env && item!=am_refc &&
       item!=am_arity && item!=am_name) { p->freason=LB_EXC_BADARG; return THE_NON_VALUE; }
    if(item==am_new_uniq && local) {
        size_t binary_words=lb_bitstring_heap_words(128);
        if(!binary_words || !lb_size_add(required,binary_words,&required)) { p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE; }
    }
    size_t need,collections=p->collections;
    if(!lb_size_add(required,p->heap_reserved,&need)) { p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE; }
    if(!lb_heap_reserve(p,need,MAX_REG)) { p->status=fun_info_allocation_failure(lb_alloc_domain_failure(domain)); return THE_NON_VALUE; }
    if(p->collections!=collections) p->last_native_cost+=p->last_gc_cost;
    fun=lb_fun(args[0]); external=lb_fun_is_external(fun); local=external?NULL:fun->entry.fun; export=external?fun->entry.exp:NULL;
    Eterm value=THE_NON_VALUE;
    if(item==am_type) value=external?am_external:am_local;
    else if(item==am_module) value=external?export->info.mfa.module:local->module;
    else if(item==am_name) value=external?export->info.mfa.function:local->function;
    else if(item==am_arity) value=make_small(lb_fun_arity(fun));
    else if(item==am_env) {
        value=NIL;
        for(size_t i=free_count;i>0;--i) {
            if(!lb_work_domain_charge(domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
            p->last_native_cost++;
            p->htop[0]=fun->env[i-1]; p->htop[1]=value; value=make_list(p->htop); p->htop+=2;
        }
    } else if(item==am_refc) value=external?am_undefined:make_small(1);
    else if(item==am_index) value=local?make_small(local->old_index):am_undefined;
    else if(item==am_uniq) value=local?make_small(local->old_uniq):am_undefined;
    else if(item==am_new_index) value=local?make_small(local->index):am_undefined;
    else if(item==am_new_uniq) {
        if(!local) value=am_undefined;
        else {
            if(!lb_work_domain_charge(domain,sizeof(local->uniq))) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
            p->last_native_cost+=sizeof(local->uniq);
            LbAllocStatus status=lb_bitstring_build(domain,&p->off_heap,&p->htop,local->uniq,128,0,&value);
            if(status!=LB_ALLOC_OK) { p->status=fun_info_allocation_failure(status); return THE_NON_VALUE; }
        }
    }
    if(!lb_work_domain_charge(domain,2)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    p->last_native_cost+=2;
    Eterm result=TUPLE2(p->htop,item,value); p->htop+=3; return result;
}
Eterm lb_bif_make_fun_3(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)pc; p->last_native_cost=0; LbCodeSpace *space=p->entry_module->space;
    if(!lb_work_domain_charge(space->domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    ++p->last_native_cost;
    if(!is_atom(args[0]) || !is_atom(args[1]) || !is_small(args[2]) || signed_val(args[2])<0) {
        p->freason=LB_EXC_BADARG; return THE_NON_VALUE;
    }
    if((Uint)signed_val(args[2])>MAX_ARG) { p->freason=LB_EXC_SYSTEM_LIMIT; return THE_NON_VALUE; }
    unsigned arity=(unsigned)signed_val(args[2]); Export *export=NULL;
    for(Export *candidate=space->exports;candidate;candidate=candidate->next) {
        if(!lb_work_domain_charge(space->domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
        ++p->last_native_cost;
        if(candidate->info.mfa.module==args[0] && candidate->info.mfa.function==args[1] &&
           (unsigned)candidate->info.mfa.arity==arity) { export=candidate; break; }
    }
    if(!export) { p->freason=LB_EXC_UNDEF; return THE_NON_VALUE; }
    size_t need;
    if(!lb_size_add(LB_FUN_WORDS,p->heap_reserved,&need)) { p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE; }
    if(!lb_work_domain_charge(space->domain,LB_FUN_WORDS)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    p->last_native_cost+=LB_FUN_WORDS;
    size_t collections=p->collections;
    if(!lb_heap_reserve(p,need,MAX_REG)) { p->status=fun_info_allocation_failure(lb_alloc_domain_failure(space->domain)); return THE_NON_VALUE; }
    if(p->collections!=collections) p->last_native_cost+=p->last_gc_cost;
    LbAllocStatus retained=lb_fun_retain_export(space,&p->fun_leases,export,&p->last_native_cost);
    if(retained!=LB_ALLOC_OK) { p->status=fun_info_allocation_failure(retained); return THE_NON_VALUE; }
    LbFun *fun=(LbFun *)p->htop;
    fun->thing_word=LB_FUN_HEADER(arity,0)|((Uint)1<<LB_FUN_KIND_SHIFT);
    fun->entry.exp=export; p->htop+=LB_FUN_WORDS;
    return make_boxed((const Eterm *)fun);
}
Eterm lb_bif_fun_info_mfa_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)pc; p->last_native_cost=0; LbAllocDomain *domain=p->entry_module->space->domain;
    if(!lb_work_domain_charge(domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    ++p->last_native_cost;
    const LbFun *fun=lb_fun(args[0]);
    if(!fun) { p->freason=LB_EXC_BADARG; return THE_NON_VALUE; }
    size_t need;
    if(!lb_size_add(4,p->heap_reserved,&need)) { p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE; }
    size_t collections=p->collections;
    if(!lb_heap_reserve(p,need,MAX_REG)) { p->status=fun_info_allocation_failure(lb_alloc_domain_failure(domain)); return THE_NON_VALUE; }
    if(p->collections!=collections) p->last_native_cost+=p->last_gc_cost;
    fun=lb_fun(args[0]); Eterm module,function;
    if(lb_fun_is_external(fun)) { module=fun->entry.exp->info.mfa.module; function=fun->entry.exp->info.mfa.function; }
    else { module=fun->entry.fun->module; function=fun->entry.fun->function; }
    if(!lb_work_domain_charge(domain,4)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    p->last_native_cost+=4;
    Eterm result=TUPLE3(p->htop,module,function,make_small(lb_fun_arity(fun)));
    p->htop+=4; return result;
}
static LbAllocStatus retain(LbCodeSpace *space,LbFunLease **leases,const LbFunEntry *entry)
{
    if(!space || !leases || !entry || !entry->owner || entry->owner->space!=space ||
       !entry->owner->published || !entry->dispatch.addresses[0] || entry->pend_purge_address) return LB_ALLOC_INVALID;
    for(LbFunLease *lease=*leases;lease;lease=lease->next) {
        if(!lb_work_domain_charge(space->domain,1)) return LB_ALLOC_WORK_LIMIT;
        if(lease->owner==entry->owner) return LB_ALLOC_OK;
    }
    if(entry->owner->users==SIZE_MAX) return LB_ALLOC_NO_MEMORY;
    void *memory;
    if(lb_alloc_domain_allocate(space->domain,sizeof(LbFunLease),&memory)!=LB_ALLOC_OK) return LB_ALLOC_NO_MEMORY;
    LbFunLease *lease=memory; *lease=(LbFunLease){*leases,entry,entry->owner,0};
    ++entry->owner->users; *leases=lease; return LB_ALLOC_OK;
}
LbAllocStatus lb_fun_retain(LbProcess *p,const LbFunEntry *entry)
{
    if(!p || !p->entry_module) return LB_ALLOC_INVALID;
    return retain(p->entry_module->space,&p->fun_leases,entry);
}
LbAllocStatus lb_fun_retain_export(LbCodeSpace *space,LbFunLease **leases,const Export *export,Uint *cost)
{
    if(!space || !leases || !export || !cost || !export->dispatch.addresses[0]) return LB_ALLOC_INVALID;
    LbCodeModule *owner=export->owner;
    if(!owner) return LB_ALLOC_OK; /* Code-space native/instruction export; no module lease. */
    if(owner->space!=space || !owner->published) return LB_ALLOC_INVALID;
    for(LbFunLease *lease=*leases;lease;lease=lease->next) {
        if(!lb_work_domain_charge(space->domain,1)) return LB_ALLOC_WORK_LIMIT;
        ++*cost;
        if(lease->owner==owner) return LB_ALLOC_OK;
    }
    if(owner->users==SIZE_MAX) return LB_ALLOC_NO_MEMORY;
    void *memory;
    if(lb_alloc_domain_allocate(space->domain,sizeof(LbFunLease),&memory)!=LB_ALLOC_OK) return LB_ALLOC_NO_MEMORY;
    LbFunLease *lease=memory; *lease=(LbFunLease){*leases,NULL,owner,0};
    ++owner->users; *leases=lease; return LB_ALLOC_OK;
}
/* Prepare code ownership before copying a valid owned term into this process.
 * Term storage is not mutated; a failed preparation remains wholly owned by
 * the unpublished destination and is reclaimed by its destructor. */
static LbAllocStatus retain_roots(LbCodeSpace *space,LbFunLease **leases,Eterm term,Uint *cost,int pin)
{
    Eterm local[64],*todo=local; size_t count=1,capacity=64;
    LbAllocDomain *domain=space->domain;
    LbAllocStatus status=LB_ALLOC_OK; todo[0]=term;
    while(count) {
        if(!lb_work_domain_charge(domain,1)) { status=LB_ALLOC_WORK_LIMIT; break; }
        Eterm value=todo[--count]; const Eterm *children=NULL; size_t n=0;
        ++*cost;
        if(is_list(value)) { children=list_val(value); n=2; }
        else if(is_boxed(value)) {
            const Eterm *pointer=boxed_val(value);
            if(is_arity_value(*pointer)) { children=pointer+1; n=arityval(*pointer); }
            else if((*pointer&_HEADER_SUBTAG_MASK)==MAP_SUBTAG) {
                (void)lb_map_object_words(pointer);
                children=pointer+lb_map_object_child_offset(pointer);
                n=lb_map_object_child_count(pointer);
            } else if(pin && *pointer==LB_HEADER_SUB_BITS) {
                if(!lb_bitstring_pin(value)) { status=LB_ALLOC_INVALID; break; }
            } else if((*pointer&_TAG_HEADER_MASK)==FUN_SUBTAG) {
                const LbFun *fun=lb_fun(value);
                if(lb_fun_is_external(fun)) status=lb_fun_retain_export(space,leases,(const Export *)fun->entry.exp,cost);
                else status=retain(space,leases,fun->entry.fun);
                if(status!=LB_ALLOC_OK) break;
                children=fun->env; n=lb_fun_free(fun);
            }
        }
        if(n>capacity-count) {
            size_t next=capacity; void *memory;
            while(next-count<n) {
                if(next>SIZE_MAX/2/sizeof(Eterm)) { status=LB_ALLOC_NO_MEMORY; break; }
                next*=2;
            }
            if(status!=LB_ALLOC_OK) break;
            status=lb_alloc_domain_allocate(domain,next*sizeof(Eterm),&memory);
            if(status!=LB_ALLOC_OK) break;
            Eterm *grown=memory; memcpy(grown,todo,count*sizeof(Eterm));
            if(todo!=local) lb_release(domain,todo);
            todo=grown; capacity=next;
        }
        if(n) {
            if(!lb_work_domain_charge(domain,lb_work_bulk_units(n))) { status=LB_ALLOC_WORK_LIMIT; break; }
            memcpy(todo+count,children,n*sizeof(Eterm));
        }
        count+=n;
    }
    if(todo!=local) lb_release(domain,todo); return status;
}
LbAllocStatus lb_fun_retain_roots(LbCodeSpace *space,LbFunLease **leases,Eterm term,Uint *cost)
{ return retain_roots(space,leases,term,cost,0); }
LbAllocStatus lb_fun_retain_message_roots(LbCodeSpace *space,LbFunLease **leases,Eterm term,Uint *cost)
{ return retain_roots(space,leases,term,cost,1); }
LbAllocStatus lb_fun_retain_term(LbProcess *p,Eterm term,Uint *cost)
{
    return lb_fun_retain_roots(p->entry_module->space,&p->fun_leases,term,cost);
}
static void release(LbCodeSpace *space,LbFunLease *lease)
{
    LbCodeModule *owner=lease->owner;
    if(!owner || !owner->users) abort();
    --owner->users; lb_release(space->domain,lease);
}
void lb_fun_release_prefix(LbCodeSpace *space,LbFunLease **leases,LbFunLease *stop)
{
    while(*leases!=stop) {
        LbFunLease *lease=*leases;
        if(!lease) abort(); /* the saved prefix boundary belongs to this list */
        *leases=lease->next; release(space,lease);
    }
}
void lb_fun_release_roots(LbCodeSpace *space,LbFunLease **leases)
{
    lb_fun_release_prefix(space,leases,NULL);
}
void lb_fun_leases_clear(LbProcess *p)
{
    lb_fun_release_roots(p->entry_module->space,&p->fun_leases);
}
void lb_fun_marks_clear(LbProcess *p)
{
    for(LbFunLease *lease=p->fun_leases;lease;lease=lease->next) lease->marked=0;
}
void lb_fun_mark(LbProcess *p,const LbFunEntry *entry)
{
    for(LbFunLease *lease=p->fun_leases;lease;lease=lease->next)
        if(lease->owner==entry->owner) { lease->marked=1; return; }
    abort(); /* no code pointer may outlive its defining-module lease */
}
void lb_fun_mark_export(LbProcess *p,const Export *export)
{
    if(!export) abort();
    if(!export->owner) return;
    for(LbFunLease *lease=p->fun_leases;lease;lease=lease->next)
        if(lease->owner==export->owner) { lease->marked=1; return; }
    abort(); /* external code is protected by an explicit module lease */
}
static int contains_code(LbCodeModule *module,Uint address)
{
    return !(address%sizeof(BeamInstr)) && address>=(Uint)module->words && address<(Uint)(module->words+module->word_count);
}
Uint lb_fun_mark_code(LbProcess *p)
{
    if(!p->running && p->status!=LB_PROCESS_READY && p->status!=LB_PROCESS_YIELDED) return 0;
    Uint cost=0;
    for(LbFunLease *lease=p->fun_leases;lease;lease=lease->next) {
        LbCodeModule *module=lease->owner; ++cost;
        if(contains_code(module,(Uint)p->i)) { lease->marked=1; continue; }
        /* Continuations are untagged native code words among initialized Y
         * roots. Conservatively retain a module for any matching stack word;
         * a dead closure must not free code underneath a suspended call. */
        for(const Eterm *slot=p->stop;slot<p->hend;++slot) {
            ++cost;
            if(contains_code(module,*slot)) { lease->marked=1; break; }
        }
    }
    return cost/16+(cost%16!=0);
}
LbAllocStatus lb_fun_prepare_instruction(LbProcess *p,const BeamInstr *pc,Uint *cost)
{
    *cost=0; LbCodeSpace *space=p->entry_module->space;
    if(!space->has_fun_literals) return LB_ALLOC_OK;
    Uint address=(Uint)pc;
    for(LbCodeModule *module=space->modules;module;module=module->next) {
        if(!lb_work_domain_charge(space->domain,1)) return LB_ALLOC_WORK_LIMIT;
        ++*cost;
        if(!contains_code(module,address)) continue;
        size_t index=(size_t)(pc-module->words);
        for(const LbLiteralFunUse *use=module->literal_fun_uses;use;use=use->next) {
            if(!lb_work_domain_charge(space->domain,1)) return LB_ALLOC_WORK_LIMIT;
            ++*cost;
            if(use->pc!=index) continue;
            LbAllocStatus status=use->entry ? lb_fun_retain(p,use->entry) :
                lb_fun_retain_export(space,&p->fun_leases,use->external,cost);
            if(status!=LB_ALLOC_OK) return status;
        }
        break;
    }
    return LB_ALLOC_OK;
}
/* Native beam_common.c call_fun's local loaded-fun and badfun/badarity
 * paths. Unloaded/purging/export fun effects have no admission: all accepted
 * entries carry a defining-module lease and a verified loaded code address. */
const BeamInstr *lb_fun_call(LbProcess *p,unsigned arity)
{
    p->last_native_cost=0;
    /* apply/2 can present up to MAX_REG-1 arguments; larger-than-export
     * arities still require the ordinary badarity payload, not a refusal. */
    if(arity>=MAX_REG) { p->status=LB_PROCESS_INVALID; return NULL; }
    Eterm value=p->reg[arity]; const LbFun *fun=lb_fun(value);
    if(!fun) { p->current=NULL; p->freason=LB_EXC_BADFUN; p->fvalue=value; return NULL; }
    if(lb_fun_is_external(fun)) {
        const Export *export=fun->entry.exp;
        if(!export || !export->dispatch.addresses[0]) { p->status=LB_PROCESS_REFUSED; return NULL; }
        int retained=!export->owner;
        for(LbFunLease *lease=p->fun_leases;!retained && lease;lease=lease->next) {
            if(!lb_work_domain_charge(p->entry_module->space->domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return NULL; }
            ++p->last_native_cost;
            retained=lease->owner==export->owner;
        }
        if(!retained || (export->owner && (export->owner->space!=p->entry_module->space || !export->owner->published))) {
            p->status=LB_PROCESS_REFUSED; return NULL;
        }
        if(lb_fun_arity(fun)==arity && export->info.mfa.arity==arity) return export->dispatch.addresses[0];
    } else {
        const LbFunEntry *entry=fun->entry.fun; int retained=0;
        for(LbFunLease *lease=p->fun_leases;lease;lease=lease->next) {
            if(!lb_work_domain_charge(p->entry_module->space->domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return NULL; }
            ++p->last_native_cost;
            if(lease->owner==entry->owner) { retained=1; break; }
        }
        if(!retained || entry->owner->space!=p->entry_module->space ||
           !entry->owner->published || entry->pend_purge_address || !entry->dispatch.addresses[0]) {
            p->status=LB_PROCESS_REFUSED; return NULL;
        }
        if(lb_fun_arity(fun)==arity) {
            unsigned count=(unsigned)lb_fun_free(fun);
            if(arity+count>MAX_ARG || entry->arity!=(int)arity) { p->status=LB_PROCESS_INVALID; return NULL; }
        int found=0;
        for(size_t i=0;i<lb_code_module_function_count(entry->owner);++i) {
            if(!lb_work_domain_charge(p->entry_module->space->domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return NULL; }
            const BeamInstr *info=(const BeamInstr *)entry->owner->words[i]; ++p->last_native_cost;
            if(info+sizeof(LbCodeInfo)/sizeof(BeamInstr)==entry->dispatch.addresses[0]) {
                found=info[sizeof(LbCodeInfo)/sizeof(BeamInstr)-1]==arity+count; break;
            }
        }
        if(!found) { p->status=LB_PROCESS_INVALID; return NULL; }
        if(!lb_work_domain_charge(p->entry_module->space->domain,count+lb_work_bulk_units(count))) {
            p->status=LB_PROCESS_WORK_LIMIT; return NULL;
        }
            for(unsigned i=0;i<count;++i) p->reg[arity+i]=fun->env[i];
            return entry->dispatch.addresses[0];
        }
    }
    /* Build the native {Fun,Args} badarity payload in one fallible reservation.
     * Both the argument registers and fun register survive moving collection. */
    uint64_t work=arity+lb_work_bulk_units(2*(uint64_t)arity+3);
    if(!lb_work_domain_charge(p->entry_module->space->domain,work)) {
        p->status=LB_PROCESS_WORK_LIMIT; return NULL;
    }
    p->last_native_cost+=work;
    size_t before=p->collections,need;
    if(!lb_size_add(2*(size_t)arity+3,p->heap_reserved,&need)) { p->status=LB_PROCESS_LIMIT; return NULL; }
    if(!lb_heap_reserve(p,need,arity+1)) {
        LbAllocStatus cause=lb_alloc_domain_failure(p->entry_module->space->domain);
        p->status=cause==LB_ALLOC_WORK_LIMIT?LB_PROCESS_WORK_LIMIT:
            cause==LB_ALLOC_LIMIT?LB_PROCESS_LIMIT:LB_PROCESS_NO_MEMORY;
        return NULL;
    }
    if(before!=p->collections) p->last_native_cost+=p->last_gc_cost;
    Eterm args=NIL; value=p->reg[arity];
    for(unsigned i=arity;i;--i) { args=CONS(p->htop,p->reg[i-1],args); p->htop+=2; }
    p->freason=LB_EXC_BADARITY; p->fvalue=TUPLE2(p->htop,value,args); p->htop+=3;
    return NULL;
}
void lb_fun_sweep(LbProcess *p)
{
    LbFunLease **link=&p->fun_leases;
    while(*link) {
        LbFunLease *lease=*link;
        if(lease->marked) link=&lease->next;
        else { *link=lease->next; release(p->entry_module->space,lease); }
    }
}
