/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Joinable Engine worker driving the existing generated BEAM interpreter.
 * No independent VM, tenant TLS, ambient native callbacks or process startup.
 */
#define _POSIX_C_SOURCE 200809L
#include "executor.h"
#include "receive.h"
#include <time.h>
#include <errno.h>
#include "code_internal.h"
#include "binary.h"
#include "world_internal.h"
#include "process_internal.h"
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
static int finish_task(LbTask *task,LbTaskStatus status)
{
    LbCall *call=task->completion;
    int memory_limited=status==LB_TASK_MEMORY_LIMIT;
    /* Timer/infrastructure failure can bypass the interpreter stop label.
     * Propagate that failure through owned monitor halves too; private world
     * cancellation instead detaches them during destruction without DOWN. */
    if(status!=LB_TASK_CANCELLED) {
        lb_monitor_exit(task->process,1);
        if(lb_alloc_domain_failure(task->domain)==LB_ALLOC_LIMIT) {
            status=LB_TASK_MEMORY_LIMIT; memory_limited=1;
        }
    }
    task->status=status;
    if(call) {
        lb_call_complete(call,task->process,status);
        if(call->status==LB_WORLD_MEMORY_LIMIT) { task->status=LB_TASK_MEMORY_LIMIT; memory_limited=1; }
        lb_process_destroy(task->process); task->process=NULL;
        if(!call->owner_live) {
            lb_task_release_locked(task); call->task=NULL; lb_call_discard_completed(call);
        }
    } else if(task->background) {
        /* No host completion handle owns a spawned process. Logical exit has
         * already removed its PID; physically retire its task/code/heap now. */
        lb_task_release_locked(task);
    } else if(status==LB_TASK_CANCELLED) {
        lb_process_destroy(task->process); task->process=NULL;
    }
    return memory_limited;
}
static int solitary_invocation(const LbTask *task)
{
    if(!task->completion) return 0;
    for(LbTask *other=task->engine->tasks;other;other=other->next)
        if(other!=task && other->world==task->world && other->status==LB_TASK_PENDING) return 0;
    return 1;
}
static void step(LbTask *task)
{
    /* Both bounds are real interpreter safepoints, not a replacement evaluator.
     * The Engine lock serializes the existing allocation/loader/GC domains. */
    LbWorld *world=task->world;
    int contained=solitary_invocation(task),memory_limited=0;
    lb_alloc_domain_begin_operation(task->domain);
    LbProcessStatus result=lb_process_run(task->process,1024,128);
    if(lb_alloc_domain_failure(task->domain)==LB_ALLOC_LIMIT) {
        task->process->status=LB_PROCESS_NO_MEMORY; result=LB_PROCESS_NO_MEMORY;
    }
    switch(result) {
    case LB_PROCESS_YIELDED: break;
    case LB_PROCESS_DONE: memory_limited=finish_task(task,LB_TASK_DONE); break;
    case LB_PROCESS_EXCEPTION: memory_limited=finish_task(task,LB_TASK_EXCEPTION); break;
    case LB_PROCESS_NO_MEMORY: {
        int limited=lb_alloc_domain_failure(task->domain)==LB_ALLOC_LIMIT;
        memory_limited=finish_task(task,limited?LB_TASK_MEMORY_LIMIT:LB_TASK_NO_MEMORY);
        break;
    }
    case LB_PROCESS_LIMIT: memory_limited=finish_task(task,LB_TASK_LIMIT); break;
    case LB_PROCESS_REFUSED: memory_limited=finish_task(task,LB_TASK_REFUSED); break;
    case LB_PROCESS_ENVIRONMENT: memory_limited=finish_task(task,LB_TASK_ENVIRONMENT); break;
    default: abort(); /* published tasks contain runnable, verified processes */
    }
    if(memory_limited && world && !contained) lb_world_close_for_limit_locked(world);
}
static void *worker_main(void *context)
{
    LbEngine *engine=context;
    checked(pthread_mutex_lock(&engine->mutex));
    while(!engine->worker_stop) {
        int retired=lb_world_retire_step(engine);
        LbTask *task=runnable(engine);
        if(!task && !retired) {
            int timed=0;
            for(LbTask *pending=engine->tasks;pending;pending=pending->next)
                if(pending->status==LB_TASK_PENDING && (pending->process->flags&F_INSLPQUEUE)) timed=1;
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
    process_status=lb_process_create_binary(entry,bytes,size,64,&task->process);
    if(process_status!=LB_PROCESS_READY) {
        status=process_status==LB_PROCESS_NO_MEMORY?LB_ENGINE_NO_MEMORY:LB_ENGINE_INVALID;
        lb_release(domain,task); return status;
    }
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
LbProcessStatus lb_task_spawn_fun_monitored_locked(LbWorld *world,LbProcess *parent,Eterm fun,LbMonitor *monitor,Eterm *out)
{
    if(!out) return LB_PROCESS_INVALID; *out=THE_NON_VALUE;
    if(!world || !parent || world->phase!=LB_WORLD_RUNNING ||
       parent->entry_module->space!=world->space || !world->engine->worker_started) return LB_PROCESS_REFUSED;
    LbEngine *engine=world->engine; void *memory;
    if(lb_alloc_domain_allocate(&world->domain,sizeof(LbTask),&memory)!=LB_ALLOC_OK) return LB_PROCESS_NO_MEMORY;
    LbTask *task=memory; memset(task,0,sizeof(*task));
    task->engine=engine; task->domain=&world->domain; task->world=world; task->background=1; task->status=LB_TASK_PENDING;
    LbProcessStatus status=lb_process_create_fun(parent,fun,64,&task->process);
    if(status!=LB_PROCESS_READY) { lb_release(task->domain,task); return status; }
    *out=task->process->identity.id;
    task->process->world=world;
    if(monitor) lb_monitor_publish(monitor,task->process);
    task->next=engine->tasks; engine->tasks=task;
    checked(pthread_cond_signal(&engine->wake)); return LB_PROCESS_READY;
}
LbProcessStatus lb_task_spawn_fun_locked(LbWorld *world,LbProcess *parent,Eterm fun,Eterm *out)
{ return lb_task_spawn_fun_monitored_locked(world,parent,fun,NULL,out); }
LbProcessStatus lb_task_spawn_result_locked(LbWorld *world,LbTask *source,Eterm *out)
{
    if(!out) return LB_PROCESS_INVALID; *out=THE_NON_VALUE;
    if(!source || source->status!=LB_TASK_DONE || !source->process ||
       !world || source->engine!=world->engine) return LB_PROCESS_REFUSED;
    return lb_task_spawn_fun_locked(world,source->process,lb_process_result(source->process),out);
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
