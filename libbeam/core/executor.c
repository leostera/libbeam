/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Joinable Engine worker driving the existing generated BEAM interpreter.
 * No independent VM, tenant TLS, ambient native callbacks or process startup.
 */
#define _POSIX_C_SOURCE 200809L
#include "executor.h"
#include "receive.h"
#include "timer.h"
#include <time.h>
#include <errno.h>
#include "code_internal.h"
#include "binary.h"
#include "world_internal.h"
#include "process_internal.h"
#include "work_budget_internal.h"
#include <sched.h>

struct LbTask {
    LbEngine *engine;
    LbAllocDomain *domain;
    LbTask *next;
    LbProcess *process;
    LbCall *completion;
    LbWorld *world; /* borrowed until that world's serialized stop drains children */
    int background;
    LbTaskStatus status;
};
static void checked(int status) { if(status) abort(); } /* ownership invariant */
void lb_engine_enter(LbEngine *engine)
{
    checked(pthread_mutex_lock(&engine->mutex));
    assert(!engine->control_borrow); engine->control_borrow=1;
}
void lb_engine_leave(LbEngine *engine)
{
    int detached;
    assert(engine->control_borrow); engine->control_borrow=0;
    detached=lb_engine_is_detached(engine);
    checked(pthread_mutex_unlock(&engine->mutex));
    if(detached) lb_engine_release_detached(engine);
}
LbEngineStatus lb_executor_init(LbEngine *engine)
{
    if(pthread_mutex_init(&engine->mutex,NULL)) return LB_ENGINE_NO_MEMORY;
    if(pthread_cond_init(&engine->wake,NULL)) {
        checked(pthread_mutex_destroy(&engine->mutex)); return LB_ENGINE_NO_MEMORY;
    }
    engine->sync_ready=1; return LB_ENGINE_OK;
}
void lb_executor_shutdown(LbEngine *engine)
{
    if(!engine->sync_ready) return;
    assert(!engine->tasks);
    checked(pthread_mutex_lock(&engine->mutex));
    engine->worker_stop=1;
    checked(pthread_cond_signal(&engine->wake));
    checked(pthread_mutex_unlock(&engine->mutex));
    if(engine->worker_started) checked(pthread_join(engine->worker,NULL));
    checked(pthread_cond_destroy(&engine->wake));
    checked(pthread_mutex_destroy(&engine->mutex));
    engine->worker_started=0; engine->sync_ready=0;
}
static LbTask *runnable(LbEngine *engine)
{
    LbTask *first=engine->cursor?engine->cursor:engine->tasks,*task=first;
    if(!first) return NULL;
    do {
        if(task->status==LB_TASK_PENDING) {
            LbProcessStatus status=lb_receive_timer_poll(task->process);
            if(status!=LB_PROCESS_READY) { task->process->status=status; task->process->waiting=0; }
            if(!task->process->waiting) return task;
        }
        task=task->next?task->next:engine->tasks;
    } while(task!=first);
    return NULL;
}
typedef enum { NO_FAILURE, MEMORY_FAILURE, WORK_FAILURE, ALLOCATION_FAILURE } TaskFailure;
static TaskFailure finish_task(LbTask *task,LbTaskStatus status)
{
    LbCall *call=task->completion;
    TaskFailure limited=status==LB_TASK_MEMORY_LIMIT?MEMORY_FAILURE:
        status==LB_TASK_WORK_LIMIT?WORK_FAILURE:status==LB_TASK_NO_MEMORY?ALLOCATION_FAILURE:NO_FAILURE;
    if(status!=LB_TASK_CANCELLED && status!=LB_TASK_MEMORY_LIMIT &&
       status!=LB_TASK_WORK_LIMIT && status!=LB_TASK_CAPABILITY_DENIED) {
        lb_link_exit(task->process,1);
        lb_monitor_exit(task->process,1);
        if(lb_alloc_domain_failure(task->domain)==LB_ALLOC_LIMIT) { status=LB_TASK_MEMORY_LIMIT; limited=MEMORY_FAILURE; }
        if(lb_alloc_domain_failure(task->domain)==LB_ALLOC_WORK_LIMIT) { status=LB_TASK_WORK_LIMIT; limited=WORK_FAILURE; }
        if(lb_alloc_domain_failure(task->domain)==LB_ALLOC_NO_MEMORY) { status=LB_TASK_NO_MEMORY; limited=ALLOCATION_FAILURE; }
    }
    if(status==LB_TASK_WORK_LIMIT && call && task->domain->failed_work && call->budget!=task->domain->failed_work)
        status=LB_TASK_CANCELLED;
    /* A denial belongs to its causal owner, not an unrelated root whose
     * execution happened to borrow that owner's work. */
    if(status==LB_TASK_CAPABILITY_DENIED && call && !call->budget->capability_failure)
        status=LB_TASK_CANCELLED;
    task->status=status;
    if(call) {
        lb_call_complete(call,task->process,status);
        if(call->status==LB_WORLD_MEMORY_LIMIT) { task->status=LB_TASK_MEMORY_LIMIT; limited=MEMORY_FAILURE; }
        if(call->status==LB_WORLD_WORK_LIMIT) { task->status=LB_TASK_WORK_LIMIT; limited=WORK_FAILURE; }
        if(call->status==LB_WORLD_NO_MEMORY) { task->status=LB_TASK_NO_MEMORY; limited=ALLOCATION_FAILURE; }
        // A foreign causal owner can turn this root's completion into CANCELLED,
        // but its original failure must still trigger family/world containment.
        if(lb_alloc_domain_failure(task->domain)==LB_ALLOC_LIMIT) limited=MEMORY_FAILURE;
        if(lb_alloc_domain_failure(task->domain)==LB_ALLOC_WORK_LIMIT) limited=WORK_FAILURE;
        if(lb_alloc_domain_failure(task->domain)==LB_ALLOC_NO_MEMORY) limited=ALLOCATION_FAILURE;
        lb_work_domain_borrow(task->domain,NULL);
        lb_process_destroy(task->process); task->process=NULL;
        if(!call->owner_live) {
            lb_task_release_locked(task); call->task=NULL; lb_call_discard_completed(call);
        }
    } else if(task->background) {
        lb_work_domain_borrow(task->domain,NULL);
        lb_task_release_locked(task);
    } else if(status==LB_TASK_CANCELLED) {
        lb_work_domain_borrow(task->domain,NULL);
        lb_process_destroy(task->process); task->process=NULL;
    }
    return limited;
}
void lb_task_cancel_family_locked(LbWorkBudget *budget,LbTaskStatus originating_status)
{
    assert(budget && !budget->domain->work);
    if(lb_work_budget_retain(budget)!=LB_ALLOC_OK) abort(); // Physical lease count cannot approach SIZE_MAX.
    LbWorld *world=budget->world;
    if(budget->call && budget->call->status==LB_WORLD_PENDING)
        finish_task(budget->call->task,originating_status);
    /* A transient host allocation failure aborts its family, including an
     * unmonitored child, while preserving the existing namespace retry policy.
     * Configured limits, denial and explicit cancellation still close escaped
     * namespaces. Monitored dependants receive infrastructure failure, not DOWN. */
    if(budget->escaped && originating_status!=LB_TASK_NO_MEMORY) lb_world_close_for_limit_locked(world);
    else {
        LbTask *task=world->engine->tasks,*next;
        while(task) {
            next=task->next;
            if(task->world==world && task->status==LB_TASK_PENDING && task->process->work_birth==budget)
                finish_task(task,LB_TASK_CANCELLED);
            task=next;
        }
    }
    lb_work_budget_release(budget);
}
static void step(LbTask *task)
{
    /* Scheduling bounds are separate from stable causal invocation credit. */
    LbWorld *world=task->world; LbAllocDomain *domain=task->domain;
    TaskFailure limited=NO_FAILURE; int denied=0;
    lb_alloc_domain_begin_operation(domain);
    lb_work_domain_borrow(domain,task->process->work_active);
    LbProcessStatus result=lb_process_run(task->process,1024,128);
    if(lb_alloc_domain_failure(domain)==LB_ALLOC_LIMIT || lb_alloc_domain_failure(domain)==LB_ALLOC_NO_MEMORY) {
        task->process->status=LB_PROCESS_NO_MEMORY; result=LB_PROCESS_NO_MEMORY;
    }
    if(lb_alloc_domain_failure(domain)==LB_ALLOC_WORK_LIMIT) {
        task->process->status=LB_PROCESS_WORK_LIMIT; result=LB_PROCESS_WORK_LIMIT;
    }
    LbWorkBudget *owner=domain->failed_work?domain->failed_work:task->process->work_active;
    if(lb_work_budget_retain(owner)!=LB_ALLOC_OK) abort();
    switch(result) {
    case LB_PROCESS_YIELDED: break;
    case LB_PROCESS_DONE: limited=finish_task(task,LB_TASK_DONE); break;
    case LB_PROCESS_EXCEPTION: limited=finish_task(task,LB_TASK_EXCEPTION); break;
    case LB_PROCESS_NO_MEMORY:
        limited=finish_task(task,lb_alloc_domain_failure(domain)==LB_ALLOC_LIMIT?LB_TASK_MEMORY_LIMIT:LB_TASK_NO_MEMORY); break;
    case LB_PROCESS_LIMIT: limited=finish_task(task,LB_TASK_LIMIT); break;
    case LB_PROCESS_WORK_LIMIT: limited=finish_task(task,LB_TASK_WORK_LIMIT); break;
    case LB_PROCESS_CAPABILITY_DENIED:
        assert(task->process->capability_failure);
        if(owner && !owner->capability_failure) owner->capability_failure=task->process->capability_failure;
        denied=1;
        limited=finish_task(task,LB_TASK_CAPABILITY_DENIED); break;
    /* Invalid runtime state is a task refusal, never a reason to abort the
     * shared Engine worker and every other isolate using it. */
    case LB_PROCESS_INVALID: case LB_PROCESS_REFUSED:
        limited=finish_task(task,LB_TASK_REFUSED); break;
    case LB_PROCESS_ENVIRONMENT: limited=finish_task(task,LB_TASK_ENVIRONMENT); break;
    default: abort();
    }
    if(domain->failed_work && owner!=domain->failed_work) {
        LbWorkBudget *causal=domain->failed_work;
        if(lb_work_budget_retain(causal)!=LB_ALLOC_OK) abort();
        lb_work_budget_release(owner); owner=causal;
    }
    lb_work_domain_borrow(domain,NULL);
    lb_work_domain_clear_failure(domain);
    if(limited) {
        LbTaskStatus cause=limited==WORK_FAILURE?LB_TASK_WORK_LIMIT:
            limited==ALLOCATION_FAILURE?LB_TASK_NO_MEMORY:LB_TASK_MEMORY_LIMIT;
        if(owner) lb_task_cancel_family_locked(owner,cause);
        else if(world) lb_world_close_for_limit_locked(world);
    } else if(denied) {
        if(owner) lb_task_cancel_family_locked(owner,LB_TASK_CAPABILITY_DENIED);
        else if(world) lb_world_close_for_limit_locked(world);
    }
    lb_work_budget_release(owner);
}
static void *worker_main(void *context)
{
    LbEngine *engine=context;
    checked(pthread_mutex_lock(&engine->mutex));
    while(!engine->worker_stop) {
        lb_timer_poll_engine(engine);
        int retired=lb_world_retire_step(engine);
        LbTask *task=runnable(engine);
        if(!task && !retired) {
            int timed=0;
            for(LbTask *pending=engine->tasks;pending;pending=pending->next)
                if(pending->status==LB_TASK_PENDING && (pending->process->flags&F_INSLPQUEUE)) timed=1;
            if(lb_timer_engine_pending(engine)) timed=1;
            if(timed) {
                struct timespec until;
                if(clock_gettime(CLOCK_REALTIME,&until)) {
                    for(LbTask *pending=engine->tasks;pending;pending=pending->next)
                        if(pending->status==LB_TASK_PENDING && (pending->process->flags&F_INSLPQUEUE)) {
                            pending->process->status=LB_PROCESS_ENVIRONMENT; pending->process->waiting=0;
                        }
                    continue;
                }
                until.tv_nsec+=1000000;
                if(until.tv_nsec>=1000000000) { ++until.tv_sec; until.tv_nsec-=1000000000; }
                int result=pthread_cond_timedwait(&engine->wake,&engine->mutex,&until);
                if(result!=ETIMEDOUT) checked(result);
            } else checked(pthread_cond_wait(&engine->wake,&engine->mutex));
            continue;
        }
        if(task) {
            engine->cursor=task->next?task->next:engine->tasks;
            step(task);
        }
        /* No task/process pointer is used across this unlocked dispatch boundary.
         * Host cancellation/removal can now retire it physically. */
        checked(pthread_mutex_unlock(&engine->mutex));
        sched_yield(); /* host control gets an opportunity between bounded chunks */
        checked(pthread_mutex_lock(&engine->mutex));
    }
    checked(pthread_mutex_unlock(&engine->mutex)); return NULL;
}
static LbEngineStatus create_locked(LbEngine *engine,const LbCodeEntry *entry,
                                    const void *bytes,size_t size,LbCall *completion,LbTask **out)
{
    LbTask *task;
    void *memory;
    LbProcessStatus process_status;
    LbEngineStatus status=LB_ENGINE_NO_MEMORY;
    if(!out) return LB_ENGINE_INVALID;
    *out=NULL;
    if(!engine || !entry || entry->module->space->engine!=engine ||
       entry->entry->info.mfa.arity!=1 || size>LB_MAX_BINARY_BYTES || (size && !bytes)) return LB_ENGINE_INVALID;
    LbAllocDomain *domain=entry->module->space->domain;
    if(lb_alloc_domain_allocate(domain,sizeof(*task),&memory)!=LB_ALLOC_OK) return LB_ENGINE_NO_MEMORY;
    task=memory; memset(task,0,sizeof(*task)); task->engine=engine; task->domain=domain; task->status=LB_TASK_PENDING;
    task->completion=completion; task->world=completion?completion->world:NULL;
    LbWorkProcessLease lease;
    if(lb_work_process_prepare(completion?completion->budget:NULL,&lease)!=LB_ALLOC_OK) {
        lb_release(domain,task); return LB_ENGINE_NO_MEMORY;
    }
    process_status=lb_process_create_binary(entry,bytes,size,64,&task->process);
    if(process_status!=LB_PROCESS_READY) {
        status=process_status==LB_PROCESS_NO_MEMORY?LB_ENGINE_NO_MEMORY:LB_ENGINE_INVALID;
        lb_work_process_abort(&lease); lb_release(domain,task); return status;
    }
    lb_work_process_publish(task->process,&lease);
    if(!engine->worker_started) {
        if(pthread_create(&engine->worker,NULL,worker_main,engine)) {
            lb_process_destroy(task->process); lb_release(domain,task); return LB_ENGINE_NO_MEMORY;
        }
        engine->worker_started=1;
    }
    /* No fallible work after publication; the worker cannot see partial state. */
    task->process->world=task->world;
    task->next=engine->tasks; engine->tasks=task;
    checked(pthread_cond_signal(&engine->wake)); *out=task; return LB_ENGINE_OK;
}
LbEngineStatus lb_task_create_binary(LbEngine *engine,const LbCodeEntry *entry,
                                     const void *bytes,size_t size,LbTask **out)
{
    LbEngineStatus status;
    if(!out) return LB_ENGINE_INVALID;
    *out=NULL; if(!engine) return LB_ENGINE_INVALID;
    if(!lb_engine_is_open(engine)) return LB_ENGINE_CLOSED;
    lb_engine_enter(engine); status=create_locked(engine,entry,bytes,size,NULL,out);
    lb_engine_leave(engine); return status;
}
LbEngineStatus lb_task_create_call_locked(LbEngine *engine,const LbCodeEntry *entry,
                                         LbBytesView input,LbCall *call,LbTask **out)
{
    assert(engine->control_borrow && call->world->engine==engine);
    return create_locked(engine,entry,input.data,input.size,call,out);
}
/* Internal serialized spawn publication, not a host interpreter or a native
 * import admission. The real spawn effect must supply its parent/world and
 * prepare monitors/signals before making this runnable boundary available. */
typedef enum { SPAWN_CHILD, SPAWN_FROM_RETAINED_FUN } SpawnOrigin;
typedef enum { SPAWN_FUN_ENTRY, SPAWN_MFA_ENTRY } SpawnEntryKind;
typedef struct { SpawnEntryKind kind; union { Eterm fun; LbSpawnMfa mfa; } value; } SpawnEntry;
static LbProcessStatus spawn_locked(LbWorld *world,LbProcess *parent,const SpawnEntry *entry,LbMonitor *monitor,LbLink *link,SpawnOrigin origin,Eterm *out)
{
    if(!out) return LB_PROCESS_INVALID; *out=THE_NON_VALUE;
    if(!world || !parent || world->phase!=LB_WORLD_RUNNING ||
       parent->entry_module->space!=world->space || !world->engine->worker_started) return LB_PROCESS_REFUSED;
    Eterm leader=NIL;
    if(origin==SPAWN_CHILD) {
        LbProcessStatus status=lb_process_group_leader(parent,&leader);
        if(status!=LB_PROCESS_READY) return status;
    }
    LbEngine *engine=world->engine; void *memory;
    if(lb_alloc_domain_allocate(&world->domain,sizeof(LbTask),&memory)!=LB_ALLOC_OK) return LB_PROCESS_NO_MEMORY;
    LbTask *task=memory; memset(task,0,sizeof(*task));
    task->engine=engine; task->domain=&world->domain; task->world=world; task->background=1; task->status=LB_TASK_PENDING;
    LbWorkProcessLease lease;
    if(lb_work_process_prepare(parent->work_active,&lease)!=LB_ALLOC_OK) {
        lb_release(task->domain,task); return LB_PROCESS_LIMIT;
    }
    LbProcessStatus status;
    switch(entry->kind) {
    case SPAWN_FUN_ENTRY: status=lb_process_create_fun(parent,entry->value.fun,64,&task->process); break;
    case SPAWN_MFA_ENTRY: status=lb_process_create_mfa(parent,&entry->value.mfa,64,&task->process); break;
    default: abort();
    }
    if(status!=LB_PROCESS_READY) { lb_work_process_abort(&lease); lb_release(task->domain,task); return status; }
    lb_work_process_publish(task->process,&lease);
    *out=task->process->identity.id;
    task->process->world=world; task->process->group_leader=leader;
    if(link) lb_link_publish(link,task->process);
    if(monitor) lb_monitor_publish(monitor,task->process);
    task->next=engine->tasks; engine->tasks=task;
    checked(pthread_cond_signal(&engine->wake)); return LB_PROCESS_READY;
}
LbProcessStatus lb_task_spawn_fun_monitored_locked(LbWorld *world,LbProcess *parent,Eterm fun,LbMonitor *monitor,Eterm *out)
{
    SpawnEntry entry={.kind=SPAWN_FUN_ENTRY,.value.fun=fun};
    return spawn_locked(world,parent,&entry,monitor,NULL,SPAWN_CHILD,out);
}
LbProcessStatus lb_task_spawn_mfa_monitored_locked(LbWorld *world,LbProcess *parent,const LbSpawnMfa *mfa,LbMonitor *monitor,Eterm *out)
{
    SpawnEntry entry={.kind=SPAWN_MFA_ENTRY,.value.mfa=*mfa};
    return spawn_locked(world,parent,&entry,monitor,NULL,SPAWN_CHILD,out);
}
LbProcessStatus lb_task_spawn_fun_linked_locked(LbWorld *world,LbProcess *parent,Eterm fun,LbLink *link,Eterm *out)
{
    SpawnEntry entry={.kind=SPAWN_FUN_ENTRY,.value.fun=fun};
    return spawn_locked(world,parent,&entry,NULL,link,SPAWN_CHILD,out);
}
LbProcessStatus lb_task_spawn_mfa_linked_locked(LbWorld *world,LbProcess *parent,const LbSpawnMfa *mfa,LbLink *link,Eterm *out)
{
    SpawnEntry entry={.kind=SPAWN_MFA_ENTRY,.value.mfa=*mfa};
    return spawn_locked(world,parent,&entry,NULL,link,SPAWN_CHILD,out);
}
LbProcessStatus lb_task_spawn_mfa_linked_monitored_locked(LbWorld *world,LbProcess *parent,const LbSpawnMfa *mfa,LbMonitor *monitor,LbLink *link,Eterm *out)
{
    SpawnEntry entry={.kind=SPAWN_MFA_ENTRY,.value.mfa=*mfa};
    return spawn_locked(world,parent,&entry,monitor,link,SPAWN_CHILD,out);
}
LbProcessStatus lb_task_spawn_fun_locked(LbWorld *world,LbProcess *parent,Eterm fun,Eterm *out)
{ return lb_task_spawn_fun_monitored_locked(world,parent,fun,NULL,out); }
LbProcessStatus lb_task_spawn_mfa_locked(LbWorld *world,LbProcess *parent,const LbSpawnMfa *mfa,Eterm *out)
{
    SpawnEntry entry={.kind=SPAWN_MFA_ENTRY,.value.mfa=*mfa};
    return spawn_locked(world,parent,&entry,NULL,NULL,SPAWN_CHILD,out);
}
LbProcessStatus lb_task_spawn_result_locked(LbWorld *world,LbTask *source,Eterm *out)
{
    if(!out) return LB_PROCESS_INVALID; *out=THE_NON_VALUE;
    if(!source || source->status!=LB_TASK_DONE || !source->process ||
       !world || source->engine!=world->engine) return LB_PROCESS_REFUSED;
    /* This private component seam launches a fresh root from retained closure
     * storage, not an Erlang child of an already terminated process. */
    SpawnEntry entry={.kind=SPAWN_FUN_ENTRY,.value.fun=lb_process_result(source->process)};
    return spawn_locked(world,source->process,&entry,NULL,NULL,SPAWN_FROM_RETAINED_FUN,out);
}
void lb_task_cancel_world_children_locked(LbWorld *world)
{
    LbTask *task=world->engine->tasks,*next;
    while(task) {
        next=task->next;
        if(task->background && task->world==world) lb_task_cancel_locked(task);
        task=next;
    }
}
LbTaskStatus lb_task_status(LbTask *task)
{
    LbTaskStatus status;
    lb_engine_enter(task->engine); status=task->status; lb_engine_leave(task->engine); return status;
}
LbTaskObservation lb_task_observe(LbTask *task)
{
    lb_engine_enter(task->engine);
    LbTaskObservation value={task->status,task->process?task->process->collections:0,
        task->process?task->process->waiting:0};
    lb_engine_leave(task->engine); return value;
}
Eterm lb_task_result(LbTask *task)
{
    Eterm result=THE_NON_VALUE;
    lb_engine_enter(task->engine);
    if(task->status==LB_TASK_DONE && task->process) result=lb_process_result(task->process);
    lb_engine_leave(task->engine); return result;
}
Eterm lb_task_exception(LbTask *task)
{
    Eterm result=THE_NON_VALUE;
    lb_engine_enter(task->engine);
    if(task->status==LB_TASK_EXCEPTION && task->process) result=lb_process_exception(task->process);
    lb_engine_leave(task->engine); return result;
}
void lb_task_cancel_locked(LbTask *task)
{
    if(task->status==LB_TASK_PENDING) finish_task(task,LB_TASK_CANCELLED);
}
void lb_task_cancel(LbTask *task)
{
    LbEngine *engine=task->engine;
    lb_engine_enter(engine); lb_task_cancel_locked(task); lb_engine_leave(engine);
}
void lb_task_release_locked(LbTask *task)
{
    LbEngine *engine;
    LbTask **link;
    if(!task) return;
    engine=task->engine;
    if(task->completion && task->status==LB_TASK_PENDING) abort();
    for(link=&engine->tasks;*link && *link!=task;link=&(*link)->next) {}
    if(!*link) abort();
    *link=task->next;
    if(engine->cursor==task) engine->cursor=task->next?task->next:engine->tasks;
    if(task->process) lb_process_destroy(task->process);
    lb_release(task->domain,task);
}
void lb_task_release(LbTask *task)
{
    LbEngine *engine;
    if(!task) return;
    engine=task->engine; lb_engine_enter(engine);
    lb_task_release_locked(task); lb_engine_leave(engine);
}
