/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Joinable Engine worker driving the existing generated BEAM interpreter.
 * No independent VM, tenant TLS, ambient native callbacks or process startup.
 */
#include "executor.h"
#include "code_internal.h"
#include "binary.h"
#include "world_internal.h"
#include <sched.h>

struct LbTask {
    LbEngine *engine;
    LbTask *next;
    LbProcess *process;
    LbCall *completion;
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
        if(task->status==LB_TASK_PENDING) return task;
        task=task->next?task->next:engine->tasks;
    } while(task!=first);
    return NULL;
}
static void finish_task(LbTask *task,LbTaskStatus status)
{
    LbCall *call=task->completion;
    task->status=status;
    if(call) {
        lb_call_complete(call,task->process,status);
        lb_process_destroy(task->process); task->process=NULL;
        if(!call->owner_live) {
            lb_task_release_locked(task); call->task=NULL; lb_call_discard_completed(call);
        }
    } else if(status==LB_TASK_CANCELLED) {
        lb_process_destroy(task->process); task->process=NULL;
    }
}
static void step(LbTask *task)
{
    /* Both bounds are real interpreter safepoints, not a replacement evaluator.
     * The Engine lock serializes the existing allocation/loader/GC domains. */
    switch(lb_process_run(task->process,1024,128)) {
    case LB_PROCESS_YIELDED: break;
    case LB_PROCESS_DONE: finish_task(task,LB_TASK_DONE); break;
    case LB_PROCESS_EXCEPTION: finish_task(task,LB_TASK_EXCEPTION); break;
    case LB_PROCESS_NO_MEMORY: finish_task(task,LB_TASK_NO_MEMORY); break;
    default: abort(); /* published tasks contain runnable, verified processes */
    }
}
static void *worker_main(void *context)
{
    LbEngine *engine=context;
    checked(pthread_mutex_lock(&engine->mutex));
    while(!engine->worker_stop) {
        int retired=lb_world_retire_step(engine);
        LbTask *task=runnable(engine);
        if(!task && !retired) { checked(pthread_cond_wait(&engine->wake,&engine->mutex)); continue; }
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
    if(lb_alloc_domain_allocate(engine->domain,sizeof(*task),&memory)!=LB_ALLOC_OK) return LB_ENGINE_NO_MEMORY;
    task=memory; memset(task,0,sizeof(*task)); task->engine=engine; task->status=LB_TASK_PENDING;
    task->completion=completion;
    process_status=lb_process_create_binary(entry,bytes,size,64,&task->process);
    if(process_status!=LB_PROCESS_READY) {
        status=process_status==LB_PROCESS_NO_MEMORY?LB_ENGINE_NO_MEMORY:LB_ENGINE_INVALID;
        lb_release(engine->domain,task); return status;
    }
    if(!engine->worker_started) {
        if(pthread_create(&engine->worker,NULL,worker_main,engine)) {
            lb_process_destroy(task->process); lb_release(engine->domain,task); return LB_ENGINE_NO_MEMORY;
        }
        engine->worker_started=1;
    }
    /* No fallible work after publication; the worker cannot see partial state. */
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
    lb_release(engine->domain,task);
}
void lb_task_release(LbTask *task)
{
    LbEngine *engine;
    if(!task) return;
    engine=task->engine; lb_engine_enter(engine);
    lb_task_release_locked(task); lb_engine_leave(engine);
}
