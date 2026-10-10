/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1996-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Adapted from erl_bif_info.c get_module_info/module_info_0/1. Owned metadata,
 * fallible preallocation and explicit process context replace ERTS factories.
 */
#include "process_internal.h"
static LbCodeModule *find_module(LbProcess *p,Eterm name)
{
    LbCodeModule *m;
    if(!is_atom(name)) return NULL;
    for(m=p->entry_module->space->modules;m;m=m->next) if(m->name==name) return m;
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
    if(!m) { p->freason=am_badarg; return THE_NON_VALUE; }
    for(i=0;i<5;++i) {
        if(!info_size(p,m,keys[i],&n) || !lb_size_add(size,n,&size)) { p->status=LB_PROCESS_NO_MEMORY; return THE_NON_VALUE; }
    }
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
    if(!m || !info_size(p,m,what,&size)) { p->freason=am_badarg; return THE_NON_VALUE; }
    if(!lb_heap_reserve(p,size,2)) { p->status=LB_PROCESS_NO_MEMORY; return THE_NON_VALUE; }
    hp=p->htop; result=build_info(p,m,what,&hp);
    if(is_non_value(result)) { p->status=LB_PROCESS_NO_MEMORY; return THE_NON_VALUE; }
    ASSERT((size_t)(hp-p->htop)==size); p->htop=hp; return result;
}
