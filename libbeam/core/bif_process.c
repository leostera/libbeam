/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1996-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#include "process_internal.h"
#include "reference.h"
#include "flatmap.h"
#include "world_internal.h"
/* Identity is allocated on first observation, with no moving-heap allocation.
 * The directory is owned by the world/code namespace and keeps its generations
 * after the last process exits. Spawn must obtain identity before publication;
 * no spawn, signal or mailbox capability follows from self/0 alone. */
LbProcessStatus lb_process_identity(LbProcess *p,Eterm *out)
{
    if(!p || !out || (p->status!=LB_PROCESS_READY && p->status!=LB_PROCESS_YIELDED)) return LB_PROCESS_INVALID;
    p->last_native_cost=1;
    if(p->identity.owner) { *out=p->identity.id; return LB_PROCESS_READY; }
    LbCodeSpace *space=p->entry_module->space;
    LbPidTable *table=space->pids;
    if(!table) {
        LbPidStatus status=lb_pid_table_create(space,1024,&table);
        if(status!=LB_PID_OK) return status==LB_PID_NO_MEMORY?LB_PROCESS_NO_MEMORY:LB_PROCESS_INVALID;
        p->last_native_cost+=1024/16;
    }
    LbPidStatus status=lb_pid_insert(table,&p->identity,p);
    if(status!=LB_PID_OK) {
        if(!space->pids && lb_pid_table_destroy(table)!=LB_PID_OK) abort();
        return status==LB_PID_FULL?LB_PROCESS_LIMIT:LB_PROCESS_INVALID;
    }
    space->pids=table; *out=p->identity.id; return LB_PROCESS_READY;
}
Eterm lb_bif_self_0(LbProcess *p,Eterm *args,const BeamInstr *instruction)
{
    (void)args; (void)instruction;
    Eterm result;
    LbProcessStatus status=lb_process_identity(p,&result);
    if(status==LB_PROCESS_READY) return result;
    p->status=status; return THE_NON_VALUE;
}
/* Native spawn_3/erl_create_process boundary: atoms and a proper argument
 * list are required. This executable slice owns the erlang:apply(F,[]) startup
 * for local fun/0 only; other valid MFAs/apply shapes refuse infrastructure
 * admission rather than pretending to be badarg or silently changing spawn/1
 * (which remains an Erlang wrapper, not a native BIF identity). */
Eterm lb_bif_spawn_3(LbProcess *p,Eterm *args,const BeamInstr *instruction)
{
    (void)instruction;
    p->last_native_cost=1;
    if(!is_atom(args[0]) || !is_atom(args[1])) goto badarg;
    Eterm tail=args[2]; size_t length=0;
    while(is_list(tail)) {
        if(!lb_work_domain_charge(p->entry_module->space->domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
        if(++length>LB_MAX_BINARY_BYTES/sizeof(Eterm)) { p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE; }
        ++p->last_native_cost; tail=list_val(tail)[1];
    }
    if(!is_nil(tail)) goto badarg;
    if(args[0]!=am_erlang || args[1]!=am_apply || length!=2) goto refused;
    Eterm *first=list_val(args[2]),*second=list_val(first[1]);
    const LbFun *fun=lb_fun(first[0]);
    if(!is_nil(second[0]) || !fun || lb_fun_arity(fun)!=0 || !p->world || !p->running) goto refused;
    Eterm pid; Uint validation_cost=p->last_native_cost;
    p->last_native_cost=1; /* task preparation plus any child-copy work */
    LbProcessStatus status=lb_task_spawn_fun_locked(p->world,p,first[0],&pid);
    p->last_native_cost+=validation_cost;
    if(status==LB_PROCESS_READY) return pid;
    p->status=status==LB_PROCESS_INVALID?LB_PROCESS_REFUSED:status; return THE_NON_VALUE;
badarg:
    p->freason=LB_EXC_BADARG; return THE_NON_VALUE;
refused:
    p->status=LB_PROCESS_REFUSED; return THE_NON_VALUE;
}
Eterm lb_bif_spawn_opt_4(LbProcess *p,Eterm *args,const BeamInstr *instruction)
{
    (void)instruction; p->last_native_cost=1;
    if(!is_atom(args[0]) || !is_atom(args[1])) goto badarg;
    Eterm tail=args[2]; size_t length=0;
    while(is_list(tail)) {
        if(!lb_work_domain_charge(p->entry_module->space->domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
        if(++length>LB_MAX_BINARY_BYTES/sizeof(Eterm)) goto limit;
        tail=list_val(tail)[1];
    }
    if(!is_nil(tail)) goto badarg;
    Eterm options=args[3]; int monitored=0;
    while(is_list(options)) {
        if(!lb_work_domain_charge(p->entry_module->space->domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
        if(++p->last_native_cost>LB_MAX_BINARY_BYTES/sizeof(Eterm)) goto limit;
        Eterm option=list_val(options)[0]; options=list_val(options)[1];
        if(option!=am_monitor) goto refused;
        monitored=1;
    }
    if(!is_nil(options)) goto badarg;
    if(!monitored) goto refused; /* positive spawn_opt/4 result is always {Pid,Ref} */
    if(args[0]!=am_erlang || args[1]!=am_apply || length!=2 || !p->world || !p->running) goto refused;
    Eterm *first=list_val(args[2]),*second=list_val(first[1]);
    const LbFun *fun=lb_fun(first[0]);
    if(!is_nil(second[0]) || !fun || lb_fun_arity(fun)!=0) goto refused;
    LbEngine *engine=p->entry_module->space->engine;
    if(engine->reference_counter==UINT64_MAX) goto limit;
    size_t need,before=p->collections;
    if(!lb_size_add(LB_REF_WORDS+3,p->heap_reserved,&need) || !lb_heap_reserve(p,need,4)) goto memory;
    if(p->collections!=before) p->last_native_cost+=p->last_gc_cost;
    Eterm parent_pid;
    LbProcessStatus status=lb_process_identity(p,&parent_pid);
    if(status!=LB_PROCESS_READY) { p->status=status; return THE_NON_VALUE; }
    Eterm ref=lb_reference_write(p->htop,engine->reference_counter); p->htop+=LB_REF_WORDS;
    LbMonitor *monitor; status=lb_monitor_prepare(p,ref,&monitor);
    if(status!=LB_PROCESS_READY) { p->status=status; return THE_NON_VALUE; }
    /* Reserve/GC preceded borrowing the reloaded closure and preparing halves.
     * The task attaches both halves before publishing the child's runnable link. */
    Eterm pid;
    status=lb_task_spawn_fun_monitored_locked(p->world,p,list_val(args[2])[0],monitor,&pid);
    if(status!=LB_PROCESS_READY) { lb_monitor_discard(monitor); p->status=status; return THE_NON_VALUE; }
    ++engine->reference_counter; /* no fallible work remains after runnable publication */
    Eterm result=TUPLE2(p->htop,pid,ref); p->htop+=3; return result;
badarg: p->freason=LB_EXC_BADARG; return THE_NON_VALUE;
refused: p->status=LB_PROCESS_REFUSED; return THE_NON_VALUE;
limit: p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE;
memory: p->status=LB_PROCESS_NO_MEMORY; return THE_NON_VALUE;
}
Eterm lb_bif_demonitor_2(LbProcess *p,Eterm *args,const BeamInstr *instruction)
{
    (void)instruction; p->last_native_cost=1;
    if(!is_boxed(args[0]) || boxed_val(args[0])[0]!=LB_REF_HEADER) goto badarg;
    Eterm options=args[1]; int flush=0;
    while(is_list(options)) {
        if(!lb_work_domain_charge(p->entry_module->space->domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
        if(++p->last_native_cost>LB_MAX_BINARY_BYTES/sizeof(Eterm)) { p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE; }
        Eterm option=list_val(options)[0]; options=list_val(options)[1];
        if(option==am_flush) flush=1;
        else if(option==am_info) { p->status=LB_PROCESS_REFUSED; return THE_NON_VALUE; }
        else goto badarg;
    }
    if(!is_nil(options)) goto badarg;
    lb_monitor_demonitor(p,args[0],flush); return am_true;
badarg: p->freason=LB_EXC_BADARG; return THE_NON_VALUE;
}
Eterm lb_bif_exit_2(LbProcess *p,Eterm *args,const BeamInstr *instruction)
{
    (void)instruction; p->last_native_cost=1;
    if(!is_internal_pid(args[0])) { p->freason=LB_EXC_BADARG; return THE_NON_VALUE; }
    if(args[1]!=am_kill || !p->world || !p->running) { p->status=LB_PROCESS_REFUSED; return THE_NON_VALUE; }
    LbProcess *target=lb_pid_lookup(p->entry_module->space->pids,args[0]);
    if(target) {
        if(!lb_work_domain_charge(p->entry_module->space->domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
        lb_work_cross_effect(p->work_active,target->work_birth);
        target->exit_requested=1; target->waiting=0;
    }
    return am_true;
}
Eterm lb_bif_register_2(LbProcess *p,Eterm *args,const BeamInstr *instruction)
{
    (void)instruction; p->last_native_cost=3;
    LbProcessStatus status=lb_registry_register(p,args[0],args[1]);
    if(status==LB_PROCESS_READY) return am_true;
    if(status==LB_PROCESS_EXCEPTION) p->freason=LB_EXC_BADARG;
    else p->status=status;
    return THE_NON_VALUE;
}
/* bif.c:whereis_1/unregister_1 and register.c, over the owning namespace. Ports
 * cannot be registered in this profile. Lookup charges each visited bucket
 * entry; distinguish exhaustion from a genuinely absent registration. */
static Eterm registered_name(LbProcess *p,Eterm name,int remove)
{
    LbCodeSpace *space=p->entry_module->space;
    p->last_native_cost=1;
    if(!lb_work_domain_charge(space->domain,1)) {
        p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE;
    }
    if(!is_atom(name)) { p->freason=LB_EXC_BADARG; return THE_NON_VALUE; }
    LbProcess *target=lb_registry_lookup(space,name,&p->last_native_cost);
    if(lb_alloc_domain_failure(space->domain)==LB_ALLOC_WORK_LIMIT) {
        p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE;
    }
    if(!remove) return target?target->identity.id:am_undefined;
    if(!target) { p->freason=LB_EXC_BADARG; return THE_NON_VALUE; }
    lb_registry_remove(target);
    return am_true;
}
Eterm lb_bif_registered_0(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)args; (void)pc; Eterm result;
    LbProcessStatus status=lb_registry_names(p,&result);
    if(status!=LB_PROCESS_READY) p->status=status;
    return result;
}
Eterm lb_bif_whereis_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{ (void)pc; return registered_name(p,args[0],0); }
Eterm lb_bif_unregister_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{ (void)pc; return registered_name(p,args[0],1); }
/* Native bif.c error_1: retain the supplied reason, with error class and
 * logging/trace flags. This does not admit stacktrace construction or unwind. */
Eterm lb_bif_error_1(LbProcess *p,Eterm *args,const BeamInstr *instruction)
{
    (void)instruction;
    p->last_native_cost=1; p->fvalue=args[0]; p->freason=LB_EXC_ERROR;
    return THE_NON_VALUE;
}
Eterm lb_bif_throw_1(LbProcess *p,Eterm *args,const BeamInstr *instruction)
{
    (void)instruction;
    p->last_native_cost=1; p->fvalue=args[0]; p->freason=LB_EXC_THROWN;
    return THE_NON_VALUE;
}
/* Complete native error_3 option scan and error_2 fallback. Reserve before
 * borrowing moving list/map operands; args points to registered X slots. */
Eterm lb_bif_error_3(LbProcess *p,Eterm *args,const BeamInstr *instruction)
{
    (void)instruction; p->last_native_cost=1;
    size_t need,before=p->collections;
    if(!lb_size_add(4,p->heap_reserved,&need) || !lb_heap_reserve(p,need,3)) {
        p->status=LB_PROCESS_NO_MEMORY; return THE_NON_VALUE;
    }
    if(p->collections!=before) p->last_native_cost+=p->last_gc_cost;
    Eterm info=THE_NON_VALUE,list=args[2];
    while(is_list(list)) {
        if(!lb_work_domain_charge(p->entry_module->space->domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
        if(++p->last_native_cost>LB_MAX_BINARY_BYTES/sizeof(Eterm)) {
            p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE;
        }
        Eterm term=list_val(list)[0];
        if(!is_tuple(term)) break;
        Eterm *tuple=tuple_val(term);
        if(arityval(tuple[0])!=2 || tuple[1]!=am_error_info) break;
        info=tuple[2];
        if(!is_boxed(info) || boxed_val(info)[0]!=LB_FLATMAP_HEADER) break;
        list=list_val(list)[1];
    }
    if(!is_nil(list)) info=THE_NON_VALUE;
    if(!is_non_value(info)) {
        p->fvalue=TUPLE3(p->htop,args[0],args[1],info); p->htop+=4; p->freason=LB_EXC_ERROR_3;
    } else {
        p->fvalue=TUPLE2(p->htop,args[0],args[1]); p->htop+=3; p->freason=LB_EXC_ERROR_2;
    }
    return THE_NON_VALUE;
}
Eterm lb_bif_make_ref_0(LbProcess *p,Eterm *args,const BeamInstr *instruction)
{
    (void)args; (void)instruction;
    LbEngine *engine=p->entry_module->space->engine;
    p->last_native_cost=1;
    /* Refuse generation wrap, rather than equating a fresh ref with an old
     * value that can still exist. Failure neither consumes an ID nor publishes. */
    if(engine->reference_counter==UINT64_MAX) { p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE; }
    size_t before=p->collections,need;
    if(!lb_size_add(LB_REF_WORDS,p->heap_reserved,&need) || !lb_heap_reserve(p,need,0)) {
        p->status=LB_PROCESS_NO_MEMORY; return THE_NON_VALUE;
    }
    if(p->collections!=before) p->last_native_cost+=p->last_gc_cost;
    Eterm result=lb_reference_write(p->htop,engine->reference_counter++);
    p->htop+=LB_REF_WORDS; return result;
}
