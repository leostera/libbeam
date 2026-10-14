/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1996-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#include "process_internal.h"
#include "reference.h"
#include "flatmap.h"
#include "world_internal.h"
#include "exit_signal.h"
typedef enum {
    UNIQUE_DEFAULT=0, UNIQUE_POSITIVE=1, UNIQUE_MONOTONIC=2,
    UNIQUE_POSITIVE_MONOTONIC=3
} UniqueOptions;
static Eterm unique_integer(LbProcess *p,Eterm modifiers)
{
    LbAllocDomain *domain=p->entry_module->space->domain;
    UniqueOptions options=UNIQUE_DEFAULT; p->last_native_cost=0;
    if(!lb_work_domain_charge(domain,1)) goto work_failure;
    p->last_native_cost=1;
    while(is_list(modifiers)) {
        if(!lb_work_domain_charge(domain,1)) goto work_failure;
        ++p->last_native_cost; Eterm option=CAR(list_val(modifiers));
        if(option==am_positive) options=(UniqueOptions)(options|UNIQUE_POSITIVE);
        else if(option==am_monotonic) options=(UniqueOptions)(options|UNIQUE_MONOTONIC);
        else goto badarg;
        modifiers=CDR(list_val(modifiers));
    }
    if(!is_nil(modifiers)) goto badarg;
    LbUniqueSequence *sequence=&p->entry_module->space->engine->unique_sequences[!!(options&UNIQUE_MONOTONIC)];
    if(sequence->exhausted) { p->freason=LB_EXC_SYSTEM_LIMIT; return THE_NON_VALUE; }
    Eterm result,words[4]; const Uint offset=(Uint)(-MIN_SMALL);
    if(!(options&UNIQUE_POSITIVE) && !sequence->high && sequence->low<offset) {
        result=make_small(MIN_SMALL+(Sint)sequence->low);
    } else {
        if(options&UNIQUE_POSITIVE) {
            words[1]=sequence->low+1;
            words[2]=sequence->high+(words[1]==0);
            words[3]=!words[1] && !words[2];
        } else {
            words[1]=sequence->low-offset;
            words[2]=sequence->high-(sequence->low<offset); words[3]=0;
        }
        if(!words[3] && !words[2] && words[1]<=(Uint)MAX_SMALL) result=make_small((Sint)words[1]);
        else {
            size_t digits=words[3]?3:words[2]?2:1,need,collections=p->collections;
            words[0]=_make_header(digits,POS_BIG_SUBTAG);
            if(!lb_work_domain_charge(domain,digits+1)) goto work_failure;
            p->last_native_cost+=digits+1;
            if(!lb_size_add(digits+1,p->heap_reserved,&need)) { p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE; }
            if(!lb_heap_reserve(p,need,MAX_REG)) {
                LbAllocStatus cause=lb_alloc_domain_failure(domain);
                p->status=cause==LB_ALLOC_WORK_LIMIT?LB_PROCESS_WORK_LIMIT:
                    cause==LB_ALLOC_LIMIT?LB_PROCESS_LIMIT:LB_PROCESS_NO_MEMORY;
                return THE_NON_VALUE;
            }
            if(p->collections!=collections) p->last_native_cost+=p->last_gc_cost;
            result=make_boxed(p->htop); memcpy(p->htop,words,(digits+1)*sizeof(Eterm)); p->htop+=digits+1;
        }
    }
    /* Serialized, infallible publication. Failed parsing, charging or GC cannot
     * consume a value. Gaps after a successful query are allowed, as for refs. */
    if(sequence->low==UINT64_MAX && sequence->high==UINT64_MAX) sequence->exhausted=1;
    else if(++sequence->low==0) ++sequence->high;
    return result;
work_failure:
    p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE;
badarg:
    p->freason=LB_EXC_BADARG; return THE_NON_VALUE;
}
Eterm lb_bif_unique_integer_0(LbProcess *p,Eterm *args,const BeamInstr *instruction)
{ (void)args; (void)instruction; return unique_integer(p,NIL); }
Eterm lb_bif_unique_integer_1(LbProcess *p,Eterm *args,const BeamInstr *instruction)
{ (void)instruction; return unique_integer(p,args[0]); }

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
        return status==LB_PID_FULL?LB_PROCESS_LIMIT:status==LB_PID_NO_MEMORY?LB_PROCESS_NO_MEMORY:LB_PROCESS_INVALID;
    }
    space->pids=table; *out=p->identity.id; return LB_PROCESS_READY;
}
/* A host-created root leads its own group; no host I/O PID is fabricated.
 * Guest children receive the parent's PID-valued setting before publication. */
LbProcessStatus lb_process_group_leader(LbProcess *p,Eterm *out)
{
    if(!p || !out) return LB_PROCESS_INVALID;
    if(is_nil(p->group_leader)) {
        Eterm pid; LbProcessStatus status=lb_process_identity(p,&pid);
        if(status!=LB_PROCESS_READY) return status;
        p->group_leader=pid;
    }
    assert(is_internal_pid(p->group_leader));
    *out=p->group_leader; return LB_PROCESS_READY;
}
Eterm lb_bif_group_leader_0(LbProcess *p,Eterm *args,const BeamInstr *instruction)
{
    (void)args; (void)instruction; p->last_native_cost=1;
    if(!lb_work_domain_charge(p->entry_module->space->domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    Eterm leader; LbProcessStatus status=lb_process_group_leader(p,&leader);
    if(status==LB_PROCESS_READY) return leader;
    p->status=status; return THE_NON_VALUE;
}
Eterm lb_bif_group_leader_2(LbProcess *p,Eterm *args,const BeamInstr *instruction)
{
    (void)instruction; p->last_native_cost=2;
    LbCodeSpace *space=p->entry_module->space;
    if(!lb_work_domain_charge(space->domain,2)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    if(!is_pid(args[0]) || !is_pid(args[1])) goto badarg;
    if(!is_internal_pid(args[0]) || !is_internal_pid(args[1])) { p->status=LB_PROCESS_REFUSED; return THE_NON_VALUE; }
    LbProcess *target=lb_pid_lookup(space->pids,args[1]);
    if(!target) goto badarg;
    /* The leader itself may already be dead; setting its PID does not retain
     * that process. Only the target must exist at this serialized boundary. */
    target->group_leader=args[0];
    lb_work_cross_effect(p->work_active,target->work_birth); return am_true;
 badarg: p->freason=LB_EXC_BADARG; return THE_NON_VALUE;
}
/* bif.c:send_2 delegates to the same local delivery path as the send
 * instruction. Charge invocation before lookup, allocation or publication. */
Eterm lb_bif_send_2(LbProcess *p,Eterm *args,const BeamInstr *instruction)
{
    (void)instruction; p->last_native_cost=1;
    if(!lb_work_domain_charge(p->entry_module->space->domain,1)) {
        p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE;
    }
    Eterm result=lb_erl_send(p,args[0],args[1]);
    ++p->last_native_cost; return result;
}
/* Collection is transactional: failure precedes every forwarding write.
 * Domain borrowing keeps all work charged to the requesting invocation. */
static Eterm collect_process(LbProcess *caller,LbProcess *target,size_t live)
{
    if(!lb_process_collect_live(target,0,live)) {
        LbAllocStatus cause=lb_alloc_domain_failure(caller->entry_module->space->domain);
        caller->status=cause==LB_ALLOC_WORK_LIMIT?LB_PROCESS_WORK_LIMIT:
            cause==LB_ALLOC_NO_MEMORY?LB_PROCESS_NO_MEMORY:LB_PROCESS_LIMIT;
        return THE_NON_VALUE;
    }
    caller->last_native_cost+=target->last_gc_cost;
    if(caller!=target) lb_work_cross_effect(caller->work_active,target->work_birth);
    return am_true;
}
Eterm lb_bif_garbage_collect_0(LbProcess *p,Eterm *args,const BeamInstr *instruction)
{
    (void)args; (void)instruction; p->last_native_cost=1;
    if(!lb_work_domain_charge(p->entry_module->space->domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    return collect_process(p,p,0);
}
Eterm lb_bif_garbage_collect_1(LbProcess *p,Eterm *args,const BeamInstr *instruction)
{
    (void)instruction; p->last_native_cost=1;
    LbCodeSpace *space=p->entry_module->space;
    if(!lb_work_domain_charge(space->domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    if(!is_internal_pid(args[0])) {
        if(is_pid(args[0])) p->status=LB_PROCESS_REFUSED;
        else p->freason=LB_EXC_BADARG;
        return THE_NON_VALUE;
    }
    LbProcess *target=lb_pid_lookup(space->pids,args[0]);
    if(!target) return am_false;
    /* The shared worker and Engine serialization leave every other process at
     * a saved dispatch boundary. Preserve its whole initialized X bank: the
     * requester's arity says nothing about the target continuation's liveness. */
    assert(target==p || !target->running);
    return collect_process(p,target,target==p?1:MAX_REG);
}
Eterm lb_bif_self_0(LbProcess *p,Eterm *args,const BeamInstr *instruction)
{
    (void)args; (void)instruction;
    Eterm result;
    LbProcessStatus status=lb_process_identity(p,&result);
    if(status==LB_PROCESS_READY) return result;
    p->status=status; return THE_NON_VALUE;
}
/* Ordinary local process monitor: validate before effects, stage the ref and
 * either a pair of monitor halves or an owned immediate noproc notification.
 * The reference counter and heap top commit only after fallible work ends. */
Eterm lb_bif_monitor_2(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)pc; LbCodeSpace *space=p->entry_module->space; LbEngine *engine=space->engine;
    LbAllocDomain *domain=space->domain; Eterm target=args[1],name=NIL;
    p->last_native_cost=1;
    if(args!=p->reg) { p->status=LB_PROCESS_INVALID; return THE_NON_VALUE; }
    if(!lb_work_domain_charge(domain,1)) goto work;
    if(args[0]!=am_process) {
        if(args[0]==am_port) {
            if(is_atom(target) || (target&_TAG_IMMED1_MASK)==_TAG_IMMED1_PORT ||
               (is_boxed(target) && (*boxed_val(target)&_TAG_HEADER_MASK)==EXTERNAL_PORT_SUBTAG)) goto refused;
            if(is_tuple(target)) {
                Eterm *tuple=tuple_val(target);
                if(arityval(tuple[0])==2 && is_atom(tuple[1]) && is_atom(tuple[2]) && tuple[2]==space->node_name) goto refused;
            }
        }
        if(args[0]==am_time_offset && target==am_clock_service) goto refused;
        goto badarg;
    }
    if(is_tuple(target)) {
        Eterm *tuple=tuple_val(target);
        if(arityval(tuple[0])!=2 || !is_atom(tuple[1]) || !is_atom(tuple[2])) goto badarg;
        if(tuple[2]!=space->node_name) goto refused;
        target=tuple[1];
    }
    if(is_atom(target)) name=target;
    else if(!is_internal_pid(target)) { if(is_pid(target)) goto refused; goto badarg; }
    if(!p->world || !p->running) goto refused;
    LbProcess *other=is_atom(name)?lb_registry_lookup(space,name,&p->last_native_cost):lb_pid_lookup(space->pids,target);
    if(lb_alloc_domain_failure(domain)==LB_ALLOC_WORK_LIMIT) goto work;
    if(engine->reference_counter==UINT64_MAX) { p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE; }
    size_t need,before=p->collections;
    if(!lb_size_add(LB_REF_WORDS,p->heap_reserved,&need)) { p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE; }
    if(!lb_work_domain_charge(domain,LB_REF_WORDS)) goto work;
    p->last_native_cost+=LB_REF_WORDS;
    if(!lb_heap_reserve(p,need,2)) goto memory;
    if(p->collections!=before) p->last_native_cost+=p->last_gc_cost;
    Eterm ref=lb_reference_write(p->htop,engine->reference_counter);
    if(other && other!=p) {
        if(!lb_work_domain_charge(domain,2)) goto work;
        Uint prior=p->last_native_cost; Eterm pid;
        LbProcessStatus status=lb_process_identity(p,&pid); p->last_native_cost+=prior+2;
        if(status!=LB_PROCESS_READY) { if(status==LB_PROCESS_NO_MEMORY) goto memory; p->status=status; return THE_NON_VALUE; }
        LbMonitor *monitor;
        if(lb_monitor_prepare(p,ref,&monitor)!=LB_PROCESS_READY) goto memory;
        monitor->target_name=name;
        lb_monitor_publish(monitor,other);
    } else if(!other) {
        Eterm named[3]={make_arityval(2),name,space->node_name};
        Eterm object=is_atom(name)?make_boxed(named):target;
        Eterm tuple[6]={make_arityval(5),am_DOWN,ref,am_process,object,am_noproc};
        LbMessage *message; Uint cost=0;
        LbProcessStatus status=lb_message_create(space,make_boxed(tuple),&message,&cost); p->last_native_cost+=cost;
        if(status!=LB_PROCESS_READY) { if(status==LB_PROCESS_NO_MEMORY) goto memory; p->status=status; return THE_NON_VALUE; }
        if(!lb_work_domain_charge(domain,1)) { lb_message_destroy(message); goto work; }
        ++p->last_native_cost;
        status=lb_message_deliver(p,message);
        if(status!=LB_PROCESS_READY) { lb_message_destroy(message); p->status=status; return THE_NON_VALUE; }
    }
    p->htop+=LB_REF_WORDS; ++engine->reference_counter; return ref;
badarg: p->freason=LB_EXC_BADARG; return THE_NON_VALUE;
refused: p->status=LB_PROCESS_REFUSED; return THE_NON_VALUE;
work: p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE;
memory: {
    LbAllocStatus cause=lb_alloc_domain_failure(domain);
    p->status=cause==LB_ALLOC_WORK_LIMIT?LB_PROCESS_WORK_LIMIT:
        (cause==LB_ALLOC_LIMIT || cause==LB_ALLOC_OK)?LB_PROCESS_LIMIT:LB_PROCESS_NO_MEMORY;
    return THE_NON_VALUE;
}
}
Eterm lb_bif_demonitor_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)pc; p->last_native_cost=1;
    if(!lb_work_domain_charge(p->entry_module->space->domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    if(!lb_reference(args[0])) { p->freason=LB_EXC_BADARG; return THE_NON_VALUE; }
    lb_monitor_demonitor(p,args[0],0);
    return p->status==LB_PROCESS_WORK_LIMIT?THE_NON_VALUE:am_true;
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
static int spawn_mfa_input(LbProcess *p,Eterm *args,LbSpawnMfa *out)
{
    p->last_native_cost=1; LbAllocDomain *domain=p->entry_module->space->domain;
    if(!lb_work_domain_charge(domain,1)) goto work;
    if(!is_atom(args[0]) || !is_atom(args[1])) goto badarg;
    Eterm tail=args[2]; size_t arity=0;
    while(is_list(tail)) {
        if(!lb_work_domain_charge(domain,1)) goto work;
        ++p->last_native_cost; ++arity; tail=CDR(list_val(tail));
    }
    if(!is_nil(tail)) goto badarg;
    if(arity>MAX_ARG) { p->freason=LB_EXC_SYSTEM_LIMIT; return 0; }
    if(!p->world || !p->running) { p->status=LB_PROCESS_REFUSED; return 0; }
    *out=(LbSpawnMfa){args[0],args[1],args[2],(unsigned)arity}; return 1;
badarg: p->freason=LB_EXC_BADARG; return 0;
work: p->status=LB_PROCESS_WORK_LIMIT; return 0;
}
Eterm lb_bif_spawn_link_3(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)pc; LbSpawnMfa start;
    if(!spawn_mfa_input(p,args,&start)) return THE_NON_VALUE;
    Eterm from; LbProcessStatus status=lb_process_identity(p,&from);
    if(status!=LB_PROCESS_READY) { p->status=status; return THE_NON_VALUE; }
    LbLink *link; status=lb_link_prepare(p,&link);
    if(status!=LB_PROCESS_READY) { p->status=status; return THE_NON_VALUE; }
    Eterm pid;
    if(start.module==am_erlang && start.function==am_apply && start.arity==2) {
        Eterm fun=CAR(list_val(start.arguments)),arguments=CAR(list_val(CDR(list_val(start.arguments))));
        const LbFun *value=lb_fun(fun);
        status=!value || lb_fun_arity(value)!=0 || !is_nil(arguments)?LB_PROCESS_REFUSED:
            lb_task_spawn_fun_linked_locked(p->world,p,fun,link,&pid);
    } else status=lb_task_spawn_mfa_linked_locked(p->world,p,&start,link,&pid);
    if(status!=LB_PROCESS_READY) { lb_link_discard(link); p->status=status; return THE_NON_VALUE; }
    return pid;
}
/* erlang:spawn_monitor/3's local MFA startup, with the same prepublication
 * monitor/task transaction as the fun wrapper. No child executes on this stack. */
Eterm lb_bif_spawn_monitor_3(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)pc; LbSpawnMfa start;
    if(!spawn_mfa_input(p,args,&start)) return THE_NON_VALUE;
    LbAllocDomain *domain=p->entry_module->space->domain;
    LbEngine *engine=p->entry_module->space->engine;
    if(engine->reference_counter==UINT64_MAX) { p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE; }
    size_t need,before=p->collections;
    if(!lb_size_add(LB_REF_WORDS+3,p->heap_reserved,&need)) { p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE; }
    if(!lb_heap_reserve(p,need,3)) {
        LbAllocStatus cause=lb_alloc_domain_failure(domain);
        p->status=cause==LB_ALLOC_WORK_LIMIT?LB_PROCESS_WORK_LIMIT:
            cause==LB_ALLOC_NO_MEMORY?LB_PROCESS_NO_MEMORY:LB_PROCESS_LIMIT;
        return THE_NON_VALUE;
    }
    if(p->collections!=before) p->last_native_cost+=p->last_gc_cost;
    Eterm parent_pid; LbProcessStatus status=lb_process_identity(p,&parent_pid);
    if(status!=LB_PROCESS_READY) { p->status=status; return THE_NON_VALUE; }
    Eterm ref=lb_reference_write(p->htop,engine->reference_counter); p->htop+=LB_REF_WORDS;
    LbMonitor *monitor; status=lb_monitor_prepare(p,ref,&monitor);
    if(status!=LB_PROCESS_READY) { p->status=status; return THE_NON_VALUE; }
    /* Reacquire after reserve/GC. Child copying borrows this list without
     * collecting the parent; no fallible acquisition follows PID publication. */
    start.arguments=args[2]; Eterm pid;
    status=lb_task_spawn_mfa_monitored_locked(p->world,p,&start,monitor,&pid);
    if(status!=LB_PROCESS_READY) { lb_monitor_discard(monitor); p->status=status; return THE_NON_VALUE; }
    ++engine->reference_counter;
    Eterm result=TUPLE2(p->htop,pid,ref); p->htop+=3; return result;
}
/* Exact spawn_opt/2 fun profile. Option traversal and all parent-side
 * reservations precede monitor/link preparation and runnable publication.
 * This slice supports the ordinary, [monitor], and [link] forms; mixed links
 * plus monitors and scheduler/heap policy options remain an explicit profile
 * refusal, never a silently dropped option. */
Eterm lb_bif_spawn_opt_2(LbProcess *p,Eterm *args,const BeamInstr *instruction)
{
    (void)instruction; LbAllocDomain *domain=p->entry_module->space->domain;
    p->last_native_cost=1;
    const LbFun *fun=lb_fun(args[0]);
    if(!fun || lb_fun_arity(fun)!=0) goto badarg;
    Eterm tail=args[1]; int monitored=0,linked=0,unsupported=0; size_t count=0;
    while(is_list(tail)) {
        if(!lb_work_domain_charge(domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
        if(++count>LB_MAX_BINARY_BYTES/sizeof(Eterm)) { p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE; }
        Eterm option=CAR(list_val(tail)); tail=CDR(list_val(tail)); ++p->last_native_cost;
        if(option==am_monitor) monitored=1;
        else if(option==am_link) linked=1;
        else if(is_atom(option)) {
            LbBeamBytes name;
            if(lb_atoms_name(p->entry_module->space->atoms,option,&name)!=LB_ATOM_OK) goto badarg;
            static const char *valid_atoms[]={"sensitive","async_dist"}; int valid=0;
            for(size_t i=0;i<sizeof(valid_atoms)/sizeof(*valid_atoms);++i)
                if(name.size==strlen(valid_atoms[i]) && !memcmp(name.data,valid_atoms[i],name.size)) valid=1;
            if(!valid) goto badarg;
            unsupported=1;
        } else if(is_tuple(option)) {
            Eterm *tuple=tuple_val(option);
            if(arityval(tuple[0])!=2 || !is_atom(tuple[1])) goto badarg;
            LbBeamBytes key; if(lb_atoms_name(p->entry_module->space->atoms,tuple[1],&key)!=LB_ATOM_OK) goto badarg;
            static const char *valid_keys[]={"priority","fullsweep_after","min_heap_size","min_bin_vheap_size","max_heap_size","message_queue_data","scheduler"}; int valid=0;
            for(size_t i=0;i<sizeof(valid_keys)/sizeof(*valid_keys);++i)
                if(key.size==strlen(valid_keys[i]) && !memcmp(key.data,valid_keys[i],key.size)) valid=1;
            if(!valid) goto badarg;
            unsupported=1;
        } else goto badarg;
    }
    if(!is_nil(tail)) goto badarg;
    if(unsupported || (monitored&&linked)) goto refused;
    if(!p->world || !p->running) goto refused;
    if(!monitored && !linked) {
        Eterm pid; LbProcessStatus status=lb_task_spawn_fun_locked(p->world,p,args[0],&pid);
        if(status!=LB_PROCESS_READY) { p->status=status==LB_PROCESS_INVALID?LB_PROCESS_REFUSED:status; return THE_NON_VALUE; }
        return pid;
    }
    if(linked) {
        LbLink *link; LbProcessStatus status=lb_link_prepare(p,&link);
        if(status!=LB_PROCESS_READY) { p->status=status; return THE_NON_VALUE; }
        Eterm pid; status=lb_task_spawn_fun_linked_locked(p->world,p,args[0],link,&pid);
        if(status!=LB_PROCESS_READY) { lb_link_discard(link); p->status=status==LB_PROCESS_INVALID?LB_PROCESS_REFUSED:status; return THE_NON_VALUE; }
        return pid;
    }
    LbEngine *engine=p->entry_module->space->engine;
    if(engine->reference_counter==UINT64_MAX) goto limit;
    size_t need,before=p->collections;
    if(!lb_size_add(LB_REF_WORDS+3,p->heap_reserved,&need)) goto limit;
    if(!lb_heap_reserve(p,need,4)) {
        LbAllocStatus cause=lb_alloc_domain_failure(domain);
        p->status=cause==LB_ALLOC_WORK_LIMIT?LB_PROCESS_WORK_LIMIT:
            cause==LB_ALLOC_NO_MEMORY?LB_PROCESS_NO_MEMORY:LB_PROCESS_LIMIT;
        return THE_NON_VALUE;
    }
    if(p->collections!=before) p->last_native_cost+=p->last_gc_cost;
    Eterm parent_pid; LbProcessStatus status=lb_process_identity(p,&parent_pid);
    if(status!=LB_PROCESS_READY) { p->status=status; return THE_NON_VALUE; }
    (void)parent_pid;
    Eterm ref=lb_reference_write(p->htop,engine->reference_counter); p->htop+=LB_REF_WORDS;
    LbMonitor *monitor; status=lb_monitor_prepare(p,ref,&monitor);
    if(status!=LB_PROCESS_READY) { p->status=status; return THE_NON_VALUE; }
    Eterm pid; status=lb_task_spawn_fun_monitored_locked(p->world,p,args[0],monitor,&pid);
    if(status!=LB_PROCESS_READY) { lb_monitor_discard(monitor); p->status=status==LB_PROCESS_INVALID?LB_PROCESS_REFUSED:status; return THE_NON_VALUE; }
    ++engine->reference_counter;
    Eterm result=TUPLE2(p->htop,pid,ref); p->htop+=3; return result;
badarg: p->freason=LB_EXC_BADARG; return THE_NON_VALUE;
refused: p->status=LB_PROCESS_REFUSED; return THE_NON_VALUE;
limit: p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE;
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
    if(!lb_work_domain_charge(p->entry_module->space->domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
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
    lb_monitor_demonitor(p,args[0],flush);
    return p->status==LB_PROCESS_WORK_LIMIT?THE_NON_VALUE:am_true;
badarg: p->freason=LB_EXC_BADARG; return THE_NON_VALUE;
}
/* bif.c:process_flag_2. trap_exit is process-local, not inherited by spawn.
 * Unsupported policies remain refusals until their actual consumers exist. */
Eterm lb_bif_process_flag_2(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)pc; p->last_native_cost=1;
    if(!lb_work_domain_charge(p->entry_module->space->domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    if(args[0]==am_trap_exit) {
        if(args[1]!=am_true && args[1]!=am_false) goto badarg;
        Eterm old=(p->flags&F_TRAP_EXIT)?am_true:am_false;
        if(args[1]==am_true) p->flags|=F_TRAP_EXIT;
        else p->flags&=~F_TRAP_EXIT;
        return old;
    }
    if(args[0]==am_error_handler || args[0]==am_priority || args[0]==am_scheduler ||
       args[0]==am_fullsweep_after || args[0]==am_min_heap_size || args[0]==am_min_bin_vheap_size ||
       args[0]==am_max_heap_size || args[0]==am_message_queue_data || args[0]==am_sensitive ||
       args[0]==am_async_dist || args[0]==am_save_calls || args[0]==am_monitor_nodes)
        goto refused;
    if(is_tuple(args[0])) {
        Eterm *tuple=tuple_val(args[0]);
        if(arityval(tuple[0])==2 && tuple[1]==am_monitor_nodes) goto refused;
    }
 badarg: p->freason=LB_EXC_BADARG; return THE_NON_VALUE;
 refused: p->status=LB_PROCESS_REFUSED; return THE_NON_VALUE;
}
Eterm lb_bif_exit_2(LbProcess *p,Eterm *args,const BeamInstr *instruction)
{
    (void)instruction; p->last_native_cost=1;
    if(!lb_work_domain_charge(p->entry_module->space->domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    if(!is_internal_pid(args[0])) {
        if(is_external_pid(args[0]) || (args[0]&_TAG_IMMED1_MASK)==_TAG_IMMED1_PORT) p->status=LB_PROCESS_REFUSED;
        else p->freason=LB_EXC_BADARG;
        return THE_NON_VALUE;
    }
    if(!p->world || !p->running) { p->status=LB_PROCESS_REFUSED; return THE_NON_VALUE; }
    LbProcess *target=lb_pid_lookup(p->entry_module->space->pids,args[0]);
    if(!target) return am_true;
    Eterm from; LbProcessStatus status=lb_process_identity(p,&from);
    if(status==LB_PROCESS_READY) status=lb_exit_signal_send(p,target,from,args[1],LB_EXIT_EXPLICIT);
    if(status!=LB_PROCESS_READY) { p->status=status; return THE_NON_VALUE; }
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
