/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1996-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Adapted from erl_bif_info.c get_module_info/module_info_0/1. Owned metadata,
 * fallible preallocation and explicit process context replace ERTS factories.
 */
#include "process_internal.h"
#include "reference.h"
#include "map_runtime.h"

Eterm lb_bif_get_cookie_0(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)args; (void)pc; p->last_native_cost=1;
    if(!lb_work_domain_charge(p->entry_module->space->domain,1)) {
        p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE;
    }
    /* Unnamed code spaces have no distributed cookie configured. */
    return am_nocookie;
}

Eterm lb_bif_is_alive_0(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)args; (void)pc; p->last_native_cost=1;
    if(!lb_work_domain_charge(p->entry_module->space->domain,1)) {
        p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE;
    }
    /* Code spaces have no distributed node name or dynamic-node service. */
    return am_false;
}

/* These are properties of the owned runtime, not of the OTP installation
 * used to compile fixtures. There is one configured Engine worker, no dirty
 * schedulers and no asynchronous driver pool. Unimplemented selectors remain
 * infrastructure refusals; do not synthesize ERTS version/allocator metadata. */
Eterm lb_bif_is_builtin_3(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)pc; p->last_native_cost=0; LbAllocDomain *domain=p->entry_module->space->domain;
    if(!lb_work_domain_charge(domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    ++p->last_native_cost;
    if(!is_atom(args[0]) || !is_atom(args[1]) || !is_small(args[2])) {
        p->freason=LB_EXC_BADARG; return THE_NON_VALUE;
    }
    Sint arity=signed_val(args[2]);
    if(arity<0 || arity>MAX_ARG) return am_false;
    LbEngine *engine=p->entry_module->space->engine;
    for(size_t i=0;i<LB_NATIVE_COUNT;++i) {
        if(!lb_work_domain_charge(domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
        ++p->last_native_cost;
        const BifEntry *entry=&engine->bifs[engine->native_ids[i]];
        if(entry->f && entry->module==args[0] && entry->name==args[1] && entry->arity==arity) return am_true;
    }
    return am_false;
}

Eterm lb_bif_processes_0(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)args; (void)pc; LbCodeSpace *space=p->entry_module->space;
    LbAllocDomain *domain=space->domain; p->last_native_cost=0;
    if(!lb_work_domain_charge(domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    ++p->last_native_cost;
    LbPidTable *table=space->pids;
    size_t capacity=table?lb_pid_capacity(table):1024;
    size_t count=lb_pid_count(table)+(p->identity.owner?0:1),words,need,work;
    if(count>capacity || !lb_size_mul(count,2,&words) ||
       !lb_size_add(capacity,words,&work) || work>UINT_MAX ||
       !lb_size_add(words,p->heap_reserved,&need)) { p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE; }
    if(!lb_work_domain_charge(domain,(Uint)work)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    p->last_native_cost+=work; size_t collections=p->collections;
    if(!lb_heap_reserve(p,need,MAX_REG)) {
        LbAllocStatus cause=lb_alloc_domain_failure(domain);
        p->status=cause==LB_ALLOC_WORK_LIMIT?LB_PROCESS_WORK_LIMIT:
            cause==LB_ALLOC_NO_MEMORY?LB_PROCESS_NO_MEMORY:LB_PROCESS_LIMIT;
        return THE_NON_VALUE;
    }
    if(p->collections!=collections) p->last_native_cost+=p->last_gc_cost;
    Uint prepared_cost=p->last_native_cost; Eterm identity;
    LbProcessStatus status=lb_process_identity(p,&identity);
    Uint identity_cost=p->last_native_cost; p->last_native_cost=prepared_cost+identity_cost;
    if(status!=LB_PROCESS_READY) {
        LbAllocStatus cause=lb_alloc_domain_failure(domain);
        p->status=cause==LB_ALLOC_WORK_LIMIT?LB_PROCESS_WORK_LIMIT:
            cause==LB_ALLOC_LIMIT?LB_PROCESS_LIMIT:
            cause==LB_ALLOC_NO_MEMORY?LB_PROCESS_NO_MEMORY:status;
        return THE_NON_VALUE;
    }
    if(lb_pid_count(space->pids)!=count) { p->status=LB_PROCESS_INVALID; return THE_NON_VALUE; }
    Eterm *hp=p->htop,result;
    if(lb_pid_make_list(space->pids,&hp,count,&result)!=LB_PID_OK) { p->status=LB_PROCESS_INVALID; return THE_NON_VALUE; }
    p->htop=hp; return result;
}

Eterm lb_bif_processes_iterator_0(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)args; (void)pc; LbAllocDomain *domain=p->entry_module->space->domain; p->last_native_cost=1;
    if(!lb_work_domain_charge(domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    size_t need,collections=p->collections;
    if(!lb_size_add(3,p->heap_reserved,&need) || !lb_work_domain_charge(domain,3)) {
        p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE;
    }
    p->last_native_cost+=3;
    if(!lb_heap_reserve(p,need,MAX_REG)) {
        LbAllocStatus cause=lb_alloc_domain_failure(domain);
        p->status=cause==LB_ALLOC_WORK_LIMIT?LB_PROCESS_WORK_LIMIT:
            cause==LB_ALLOC_LIMIT?LB_PROCESS_LIMIT:LB_PROCESS_NO_MEMORY;
        return THE_NON_VALUE;
    }
    if(p->collections!=collections) p->last_native_cost+=p->last_gc_cost;
    Eterm result=TUPLE2(p->htop,make_small(0),NIL); p->htop+=3; return result;
}
Eterm lb_bif_processes_next_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)pc; LbAllocDomain *domain=p->entry_module->space->domain; p->last_native_cost=1;
    if(!lb_work_domain_charge(domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    Uint call_cost=p->last_native_cost; Eterm iterator=args[0];
    if(!is_tuple(iterator) || arityval(*tuple_val(iterator))!=2) { p->freason=LB_EXC_BADARG; return THE_NON_VALUE; }
    Eterm index=tuple_val(iterator)[1],tail=tuple_val(iterator)[2],head=NIL; int first_snapshot=0;
    if(is_list(tail)) { head=CAR(list_val(tail)); tail=CDR(list_val(tail)); }
    else if(is_nil(tail)) {
        if(!is_small(index) || signed_val(index)<0) { p->freason=LB_EXC_BADARG; return THE_NON_VALUE; }
        if(signed_val(index)>0) return am_none;
        Eterm snapshot=lb_bif_processes_0(p,NULL,NULL);
        if(is_non_value(snapshot)) return snapshot;
        p->last_native_cost+=call_cost;
        if(is_nil(snapshot)) return am_none;
        if(p->construction_root!=NIL) abort();
        p->construction_root=snapshot; head=CAR(list_val(snapshot)); tail=CDR(list_val(snapshot));
        index=make_small(1); first_snapshot=1;
    } else { p->freason=LB_EXC_BADARG; return THE_NON_VALUE; }
    size_t need,collections=p->collections;
    if(!lb_size_add(6,p->heap_reserved,&need)) {
        if(first_snapshot) p->construction_root=NIL;
        p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE;
    }
    if(!lb_work_domain_charge(domain,6)) {
        if(first_snapshot) p->construction_root=NIL;
        p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE;
    }
    p->last_native_cost+=6;
    if(!lb_heap_reserve(p,need,MAX_REG)) {
        if(first_snapshot) p->construction_root=NIL;
        LbAllocStatus cause=lb_alloc_domain_failure(domain);
        p->status=cause==LB_ALLOC_WORK_LIMIT?LB_PROCESS_WORK_LIMIT:
            cause==LB_ALLOC_LIMIT?LB_PROCESS_LIMIT:LB_PROCESS_NO_MEMORY;
        return THE_NON_VALUE;
    }
    if(p->collections!=collections) p->last_native_cost+=p->last_gc_cost;
    if(first_snapshot) {
        Eterm snapshot=p->construction_root; p->construction_root=NIL;
        head=CAR(list_val(snapshot)); tail=CDR(list_val(snapshot));
    } else {
        iterator=args[0]; index=tuple_val(iterator)[1]; Eterm rest=tuple_val(iterator)[2];
        head=CAR(list_val(rest)); tail=CDR(list_val(rest));
    }
    Eterm next=TUPLE2(p->htop,index,tail); p->htop+=3;
    Eterm result=TUPLE2(p->htop,head,next); p->htop+=3; return result;
}

static Eterm statistics_integer(Eterm *heap,uint64_t value,size_t *words)
{
    if(value<=(uint64_t)MAX_SMALL) { *words=0; return make_small((Sint)value); }
    *words=2; heap[0]=_make_header(1,POS_BIG_SUBTAG); heap[1]=(Uint)value;
    return make_boxed(heap);
}
Eterm lb_bif_bump_reductions_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)pc; p->last_native_cost=0;
    if(!is_small(args[0]) || signed_val(args[0])<0) { p->freason=LB_EXC_BADARG; return THE_NON_VALUE; }
    if(!lb_work_domain_charge(p->entry_module->space->domain,1)) {
        p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE;
    }
    Sint requested=signed_val(args[0]);
    p->last_native_cost=requested>4000?4000u:(Uint)requested;
    return am_true;
}
Eterm lb_bif_statistics_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)pc; LbAllocDomain *domain=p->entry_module->space->domain; p->last_native_cost=0;
    if(!lb_work_domain_charge(domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    p->last_native_cost=1;
    if(args[0]!=am_reductions) { p->status=LB_PROCESS_REFUSED; return THE_NON_VALUE; }
    uint64_t total=p->reductions_total;
    uint64_t since=total>=p->reductions_last_query?total-p->reductions_last_query:0;
    size_t total_words=total>(uint64_t)MAX_SMALL?2:0;
    size_t since_words=since>(uint64_t)MAX_SMALL?2:0;
    size_t words,need,collections=p->collections;
    if(!lb_size_add(3,total_words,&words) || !lb_size_add(words,since_words,&words) ||
       !lb_size_add(words,p->heap_reserved,&need)) { p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE; }
    Uint work=(Uint)(words+1);
    if(!lb_work_domain_charge(domain,work)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    p->last_native_cost+=work;
    if(!lb_heap_reserve(p,need,MAX_REG)) {
        LbAllocStatus cause=lb_alloc_domain_failure(domain);
        p->status=cause==LB_ALLOC_WORK_LIMIT?LB_PROCESS_WORK_LIMIT:
            cause==LB_ALLOC_LIMIT?LB_PROCESS_LIMIT:LB_PROCESS_NO_MEMORY;
        return THE_NON_VALUE;
    }
    if(p->collections!=collections) p->last_native_cost+=p->last_gc_cost;
    Eterm *hp=p->htop; size_t used; Eterm total_term=statistics_integer(hp,total,&used); hp+=used;
    Eterm since_term=statistics_integer(hp,since,&used); hp+=used;
    Eterm result=TUPLE2(hp,total_term,since_term); hp+=3;
    p->htop=hp; p->reductions_last_query=total; return result;
}
static void memory_value(Eterm *heap,size_t bytes,Eterm *value,size_t *words)
{
    if(bytes<=(size_t)MAX_SMALL) { *value=make_small((Sint)bytes); *words=0; return; }
    *value=make_boxed(heap); *words=2;
    heap[0]=_make_header(1,POS_BIG_SUBTAG); heap[1]=(Uint)bytes;
}
Eterm lb_bif_memory_0(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)args; (void)pc; LbAllocDomain *domain=p->entry_module->space->domain;
    LbAllocUsage usage=lb_alloc_domain_usage(domain); size_t value_words=usage.current>(size_t)MAX_SMALL?2:0;
    size_t words,need,work,collections=p->collections;
    p->last_native_cost=0;
    if(!lb_size_add(5,value_words,&words) || !lb_size_add(words,p->heap_reserved,&need) ||
       !lb_size_add(words,1,&work)) { p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE; }
    if(!lb_work_domain_charge(domain,(Uint)work)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    p->last_native_cost=work;
    if(!lb_heap_reserve(p,need,MAX_REG)) {
        LbAllocStatus cause=lb_alloc_domain_failure(domain);
        p->status=cause==LB_ALLOC_WORK_LIMIT?LB_PROCESS_WORK_LIMIT:
            cause==LB_ALLOC_LIMIT?LB_PROCESS_LIMIT:LB_PROCESS_NO_MEMORY;
        return THE_NON_VALUE;
    }
    if(p->collections!=collections) p->last_native_cost+=p->last_gc_cost;
    Eterm value; size_t used; memory_value(p->htop,usage.current,&value,&used); p->htop+=used;
    Eterm pair=TUPLE2(p->htop,am_total,value); p->htop+=3;
    Eterm result=CONS(p->htop,pair,NIL); p->htop+=2; return result;
}
Eterm lb_bif_memory_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)pc; LbAllocDomain *domain=p->entry_module->space->domain; p->last_native_cost=1;
    if(!lb_work_domain_charge(domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    if(args[0]!=am_total) { p->status=LB_PROCESS_REFUSED; return THE_NON_VALUE; }
    LbAllocUsage usage=lb_alloc_domain_usage(domain);
    if(usage.current<=(size_t)MAX_SMALL) return make_small((Sint)usage.current);
    size_t need,collections=p->collections;
    if(!lb_size_add(2,p->heap_reserved,&need) || !lb_work_domain_charge(domain,2)) {
        p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE;
    }
    p->last_native_cost+=2;
    if(!lb_heap_reserve(p,need,MAX_REG)) {
        LbAllocStatus cause=lb_alloc_domain_failure(domain);
        p->status=cause==LB_ALLOC_WORK_LIMIT?LB_PROCESS_WORK_LIMIT:
            cause==LB_ALLOC_LIMIT?LB_PROCESS_LIMIT:LB_PROCESS_NO_MEMORY;
        return THE_NON_VALUE;
    }
    if(p->collections!=collections) p->last_native_cost+=p->last_gc_cost;
    Eterm value; size_t words; memory_value(p->htop,usage.current,&value,&words); p->htop+=words; return value;
}

Eterm lb_bif_system_info_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)pc; p->last_native_cost=1;
    if(!lb_work_domain_charge(p->entry_module->space->domain,1)) {
        p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE;
    }
    Eterm selector=args[0];
    if(is_tuple(selector)) {
        Eterm *tuple=tuple_val(selector); Uint arity=arityval(tuple[0]);
        if(!arity || !is_atom(tuple[1])) goto badarg;
        if(tuple[1]!=am_wordsize) goto refused;
        if(arity!=2) goto badarg;
        if(tuple[2]==am_internal) return make_small(sizeof(Eterm));
        if(tuple[2]==am_external) return make_small(sizeof(uintptr_t));
        goto badarg;
    }
    if(!is_atom(selector)) goto badarg;
    if(selector==am_wordsize) return make_small(sizeof(Eterm));
    if(selector==am_threads) return am_true;
    static const struct { const char *name; size_t size; Sint count; } counts[]={
        {"schedulers",10,1},{"schedulers_online",17,1},{"schedulers_active",17,1},
        {"dirty_cpu_schedulers",20,0},{"dirty_cpu_schedulers_online",27,0},
        {"dirty_io_schedulers",19,0},{"thread_pool_size",16,0}};
    LbBeamBytes name;
    if(lb_atoms_name(p->entry_module->space->atoms,selector,&name)!=LB_ATOM_OK) goto badarg;
    for(size_t i=0;i<sizeof(counts)/sizeof(*counts);++i)
        if(name.size==counts[i].size && !memcmp(name.data,counts[i].name,name.size)) return make_small(counts[i].count);
refused:
    p->status=LB_PROCESS_REFUSED; return THE_NON_VALUE;
badarg:
    p->freason=LB_EXC_BADARG; return THE_NON_VALUE;
}

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
/* dist.c:node_1 queries identity, not liveness. No PID lookup or identity
 * allocation is needed, including for retired local processes. External node
 * containers remain refused until their owned representation is supported. */
Eterm lb_bif_node_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    Eterm name=lb_bif_node_0(p,NULL,pc),term=args[0];
    if(is_non_value(name)) return name;
    if(is_internal_pid(term) || (term&_TAG_IMMED1_MASK)==_TAG_IMMED1_PORT || lb_reference(term)) return name;
    if(is_boxed(term)) {
        Eterm tag=*boxed_val(term)&_TAG_HEADER_MASK;
        if(tag==EXTERNAL_PID_SUBTAG || tag==EXTERNAL_PORT_SUBTAG || tag==EXTERNAL_REF_SUBTAG) {
            p->status=LB_PROCESS_REFUSED; return THE_NON_VALUE;
        }
    }
    p->freason=LB_EXC_BADARG; return THE_NON_VALUE;
}
/* dist.c:nodes_0 selects visible connected peers. Owned namespaces have no
 * distribution connections, and cannot create them through a guest binding. */
static int nodes_selector(Eterm selector,int *include_self)
{
    if(selector==am_this) { *include_self=1; return 1; }
    return selector==am_visible || selector==am_hidden || selector==am_connected || selector==am_known;
}
Eterm lb_bif_nodes_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)pc; LbAllocDomain *domain=p->entry_module->space->domain;
    p->last_native_cost=0;
    if(!lb_work_domain_charge(domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    ++p->last_native_cost; int include_self=0; Eterm selector=args[0];
    if(selector!=NIL && is_atom(selector)) {
        if(!nodes_selector(selector,&include_self)) goto badarg;
    } else if(selector!=NIL && is_list(selector)) {
        while(is_list(selector)) {
            if(!lb_work_domain_charge(domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
            ++p->last_native_cost; Eterm *cell=list_val(selector);
            if(!is_atom(cell[0]) || !nodes_selector(cell[0],&include_self)) goto badarg;
            selector=cell[1];
        }
        if(selector!=NIL) goto badarg;
    } else if(selector!=NIL) goto badarg;
    if(!include_self) return NIL;
    size_t need;
    if(!lb_size_add(2,p->heap_reserved,&need)) { p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE; }
    size_t collections=p->collections;
    if(!lb_heap_reserve(p,need,MAX_REG)) {
        LbAllocStatus cause=lb_alloc_domain_failure(domain);
        p->status=cause==LB_ALLOC_WORK_LIMIT?LB_PROCESS_WORK_LIMIT:
            (cause==LB_ALLOC_LIMIT || cause==LB_ALLOC_OK)?LB_PROCESS_LIMIT:LB_PROCESS_NO_MEMORY;
        return THE_NON_VALUE;
    }
    if(p->collections!=collections) p->last_native_cost+=p->last_gc_cost;
    p->htop[0]=p->entry_module->space->node_name; p->htop[1]=NIL;
    Eterm result=make_list(p->htop); p->htop+=2; return result;
badarg:
    p->freason=LB_EXC_BADARG; return THE_NON_VALUE;
}
Eterm lb_bif_nodes_2(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)pc; LbAllocDomain *domain=p->entry_module->space->domain; p->last_native_cost=1;
    if(!lb_work_domain_charge(domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    int include_self=0,connection_id=0,node_type=0; Eterm selector=args[0];
    if(selector!=NIL && is_atom(selector)) {
        if(!nodes_selector(selector,&include_self)) goto badarg;
    } else if(selector!=NIL && is_list(selector)) {
        while(is_list(selector)) {
            if(!lb_work_domain_charge(domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
            ++p->last_native_cost; Eterm *cell=list_val(selector);
            if(!is_atom(cell[0]) || !nodes_selector(cell[0],&include_self)) goto badarg;
            selector=cell[1];
        }
        if(selector!=NIL) goto badarg;
    } else if(selector!=NIL) goto badarg;
    int is_map=0; LbMapStatus map_status=lb_map_is_map(args[1],&is_map);
    if(map_status!=LB_MAP_OK) { p->status=LB_PROCESS_REFUSED; return THE_NON_VALUE; }
    if(!is_map) goto badarg;
    Uint map_size=boxed_val(args[1])[1];
    if(map_size>2) goto badarg;
    Eterm value; size_t present=0;
    map_status=lb_map_find(p,args[1],am_connection_id,&value);
    if(map_status==LB_MAP_OK) {
        if(value!=am_true && value!=am_false) goto badarg;
        connection_id=value==am_true; ++present;
    } else if(map_status!=LB_MAP_MISSING) goto map_failure;
    map_status=lb_map_find(p,args[1],am_node_type,&value);
    if(map_status==LB_MAP_OK) {
        if(value!=am_true && value!=am_false) goto badarg;
        node_type=value==am_true; ++present;
    } else if(map_status!=LB_MAP_MISSING) goto map_failure;
    if(present!=map_size) goto badarg;
    if(!include_self) return NIL;
    Eterm keys[2],values[2]; size_t count=0;
    if(connection_id) { keys[count]=am_connection_id; values[count++]=am_undefined; }
    if(node_type) { keys[count]=am_node_type; values[count++]=am_this; }
    size_t reserve_words=count?9+2*count:10;
    map_status=lb_map_reserve(p,reserve_words,MAX_REG);
    if(map_status!=LB_MAP_OK) goto map_failure;
    if(!lb_work_domain_charge(domain,5)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    p->last_native_cost+=5;
    Eterm info; map_status=lb_map_create(p,keys,values,count,&info);
    if(map_status!=LB_MAP_OK) goto map_failure;
    Eterm *pair=p->htop; pair[0]=make_arityval(2); pair[1]=p->entry_module->space->node_name; pair[2]=info;
    Eterm node_info=make_tuple(pair); p->htop+=3;
    Eterm *list=p->htop; list[0]=node_info; list[1]=NIL;
    Eterm result=make_list(list); p->htop+=2; return result;
map_failure:
    if(map_status==LB_MAP_WORK_LIMIT) p->status=LB_PROCESS_WORK_LIMIT;
    else if(map_status==LB_MAP_NO_MEMORY) p->status=LB_PROCESS_NO_MEMORY;
    else if(map_status==LB_MAP_LIMIT) p->status=LB_PROCESS_LIMIT;
    else p->status=LB_PROCESS_REFUSED;
    return THE_NON_VALUE;
badarg:
    p->freason=LB_EXC_BADARG; return THE_NON_VALUE;
}
Eterm lb_bif_nodes_0(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)args; (void)pc; p->last_native_cost=1;
    if(!lb_work_domain_charge(p->entry_module->space->domain,1)) {
        p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE;
    }
    return NIL;
}
Eterm lb_bif_ports_0(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)args; (void)pc; p->last_native_cost=1;
    if(!lb_work_domain_charge(p->entry_module->space->domain,1)) {
        p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE;
    }
    /* This code space owns no host ports; the only truthful local result is
     * the empty port table, not a projection of the host VM's ports. */
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
        if(entry->info.mfa.module==args[0] && entry->info.mfa.function==args[1] && entry->info.mfa.arity==arity &&
           (!entry->owner || lb_code_module_ready(entry->owner))) return am_true;
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
        ++visited; if(m->published && lb_code_module_ready(m)) ++count;
    }
    if(!module_list_heap(p,count,visited)) return THE_NON_VALUE;
    Eterm result=NIL; size_t written=0;
    for(const LbCodeModule *m=space->modules;m;m=m->next) if(m->published && lb_code_module_ready(m)) {
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
        if(module->name==args[0] && module->published && lb_code_module_ready(module)) return am_true;
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
        if(module->name==args[0] && module->published && lb_code_module_ready(module)) { published=1; break; }
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
        if(m->name==name && lb_code_module_ready(m)) return m;
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
