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
