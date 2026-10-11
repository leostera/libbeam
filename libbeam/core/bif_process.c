/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1996-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#include "process_internal.h"
#include "reference.h"
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
