/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Test-only POSIX failure interposition delegates every successful operation to
 * the actual thread library. There is no mock worker or alternate executor.
 */
#define _GNU_SOURCE
#define _DARWIN_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "executor.h"
#include "code_internal.h"
#include "binary.h"
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdatomic.h>
#include <time.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"CHECK %s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while(0)
static unsigned fail_native,mutexes,conditions,workers;
static void native_function(void *destination,size_t size,const char *name)
{
    void *function=dlsym(RTLD_NEXT,name);
    CHECK(function && size==sizeof(function)); memcpy(destination,&function,size);
}
int pthread_mutex_init(pthread_mutex_t *mutex,const pthread_mutexattr_t *attr)
{
    int (*actual)(pthread_mutex_t *,const pthread_mutexattr_t *); int status;
    if(fail_native==1) return EAGAIN;
    native_function(&actual,sizeof(actual),"pthread_mutex_init"); status=actual(mutex,attr);
    if(!status) ++mutexes; return status;
}
int pthread_mutex_destroy(pthread_mutex_t *mutex)
{
    int (*actual)(pthread_mutex_t *); int status;
    native_function(&actual,sizeof(actual),"pthread_mutex_destroy"); status=actual(mutex);
    if(!status) { CHECK(mutexes); --mutexes; } return status;
}
int pthread_cond_init(pthread_cond_t *condition,const pthread_condattr_t *attr)
{
    int (*actual)(pthread_cond_t *,const pthread_condattr_t *); int status;
    if(fail_native==2) return EAGAIN;
    native_function(&actual,sizeof(actual),"pthread_cond_init"); status=actual(condition,attr);
    if(!status) ++conditions; return status;
}
int pthread_cond_destroy(pthread_cond_t *condition)
{
    int (*actual)(pthread_cond_t *); int status;
    native_function(&actual,sizeof(actual),"pthread_cond_destroy"); status=actual(condition);
    if(!status) { CHECK(conditions); --conditions; } return status;
}
int pthread_create(pthread_t *thread,const pthread_attr_t *attr,void *(*start)(void *),void *context)
{
    int (*actual)(pthread_t *,const pthread_attr_t *,void *(*)(void *),void *); int status;
    if(fail_native==3) return EAGAIN;
    native_function(&actual,sizeof(actual),"pthread_create"); status=actual(thread,attr,start,context);
    if(!status) ++workers; return status;
}
int pthread_join(pthread_t thread,void **result)
{
    int (*actual)(pthread_t,void **); int status;
    native_function(&actual,sizeof(actual),"pthread_join"); status=actual(thread,result);
    if(!status) { CHECK(workers); --workers; } return status;
}
typedef struct {
    size_t calls,fail,live,bytes;
    pthread_t host;
    _Atomic size_t worker_allocations;
} Memory;
typedef union { max_align_t alignment; size_t size; } Header;
static void *allocate(void *context,size_t size)
{
    Memory *m=context; Header *h;
    if(!pthread_equal(pthread_self(),m->host)) atomic_fetch_add(&m->worker_allocations,1);
    if(++m->calls==m->fail) return NULL;
    h=malloc(sizeof(*h)+size); if(!h) return NULL;
    h->size=size; ++m->live; m->bytes+=size; return h+1;
}
static void release(void *context,void *pointer)
{
    Memory *m=context; Header *h=(Header *)pointer-1;
    CHECK(m->live && m->bytes>=h->size); --m->live; m->bytes-=h->size;
    memset(pointer,0xa5,h->size); free(h);
}
static void native_failure(void)
{
    Memory m={0}; LbSystemAllocator cb={&m,allocate,release}; LbEngine *engine;
    unsigned stage; m.host=pthread_self();
    for(stage=1;stage<=2;++stage) {
        fail_native=stage;
        CHECK(lb_engine_create(&cb,&engine)==LB_ENGINE_NO_MEMORY && !engine);
        CHECK(!m.live && !m.bytes && !mutexes && !conditions && !workers);
        fail_native=0; CHECK(lb_engine_create(&cb,&engine)==LB_ENGINE_OK);
        CHECK(mutexes==1 && conditions==1 && !workers);
        CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK && !mutexes && !conditions);
        lb_engine_release(engine); CHECK(!m.live && !m.bytes);
    }
    puts("CORE_EXECUTOR_COMPONENT_OK mutex_condition_failure_retry=true real_native_cleanup=true execution=false");
}
static LbCodeModule *load(LbCodeSpace *space,const char *path)
{
    FILE *f=fopen(path,"rb"); long length; unsigned char *data;
    LbCodeModule *module; LbBeamError error; LbCodeStatus status;
    CHECK(f && !fseek(f,0,SEEK_END)); length=ftell(f);
    CHECK(length>0 && length<8*1024*1024 && !fseek(f,0,SEEK_SET));
    data=malloc((size_t)length); CHECK(data && fread(data,1,(size_t)length,f)==(size_t)length && !fclose(f));
    status=lb_code_load(space,data,(size_t)length,&module,&error); free(data);
    if(status!=LB_CODE_OK) fprintf(stderr,"load=%d stage=%s offset=%zu\n",status,error.stage,error.offset);
    CHECK(status==LB_CODE_OK); return module;
}
static LbCodeEntry *entry(LbCodeSpace *space,LbCodeModule *module,const char *name)
{
    Eterm function; LbCodeEntry *result;
    CHECK(lb_atoms_find(space->atoms,name,strlen(name),&function)==LB_ATOM_OK);
    CHECK(lb_code_entry_acquire(space,module->name,function,1,&result)==LB_CODE_OK); return result;
}
static void finish(LbTask *task,LbTaskStatus expected)
{
    size_t i;
    const struct timespec delay={0,1000000};
    /* Polling never executes BEAM. The worker must make independent progress. */
    for(i=0;i<5000 && lb_task_status(task)==LB_TASK_PENDING;++i) nanosleep(&delay,NULL);
    CHECK(lb_task_status(task)==expected);
}
static void execute(const char *path,int retain_cancelled)
{
    Memory m={0}; LbSystemAllocator cb={&m,allocate,release}; LbEngine *engine;
    LbCodeSpace *space,*peer; LbCodeModule *module,*other;
    LbCodeEntry *identity,*loop,*info,*crash,*peer_identity;
    LbTask *task,*busy,*answer; size_t calls,live,bytes,i;
    unsigned char input[65]; LbBitstringView view;
    m.host=pthread_self(); memset(input,0x5a,sizeof(input));
    CHECK(lb_engine_create(&cb,&engine)==LB_ENGINE_OK && lb_code_space_create(engine,&space)==LB_CODE_OK);
    CHECK(lb_code_space_create(engine,&peer)==LB_CODE_OK && !workers);
    module=load(space,path); other=load(peer,path);
    identity=entry(space,module,"identity"); loop=entry(space,module,"loop");
    info=entry(space,module,"info"); crash=entry(space,module,"crash"); peer_identity=entry(peer,other,"identity");
    live=m.live; bytes=m.bytes; calls=m.calls; fail_native=3;
    CHECK(lb_task_create_binary(engine,identity,input,sizeof(input),&task)==LB_ENGINE_NO_MEMORY && !task);
    calls=m.calls-calls; CHECK(!workers && !engine->worker_started && m.live==live && m.bytes==bytes);
    fail_native=0;
    for(i=1;i<=calls;++i) {
        m.fail=m.calls+i;
        CHECK(lb_task_create_binary(engine,identity,input,sizeof(input),&task)==LB_ENGINE_NO_MEMORY && !task);
        CHECK(m.live==live && m.bytes==bytes); m.fail=0;
        CHECK(lb_task_create_binary(engine,identity,input,sizeof(input),&task)==LB_ENGINE_OK);
        finish(task,LB_TASK_DONE); CHECK(lb_bitstring_view(lb_task_result(task),&view));
        CHECK(view.bit_size==sizeof(input)*8 && !memcmp(view.data,input,sizeof(input)));
        lb_task_release(task); CHECK(m.live==live && m.bytes==bytes && workers==1);
    }
    /* Empty-input construction takes task/control/heap allocations. Fail the
     * next, real worker-side collection, then retire and retry on the same worker. */
    m.fail=m.calls+4;
    CHECK(lb_task_create_binary(engine,info,NULL,0,&task)==LB_ENGINE_OK);
    finish(task,LB_TASK_NO_MEMORY); lb_task_release(task); m.fail=0;
    CHECK(m.live==live && m.bytes==bytes);
    CHECK(lb_task_create_binary(engine,info,NULL,0,&task)==LB_ENGINE_OK);
    finish(task,LB_TASK_DONE); lb_task_release(task);
    CHECK(m.live==live && m.bytes==bytes);
    {
        LbEngine *wrong; CHECK(lb_engine_create(NULL,&wrong)==LB_ENGINE_OK);
        CHECK(lb_task_create_binary(wrong,identity,NULL,0,&task)==LB_ENGINE_INVALID && !task);
        lb_engine_release(wrong);
    }
    CHECK(lb_task_create_binary(engine,loop,NULL,0,&busy)==LB_ENGINE_OK);
    CHECK(lb_task_create_binary(engine,peer_identity,input,sizeof(input),&answer)==LB_ENGINE_OK);
    memset(input,0,sizeof(input)); finish(answer,LB_TASK_DONE);
    CHECK(lb_bitstring_view(lb_task_result(answer),&view) && view.data[0]==0x5a);
    lb_task_cancel(answer); CHECK(lb_task_status(answer)==LB_TASK_DONE);
    CHECK(lb_task_status(busy)==LB_TASK_PENDING && workers==1);
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_BUSY);
    lb_task_release(answer);
    CHECK(lb_task_create_binary(engine,info,NULL,0,&task)==LB_ENGINE_OK);
    finish(task,LB_TASK_DONE); CHECK(is_list(lb_task_result(task)) && atomic_load(&m.worker_allocations)>0);
    lb_task_release(task);
    CHECK(lb_task_create_binary(engine,crash,NULL,0,&task)==LB_ENGINE_OK);
    finish(task,LB_TASK_EXCEPTION); CHECK(lb_task_exception(task)!=THE_NON_VALUE); lb_task_release(task);
    /* Parent owner drop does not stop the worker or invalidate retained tasks. */
    CHECK(lb_task_create_binary(engine,peer_identity,input,sizeof(input),&answer)==LB_ENGINE_OK);
    lb_engine_enter(engine); lb_code_entry_release(peer_identity); peer_identity=NULL;
    CHECK(lb_code_unload(other)==LB_CODE_BUSY); /* task/process, not the entry, retains it */
    lb_engine_leave(engine);
    lb_engine_release(engine); finish(answer,LB_TASK_DONE); lb_task_release(answer);
    lb_task_cancel(busy); CHECK(lb_task_status(busy)==LB_TASK_CANCELLED); lb_task_cancel(busy);
    if(!retain_cancelled) lb_task_release(busy);
    lb_engine_enter(engine);
    lb_code_entry_release(identity); lb_code_entry_release(loop); lb_code_entry_release(info);
    lb_code_entry_release(crash); lb_code_entry_release(peer_identity);
    CHECK(lb_code_unload(module)==LB_CODE_OK && lb_code_space_destroy(space)==LB_CODE_OK);
    CHECK(lb_code_unload(other)==LB_CODE_OK && lb_code_space_destroy(peer)==LB_CODE_OK);
    /* The control borrow defers final Engine destruction until after unlock. */
    lb_engine_leave(engine);
    if(retain_cancelled) {
        CHECK(workers==1 && mutexes==1 && conditions==1);
        CHECK(lb_task_status(busy)==LB_TASK_CANCELLED); lb_task_release(busy);
    }
    CHECK(!m.live && !m.bytes && !mutexes && !conditions && !workers);
    printf("CORE_EXECUTOR_OK real_worker=true shared_between_spaces=true failure_prefixes=%zu copied_input=true native_gc=true worker_oom_retry=true cancellation=true owner_drop_join=true public_calls=false\n",calls);
}
int main(int argc,char **argv)
{
    native_failure();
    if(argc==2) { execute(argv[1],0); execute(argv[1],1); }
    CHECK(argc==1 || argc==2); return 0;
}
