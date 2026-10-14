/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1996-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Adapted from erl_bif_info.c get_module_info/module_info_0/1. Owned metadata,
 * fallible preallocation and explicit process context replace ERTS factories.
 */
#include "process_internal.h"

/* dist.c:node_0 and instrs.tab:node read the current local node's name.
 * Each owned namespace starts unnamed and cannot enable distribution. */
Eterm lb_bif_node_0(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)args; (void)pc; p->last_native_cost=1;
    if(!lb_work_domain_charge(p->entry_module->space->domain,1)) {
        p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE;
    }
    return p->entry_module->space->node_name;
}
/* dist.c:nodes_0 selects visible connected peers. Owned namespaces have no
 * distribution connections, and cannot create them through a guest binding. */
Eterm lb_bif_nodes_0(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)args; (void)pc; p->last_native_cost=1;
    if(!lb_work_domain_charge(p->entry_module->space->domain,1)) {
        p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE;
    }
    return NIL;
}
Eterm lb_bif_function_exported_3(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    LbCodeSpace *space=p->entry_module->space; (void)pc; p->last_native_cost=0;
    if(!lb_work_domain_charge(space->domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    p->last_native_cost=1;
    if(!is_atom(args[0]) || !is_atom(args[1]) || !is_small(args[2])) {
        p->freason=LB_EXC_BADARG; return THE_NON_VALUE;
    }
    /* OTP narrows the small to C int before unsigned export lookup. Express
     * the observed 32-bit wrap without implementation-defined signed casts. */
    _Static_assert(sizeof(int)==4,"native export arity ABI");
    uint32_t arity=(uint32_t)signed_val(args[2]);
    for(const Export *entry=space->exports;entry;entry=entry->next) {
        if(!lb_work_domain_charge(space->domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
        ++p->last_native_cost;
        if(entry->info.mfa.module==args[0] && entry->info.mfa.function==args[1] && entry->info.mfa.arity==arity)
            return am_true;
    }
    return am_false;
}
static int module_list_heap(LbProcess *p,size_t count,size_t visited)
{
    size_t words,need,work; LbAllocDomain *domain=p->entry_module->space->domain;
    if(!lb_size_mul(count,2,&words) || !lb_size_add(words,p->heap_reserved,&need) ||
       !lb_size_add(words,visited,&work)) { p->status=LB_PROCESS_LIMIT; return 0; }
    if(!lb_work_domain_charge(domain,work)) { p->status=LB_PROCESS_WORK_LIMIT; return 0; }
    p->last_native_cost+=work;
    if(!count) return 1;
    size_t before=p->collections;
    if(!lb_heap_reserve(p,need,0)) {
        LbAllocStatus cause=lb_alloc_domain_failure(domain);
        p->status=cause==LB_ALLOC_WORK_LIMIT?LB_PROCESS_WORK_LIMIT:
            (cause==LB_ALLOC_LIMIT || cause==LB_ALLOC_OK)?LB_PROCESS_LIMIT:LB_PROCESS_NO_MEMORY;
        return 0;
    }
    if(p->collections!=before) p->last_native_cost+=p->last_gc_cost;
    return 1;
}
/* beam_bif_load.c:loaded_0. No old-code or on_load versions exist here;
 * only fully published modules are visible. GC cannot unload namespace code. */
Eterm lb_bif_loaded_0(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)args; (void)pc; LbCodeSpace *space=p->entry_module->space;
    size_t count=0,visited=0; p->last_native_cost=1;
    if(!lb_work_domain_charge(space->domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    for(const LbCodeModule *m=space->modules;m;m=m->next) {
        if(!lb_work_domain_charge(space->domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
        ++p->last_native_cost;
        if(visited==SIZE_MAX) { p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE; }
        ++visited; if(m->published) ++count;
    }
    if(!module_list_heap(p,count,visited)) return THE_NON_VALUE;
    Eterm result=NIL; size_t written=0;
    for(const LbCodeModule *m=space->modules;m;m=m->next) if(m->published) {
        if(written==count) abort();
        result=CONS(p->htop,m->name,result); p->htop+=2; ++written;
    }
    if(written!=count) abort();
    return result;
}
/* preloaded.c installs exactly the pinned erlang wrapper image. This is the
 * boot inventory, not a claim about all OTP preloaded modules or current loads. */
Eterm lb_bif_pre_loaded_0(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)args; (void)pc; p->last_native_cost=0;
    if(!module_list_heap(p,1,1)) return THE_NON_VALUE;
    Eterm result=CONS(p->htop,am_erlang,NIL); p->htop+=2; return result;
}

/* module_loaded_1 in beam_bif_load.c queries current, fully published code,
 * not the export namespace. This runtime does not admit on_load staging. */
Eterm lb_bif_module_loaded_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    LbCodeSpace *space=p->entry_module->space; (void)pc; p->last_native_cost=0;
    if(!lb_work_domain_charge(space->domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    p->last_native_cost=1;
    if(!is_atom(args[0])) { p->freason=LB_EXC_BADARG; return THE_NON_VALUE; }
    for(const LbCodeModule *module=space->modules;module;module=module->next) {
        if(!lb_work_domain_charge(space->domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
        ++p->last_native_cost;
        if(module->name==args[0] && module->published) return am_true;
    }
    return am_false;
}
/* RFD 0005's sole loading compatibility exception: an existing published
 * module is acknowledged without examining or installing the supplied bytes.
 * This is deliberately not an adaptation of successful native code loading. */
Eterm lb_bif_load_module_2(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    static const LbCapabilityFailure denied={LB_CAPABILITY_CODE_LOADING,"erlang","load_module",2,
        "erlang:load_module/2 denied: CODE_LOADING"};
    LbCodeSpace *space=p->entry_module->space; LbBitstringView code; (void)pc;
    p->last_native_cost=0;
    if(args!=p->reg) { p->status=LB_PROCESS_INVALID; return THE_NON_VALUE; }
    if(!lb_work_domain_charge(space->domain,1)) goto work_failure;
    p->last_native_cost=1;
    if(!is_atom(args[0]) || !lb_bitstring_view(args[1],&code) || (code.bit_size&7)) {
        p->freason=LB_EXC_BADARG; return THE_NON_VALUE;
    }
    int published=0;
    for(const LbCodeModule *module=space->modules;module;module=module->next) {
        if(!lb_work_domain_charge(space->domain,1)) goto work_failure;
        ++p->last_native_cost;
        if(module->name==args[0] && module->published) { published=1; break; }
    }
    if(!published) {
        /* Entry and lookup are already charged. Do not allocate, parse BEAM,
         * or route this infrastructure outcome through guest exceptions. */
        p->capability_failure=&denied; p->status=LB_PROCESS_CAPABILITY_DENIED;
        return THE_NON_VALUE;
    }
    size_t need,collections=p->collections;
    if(!lb_size_add(3,p->heap_reserved,&need)) { p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE; }
    if(!lb_work_domain_charge(space->domain,3)) goto work_failure;
    p->last_native_cost+=3;
    if(!lb_heap_reserve(p,need,2)) {
        LbAllocStatus cause=lb_alloc_domain_failure(space->domain);
        p->status=cause==LB_ALLOC_WORK_LIMIT?LB_PROCESS_WORK_LIMIT:
            (cause==LB_ALLOC_LIMIT || cause==LB_ALLOC_OK)?LB_PROCESS_LIMIT:LB_PROCESS_NO_MEMORY;
        return THE_NON_VALUE;
    }
    if(p->collections!=collections) p->last_native_cost+=p->last_gc_cost;
    Eterm result=TUPLE2(p->htop,am_module,p->reg[0]); p->htop+=3;
    return result;
work_failure:
    p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE;
}
static LbCodeModule *find_module(LbProcess *p,Eterm name)
{
    LbCodeModule *m;
    if(!is_atom(name)) return NULL;
    for(m=p->entry_module->space->modules;m;m=m->next) {
        if(!lb_work_domain_charge(m->space->domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return NULL; }
        if(m->name==name) return m;
    }
    return NULL;
}
static int info_size(LbProcess *p,LbCodeModule *m,Eterm what,size_t *size)
{
    LbAllocDomain *d=m->space->domain;
    if(what==am_module || what==am_native || what==am_nifs || what==am_native_addresses) { *size=0; return 1; }
    if(what==am_exports) { *size=m->export_count*5; return 1; }
    if(what==am_functions) { *size=lb_code_module_function_count(m)*5; return 1; }
    if(what==am_md5) { *size=4; return 1; }
    if(what==am_attributes || what==am_compile) {
        if(lb_flat_size(d,what==am_attributes?m->attributes:m->compile,size)) return 1;
        p->status=LB_PROCESS_NO_MEMORY;
    }
    return 0;
}
static Eterm build_info(LbProcess *process,LbCodeModule *m,Eterm what,Eterm **hp)
{
    Eterm result=NIL; size_t i,count;
    if(what==am_module) return m->name;
    if(what==am_native) return am_false;
    if(what==am_nifs || what==am_native_addresses) return NIL;
    if(what==am_attributes) return lb_copy_flat(m->attributes,hp,&process->off_heap);
    if(what==am_compile) return lb_copy_flat(m->compile,hp,&process->off_heap);
    if(what==am_md5) {
        Eterm *p=*hp;
        p[0]=_make_header(3,HEAP_BITS_SUBTAG); p[1]=128; memcpy(p+2,m->md5,16);
        *hp+=4; return make_boxed(p);
    }
    count=what==am_exports?m->export_count:lb_code_module_function_count(m);
    for(i=0;i<count;++i) {
        LbMFA mfa; Eterm tuple;
        if(what==am_exports) mfa=m->exports[i].info.mfa;
        else memcpy(&mfa,(const BeamInstr *)m->words[i]+2,sizeof(mfa));
        tuple=TUPLE2(*hp,mfa.function,make_small(mfa.arity)); *hp+=3;
        result=CONS(*hp,tuple,result); *hp+=2;
    }
    return result;
}
Eterm lb_bif_module_info_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    const Eterm keys[]={am_md5,am_compile,am_attributes,am_exports,am_module};
    LbCodeModule *m=find_module(p,args[0]);
    size_t size=25,i,n;
    Eterm result=NIL,*hp;
    LbBinRef *checkpoint;
    (void)pc;
    if(!m) { p->freason=LB_EXC_BADARG; return THE_NON_VALUE; }
    for(i=0;i<5;++i) {
        if(!info_size(p,m,keys[i],&n) || !lb_size_add(size,n,&size)) { p->status=LB_PROCESS_NO_MEMORY; return THE_NON_VALUE; }
    }
    if(!lb_work_domain_charge(m->space->domain,size+lb_work_bulk_units(size))) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    if(!lb_heap_reserve(p,size,1)) { p->status=LB_PROCESS_NO_MEMORY; return THE_NON_VALUE; }
    hp=p->htop; checkpoint=p->off_heap.first;
    for(i=0;i<5;++i) {
        Eterm value=build_info(p,m,keys[i],&hp),tuple;
        if(is_non_value(value)) {
            lb_offheap_rollback(&p->off_heap,checkpoint);
            p->status=LB_PROCESS_NO_MEMORY; return THE_NON_VALUE;
        }
        tuple=TUPLE2(hp,keys[i],value); hp+=3;
        result=CONS(hp,tuple,result); hp+=2;
    }
    ASSERT((size_t)(hp-p->htop)==size); p->htop=hp; return result;
}
Eterm lb_bif_module_info_2(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    LbCodeModule *m=find_module(p,args[0]);
    Eterm what=args[1],result,*hp;
    size_t size;
    (void)pc;
    if(!m || !info_size(p,m,what,&size)) { p->freason=LB_EXC_BADARG; return THE_NON_VALUE; }
    if(!lb_work_domain_charge(m->space->domain,size+lb_work_bulk_units(size))) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    if(!lb_heap_reserve(p,size,2)) { p->status=LB_PROCESS_NO_MEMORY; return THE_NON_VALUE; }
    hp=p->htop; result=build_info(p,m,what,&hp);
    if(is_non_value(result)) { p->status=LB_PROCESS_NO_MEMORY; return THE_NON_VALUE; }
    ASSERT((size_t)(hp-p->htop)==size); p->htop=hp; return result;
}
