/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#define _POSIX_C_SOURCE 200809L
#include "world_internal.h"
#include "pid_table.h"
#include "process_internal.h"
#include <stdio.h>
#include <stdatomic.h>
#include <time.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"CHECK %s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while(0)
typedef struct {
    size_t live,bytes,host_calls,fail_host,capture_size;
    int capture_next;
    size_t worker_calls,fail_worker;
    pthread_t host;
} Memory;
typedef union { max_align_t alignment; size_t size; } Header;
static void *allocate(void *context,size_t size)
{
    Memory *m=context; Header *h;
    if(pthread_equal(pthread_self(),m->host)) {
        if(m->capture_next) { m->capture_size=size; m->capture_next=0; }
        if(++m->host_calls==m->fail_host) return NULL;
    } else if(++m->worker_calls==m->fail_worker) return NULL;
    h=malloc(sizeof(*h)+size); if(!h) return NULL;
    h->size=size; ++m->live; m->bytes+=size; return h+1;
}
static void release(void *context,void *pointer)
{
    Memory *m=context; Header *h=(Header *)pointer-1;
    CHECK(m->live && m->bytes>=h->size); --m->live; m->bytes-=h->size;
    memset(pointer,0xa5,h->size); free(h);
}
static LbBytesView text(const char *s) { return (LbBytesView){(const unsigned char *)s,strlen(s)}; }
static const LbBytesView empty={NULL,0};
static LbEngine *create(Memory *m)
{
    LbSystemAllocator cb={m,allocate,release}; LbEngine *engine;
    m->host=pthread_self(); CHECK(lb_engine_create(&cb,&engine)==LB_ENGINE_OK); return engine;
}
static LbWorldStatus await_call(LbCall *call,LbBytesView *view)
{
    LbWorldStatus status; size_t i; const struct timespec delay={0,1000000};
    for(i=0;i<5000;++i) {
        status=lb_call_poll(call,view); if(status!=LB_WORLD_PENDING) return status;
        nanosleep(&delay,NULL);
    }
    CHECK(0); return LB_WORLD_PENDING;
}
static void reclaimed(LbReclamation *r)
{
    size_t i; const struct timespec delay={0,1000000};
    for(i=0;i<5000 && lb_reclamation_poll(r)==LB_WORLD_PENDING;++i) nanosleep(&delay,NULL);
    CHECK(lb_reclamation_poll(r)==LB_WORLD_OK && lb_reclamation_consume(r)==LB_WORLD_OK);
    CHECK(lb_reclamation_poll(r)==LB_WORLD_CLOSED && lb_reclamation_consume(r)==LB_WORLD_CLOSED);
}
static void expect(LbCall *call,LbWorldStatus status)
{
    LbBytesView view; CHECK(await_call(call,&view)==status);
    CHECK(lb_call_consume(call)==LB_WORLD_OK && lb_call_consume(call)==LB_WORLD_CLOSED);
    CHECK(lb_call_poll(call,&view)==LB_WORLD_CLOSED && !view.data && !view.size); lb_call_release(call);
}
static void balanced(LbEngine *e,Memory *m,size_t live,size_t bytes)
{
    lb_engine_enter(e); CHECK(m->live==live && m->bytes==bytes); lb_engine_leave(e);
}
static void construction(void)
{
    Memory m={0}; LbEngine *engine=create(&m); LbWorld *world;
    size_t live=m.live,bytes=m.bytes,calls=m.host_calls,i;
    CHECK(lb_world_create(engine,&world)==LB_WORLD_OK); calls=m.host_calls-calls;
    CHECK(engine->worlds==world && world->space && !engine->worker_started);
    lb_world_release(world); balanced(engine,&m,live,bytes);
    for(i=1;i<=calls;++i) {
        m.fail_host=m.host_calls+i;
        CHECK(lb_world_create(engine,&world)==LB_WORLD_NO_MEMORY && !world);
        CHECK(!engine->worlds && !engine->spaces); balanced(engine,&m,live,bytes); m.fail_host=0;
        CHECK(lb_world_create(engine,&world)==LB_WORLD_OK); lb_world_release(world);
        balanced(engine,&m,live,bytes);
    }
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine);
    CHECK(!m.live && !m.bytes);
    printf("CORE_WORLD_COMPONENT_OK construction_prefixes=%zu retry=true physical_release=true execution=false\n",calls);
}
static LbBytesView image(const char *path)
{
    FILE *f=fopen(path,"rb"); long length; unsigned char *data;
    CHECK(f && !fseek(f,0,SEEK_END)); length=ftell(f);
    CHECK(length>0 && length<8*1024*1024 && !fseek(f,0,SEEK_SET));
    data=malloc((size_t)length); CHECK(data && fread(data,1,(size_t)length,f)==(size_t)length && !fclose(f));
    return (LbBytesView){data,(size_t)length};
}
static void admission_failures(LbBytesView code)
{
    Memory m={0}; LbEngine *engine=create(&m); LbWorld *world; LbCall *call;
    size_t live,bytes,calls,i; unsigned char input[65]={0}; LbBytesView view={input,sizeof(input)};
    CHECK(lb_world_create(engine,&world)==LB_WORLD_OK && lb_world_load(world,code)==LB_WORLD_OK);
    calls=m.host_calls;
    CHECK(lb_world_start(world,text("async_slice"),text("identity"),view,&call)==LB_WORLD_OK);
    calls=m.host_calls-calls; expect(call,LB_WORLD_OK); lb_world_release(world);
    for(i=1;i<=calls;++i) {
        CHECK(lb_world_create(engine,&world)==LB_WORLD_OK && lb_world_load(world,code)==LB_WORLD_OK);
        lb_engine_enter(engine); live=m.live; bytes=m.bytes; lb_engine_leave(engine);
        m.fail_host=m.host_calls+i;
        CHECK(lb_world_start(world,text("async_slice"),text("identity"),view,&call)==LB_WORLD_NO_MEMORY && !call);
        lb_engine_enter(engine);
        CHECK(world->phase==LB_WORLD_LOADING && !world->calls && !world->outstanding && !world->queued_bytes);
        lb_engine_leave(engine); balanced(engine,&m,live,bytes); m.fail_host=0;
        CHECK(lb_world_start(world,text("async_slice"),text("identity"),view,&call)==LB_WORLD_OK);
        expect(call,LB_WORLD_OK); lb_world_release(world);
    }
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine); CHECK(!m.live && !m.bytes);
    printf("CORE_WORLD_ADMISSION_OK failure_prefixes=%zu start_rollback=true retry=true\n",calls);
}
static void execution(LbBytesView code)
{
    Memory m={0}; LbEngine *engine=create(&m); LbWorld *a,*b,*replacement;
    LbCall *call,*loops[16],*retained,*peer; LbReclamation *stopped; LbBytesView view;
    unsigned char input[65],host_reply[65]; size_t i; memset(input,0x6b,sizeof(input));
    CHECK(lb_world_create(engine,&a)==LB_WORLD_OK && lb_world_create(engine,&b)==LB_WORLD_OK);
    CHECK(lb_world_call(a,text("async_slice"),text("identity"),empty,&call)==LB_WORLD_INVALID_STATE && !call);
    CHECK(lb_world_load(a,text("not BEAM"))==LB_WORLD_BAD_BEAM);
    CHECK(lb_world_load(a,code)==LB_WORLD_OK && lb_world_load(b,code)==LB_WORLD_OK);
    CHECK(lb_world_start(a,text("absent"),text("identity"),empty,&call)==LB_WORLD_NOT_FOUND && !call);
    CHECK(lb_world_start(a,text("async_slice"),text("identity"),(LbBytesView){input,sizeof(input)},&call)==LB_WORLD_OK);
    CHECK(lb_world_start(b,text("async_slice"),text("identity"),empty,&peer)==LB_WORLD_OK);
    memset(input,0,sizeof(input)); CHECK(await_call(call,&view)==LB_WORLD_OK && view.size==sizeof(input));
    CHECK(view.data[0]==0x6b); memcpy(host_reply,view.data,sizeof(host_reply));
    expect(call,LB_WORLD_OK); expect(peer,LB_WORLD_OK);
    CHECK(lb_world_load(a,code)==LB_WORLD_INVALID_STATE);
    CHECK(lb_world_start(a,text("async_slice"),text("identity"),empty,&call)==LB_WORLD_INVALID_STATE);
    CHECK(lb_world_call(a,text("async_slice"),text("crash"),empty,&call)==LB_WORLD_OK); expect(call,LB_WORLD_EXCEPTION);
    CHECK(lb_world_call(a,text("async_slice"),text("info"),empty,&call)==LB_WORLD_OK); expect(call,LB_WORLD_BAD_RESULT);
    /* Fail the real worker's host-output copy, not admission or guest execution. */
    lb_engine_enter(engine); m.fail_worker=m.worker_calls+1; lb_engine_leave(engine);
    CHECK(lb_world_call(a,text("async_slice"),text("identity"),(LbBytesView){input,sizeof(input)},&call)==LB_WORLD_OK);
    expect(call,LB_WORLD_NO_MEMORY);
    lb_engine_enter(engine); m.fail_worker=0; lb_engine_leave(engine);
    CHECK(lb_world_call(a,text("async_slice"),text("identity"),empty,&call)==LB_WORLD_OK); expect(call,LB_WORLD_OK);
    {
        LbCall *completed[64];
        for(i=0;i<64;++i) {
            CHECK(lb_world_call(a,text("async_slice"),text("identity"),empty,&completed[i])==LB_WORLD_OK);
            CHECK(await_call(completed[i],&view)==LB_WORLD_OK && !view.size);
        }
        CHECK(lb_world_call(a,text("async_slice"),text("identity"),empty,&call)==LB_WORLD_FULL && !call);
        for(i=0;i<64;++i) expect(completed[i],LB_WORLD_OK);
    }
    for(i=0;i<16;++i) CHECK(lb_world_call(a,text("async_slice"),text("loop"),empty,&loops[i])==LB_WORLD_OK);
    CHECK(lb_world_call(a,text("async_slice"),text("identity"),empty,&call)==LB_WORLD_FULL && !call);
    lb_call_release(loops[0]); loops[0]=NULL; /* pending orphan keeps its reservation until completion/stop */
    CHECK(lb_world_call(a,text("async_slice"),text("identity"),empty,&call)==LB_WORLD_FULL);
    CHECK(lb_world_call(b,text("async_slice"),text("identity"),(LbBytesView){input,sizeof(input)},&retained)==LB_WORLD_OK);
    CHECK(await_call(retained,&view)==LB_WORLD_OK && view.size==sizeof(input));
    m.fail_host=m.host_calls+1;
    CHECK(lb_world_stop(a,&stopped)==LB_WORLD_NO_MEMORY && !stopped); m.fail_host=0;
    CHECK(lb_world_call(b,text("async_slice"),text("identity"),empty,&peer)==LB_WORLD_OK);
    CHECK(lb_world_stop(a,&stopped)==LB_WORLD_OK);
    CHECK(lb_world_call(a,text("async_slice"),text("identity"),empty,&call)==LB_WORLD_CLOSED);
    expect(peer,LB_WORLD_OK); reclaimed(stopped);
    lb_engine_enter(engine); CHECK(a->phase==LB_WORLD_RECLAIMED && !a->space); lb_engine_leave(engine);
    for(i=1;i<16;++i) expect(loops[i],LB_WORLD_CANCELLED);
    lb_reclamation_release(stopped);
    CHECK(lb_world_create(engine,&replacement)==LB_WORLD_OK && lb_world_load(replacement,code)==LB_WORLD_OK);
    CHECK(lb_world_start(replacement,text("async_slice"),text("identity"),empty,&call)==LB_WORLD_OK); expect(call,LB_WORLD_OK);
    lb_world_release(replacement); lb_world_release(a);
    CHECK(lb_world_stop(b,&stopped)==LB_WORLD_OK); reclaimed(stopped);
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_BUSY);
    lb_engine_release(engine); /* surviving closed controls retain only the required parent */
    lb_world_release(b); lb_reclamation_release(stopped);
    CHECK(lb_call_poll(retained,&view)==LB_WORLD_OK && view.size==sizeof(input) && view.data[0]==0);
    expect(retained,LB_WORLD_OK); CHECK(!m.live && !m.bytes);
    for(i=0;i<sizeof(host_reply);++i) CHECK(host_reply[i]==0x6b);
    puts("CORE_WORLD_EXECUTION_OK worker=true copied_results=true peer_progress=true capacity=true stopped_reclaimed=true replacement=true owner_drop=true public_api=false");
}
static void physical_release(LbBytesView code)
{
    Memory m={0}; LbEngine *engine=create(&m); LbWorld *world,*peer;
    LbCall *call; LbReclamation *reclamation; size_t live,bytes,control_size;
    CHECK(lb_world_create(engine,&peer)==LB_WORLD_OK && lb_world_load(peer,code)==LB_WORLD_OK);
    CHECK(lb_world_start(peer,text("async_slice"),text("identity"),empty,&call)==LB_WORLD_OK);
    expect(call,LB_WORLD_OK);
    lb_engine_enter(engine); live=m.live; bytes=m.bytes; lb_engine_leave(engine);
    m.capture_next=1; CHECK(lb_world_create(engine,&world)==LB_WORLD_OK); control_size=m.capture_size;
    CHECK(lb_world_load(world,code)==LB_WORLD_OK);
    CHECK(lb_world_start(world,text("async_slice"),text("identity"),text("copied"),&call)==LB_WORLD_OK);
    expect(call,LB_WORLD_OK); CHECK(lb_world_stop(world,&reclamation)==LB_WORLD_OK);
    reclaimed(reclamation); lb_reclamation_release(reclamation);
    /* Exactly one closed world control remains, not a deferred code/heap arena. */
    balanced(engine,&m,live+1,bytes+control_size);
    CHECK(lb_world_call(peer,text("async_slice"),text("identity"),empty,&call)==LB_WORLD_OK);
    expect(call,LB_WORLD_OK); lb_world_release(world); balanced(engine,&m,live,bytes);
    lb_world_release(peer); CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine);
    CHECK(!m.live && !m.bytes);
    puts("CORE_WORLD_RECLAMATION_OK exact_vm_release=true only_closed_control_retained=true peer_survives=true");
}
static void pending_owner_drop(LbBytesView code)
{
    size_t i;
    for(i=0;i<32;++i) {
        Memory m={0}; LbEngine *engine=create(&m); LbWorld *world;
        LbCall *call; LbReclamation *reclamation;
        CHECK(lb_world_create(engine,&world)==LB_WORLD_OK && lb_world_load(world,code)==LB_WORLD_OK);
        CHECK(lb_world_start(world,text("async_slice"),text("identity"),text("orphan"),&call)==LB_WORLD_OK);
        lb_call_release(call); CHECK(lb_world_stop(world,&reclamation)==LB_WORLD_OK);
        /* Owner drop can overlap worker-side orphan completion/code retirement. */
        lb_engine_release(engine); reclaimed(reclamation);
        lb_world_release(world); lb_reclamation_release(reclamation);
        CHECK(!m.live && !m.bytes);
    }
    puts("CORE_WORLD_OWNER_DROP_OK pending_retirement=true orphan_completion=true repeat=32");
}
static void spawned_children(LbBytesView code)
{
    Memory m={0}; LbEngine *engine=create(&m); LbWorld *world; LbCall *call;
    CHECK(lb_world_create(engine,&world)==LB_WORLD_OK && lb_world_load(world,code)==LB_WORLD_OK);
    CHECK(lb_world_start(world,text("fun_slice"),text("echo"),text("worker"),&call)==LB_WORLD_OK);
    expect(call,LB_WORLD_OK);
    CHECK(lb_world_call(world,text("fun_slice"),text("spawn_finite"),text("guest"),&call)==LB_WORLD_OK);
    expect(call,LB_WORLD_OK);
    CHECK(lb_world_call(world,text("fun_slice"),text("send_self"),text("owned message"),&call)==LB_WORLD_OK);
    expect(call,LB_WORLD_OK);
    lb_engine_enter(engine); CHECK(!world->space->messages); lb_engine_leave(engine);
    CHECK(lb_world_call(world,text("fun_slice"),text("send_badarg"),text("not a pid"),&call)==LB_WORLD_OK);
    expect(call,LB_WORLD_EXCEPTION);
    CHECK(lb_world_call(world,text("fun_slice"),text("spawn_badarg"),text("not an atom"),&call)==LB_WORLD_OK);
    expect(call,LB_WORLD_EXCEPTION);
    CHECK(lb_world_call(world,text("fun_slice"),text("spawn_refused"),text("not a fun"),&call)==LB_WORLD_OK);
    expect(call,LB_WORLD_EXECUTION_REFUSED);
    /* Fault every worker allocation before the first successful complete
     * spawn. The parent reports infrastructure OOM, never a guest badarg;
     * neither failed child preparation nor parent exit leaves PID membership. */
    for(size_t prefix=1;;++prefix) {
        CHECK(prefix<100);
        lb_engine_enter(engine); m.fail_worker=m.worker_calls+prefix; lb_engine_leave(engine);
        CHECK(lb_world_call(world,text("fun_slice"),text("spawn_finite"),text("faulted guest"),&call)==LB_WORLD_OK);
        LbBytesView result; LbWorldStatus status=await_call(call,&result);
        lb_engine_enter(engine); m.fail_worker=0; lb_engine_leave(engine);
        CHECK(status==LB_WORLD_NO_MEMORY || status==LB_WORLD_OK); expect(call,status);
        const struct timespec delay={0,1000000}; size_t spins;
        for(spins=0;spins<5000;++spins) {
            lb_engine_enter(engine); size_t members=lb_pid_count(world->space->pids); lb_engine_leave(engine);
            if(!members) break; nanosleep(&delay,NULL);
        }
        CHECK(spins<5000);
        if(status==LB_WORLD_OK) break;
    }
    for(size_t prefix=1;;++prefix) {
        CHECK(prefix<100);
        lb_engine_enter(engine); m.fail_worker=m.worker_calls+prefix; lb_engine_leave(engine);
        CHECK(lb_world_call(world,text("fun_slice"),text("send_self"),text("faulted message"),&call)==LB_WORLD_OK);
        LbBytesView result; LbWorldStatus status=await_call(call,&result);
        lb_engine_enter(engine); m.fail_worker=0;
        CHECK(!world->space->messages && !lb_pid_count(world->space->pids)); lb_engine_leave(engine);
        CHECK(status==LB_WORLD_NO_MEMORY || status==LB_WORLD_OK); expect(call,status);
        if(status==LB_WORLD_OK) break;
    }
    for(size_t prefix=1;;++prefix) {
        CHECK(prefix<100);
        lb_engine_enter(engine); m.fail_worker=m.worker_calls+prefix; lb_engine_leave(engine);
        CHECK(lb_world_call(world,text("fun_slice"),text("receive_self"),text("copied closure"),&call)==LB_WORLD_OK);
        LbBytesView result; LbWorldStatus status=await_call(call,&result);
        lb_engine_enter(engine); m.fail_worker=0;
        CHECK(!world->space->messages && !lb_pid_count(world->space->pids)); lb_engine_leave(engine);
        if(status!=LB_WORLD_NO_MEMORY && status!=LB_WORLD_OK) fprintf(stderr,"receive prefix=%zu status=%d\n",prefix,status);
        CHECK(status==LB_WORLD_NO_MEMORY || status==LB_WORLD_OK); expect(call,status);
        if(status==LB_WORLD_OK) break;
    }
    for(size_t prefix=1;;++prefix) {
        CHECK(prefix<100);
        lb_engine_enter(engine); m.fail_worker=m.worker_calls+prefix; lb_engine_leave(engine);
        CHECK(lb_world_call(world,text("fun_slice"),text("named_self"),text("registered reply"),&call)==LB_WORLD_OK);
        LbBytesView result; LbWorldStatus status=await_call(call,&result);
        if(status==LB_WORLD_OK) CHECK(result.size==16 && !memcmp(result.data,"registered reply",16));
        lb_engine_enter(engine); m.fail_worker=0;
        CHECK(!world->space->registry && !world->space->messages && !lb_pid_count(world->space->pids)); lb_engine_leave(engine);
        CHECK(status==LB_WORLD_NO_MEMORY || status==LB_WORLD_OK); expect(call,status);
        if(status==LB_WORLD_OK) break;
    }
    for(size_t prefix=1;;++prefix) {
        CHECK(prefix<100);
        lb_engine_enter(engine); m.fail_worker=m.worker_calls+prefix; lb_engine_leave(engine);
        CHECK(lb_world_call(world,text("fun_slice"),text("marked_request"),text("marked reply"),&call)==LB_WORLD_OK);
        LbBytesView result; LbWorldStatus status=await_call(call,&result);
        if(status==LB_WORLD_OK) CHECK(result.size==12 && !memcmp(result.data,"marked reply",12));
        lb_engine_enter(engine); m.fail_worker=0;
        CHECK(!world->space->registry && !world->space->messages && !lb_pid_count(world->space->pids)); lb_engine_leave(engine);
        CHECK(status==LB_WORLD_NO_MEMORY || status==LB_WORLD_OK); expect(call,status);
        if(status==LB_WORLD_OK) break;
    }
    const char *monitored[]={"monitored_normal","monitored_kill","monitored_flush","monitored_error","monitored_error_info","monitored_bif_error","monitored_error_call","monitored_empty_info"};
    for(size_t i=0;i<sizeof(monitored)/sizeof(monitored[0]);++i) for(size_t prefix=1;;++prefix) {
        CHECK(prefix<300);
        lb_engine_enter(engine); m.fail_worker=m.worker_calls+prefix; lb_engine_leave(engine);
        CHECK(lb_world_call(world,text("fun_slice"),text(monitored[i]),text("monitored reply"),&call)==LB_WORLD_OK);
        LbBytesView result; LbWorldStatus status=await_call(call,&result);
        if(status==LB_WORLD_OK) CHECK(result.size==15 && !memcmp(result.data,"monitored reply",15));
        lb_engine_enter(engine); m.fail_worker=0;
        CHECK(!world->space->registry && !world->space->messages && !lb_pid_count(world->space->pids)); lb_engine_leave(engine);
        CHECK(status==LB_WORLD_NO_MEMORY || status==LB_WORLD_OK); expect(call,status);
        if(status==LB_WORLD_OK) break;
    }
    puts("CORE_WORLD_MONITOR_ERRORS_OK real_mfa_args=true saved_continuations=true error_info=true capture_prefix_rollback=true no_line_info=true");
    const char *errors[]={"named_duplicate","missing_name","guest_error"};
    for(size_t i=0;i<sizeof(errors)/sizeof(errors[0]);++i) {
        CHECK(lb_world_call(world,text("fun_slice"),text(errors[i]),text("reason"),&call)==LB_WORLD_OK);
        expect(call,LB_WORLD_EXCEPTION);
        lb_engine_enter(engine); CHECK(!world->space->registry && !world->space->messages && !lb_pid_count(world->space->pids)); lb_engine_leave(engine);
    }
    for(size_t prefix=1;;++prefix) {
        CHECK(prefix<100);
        lb_engine_enter(engine); m.fail_worker=m.worker_calls+prefix; lb_engine_leave(engine);
        CHECK(lb_world_call(world,text("fun_slice"),text("guest_error_info"),text("native error metadata"),&call)==LB_WORLD_OK);
        LbBytesView result; LbWorldStatus status=await_call(call,&result);
        lb_engine_enter(engine); m.fail_worker=0;
        CHECK(!world->space->registry && !world->space->messages && !lb_pid_count(world->space->pids)); lb_engine_leave(engine);
        CHECK(status==LB_WORLD_NO_MEMORY || status==LB_WORLD_EXCEPTION); expect(call,status);
        if(status==LB_WORLD_EXCEPTION) break;
    }
    /* The private task seam below still tests preparation from a retained
     * worker-produced result. Actual guest spawn above uses the owning world. */
    const char *factories[]={"process_factory","process_nested","process_loop"};
    for(size_t kind=0;kind<3;++kind) {
        LbCodeEntry *entry; LbTask *source;
        lb_engine_enter(engine);
        Eterm module,function;
        CHECK(lb_atoms_find(world->space->atoms,"fun_slice",9,&module)==LB_ATOM_OK);
        CHECK(lb_atoms_find(world->space->atoms,factories[kind],strlen(factories[kind]),&function)==LB_ATOM_OK);
        CHECK(lb_code_entry_acquire(world->space,module,function,1,&entry)==LB_CODE_OK);
        lb_engine_leave(engine);
        CHECK(lb_task_create_binary(engine,entry,"captured",8,&source)==LB_ENGINE_OK);
        const struct timespec delay={0,1000000}; size_t spins=0;
        while(lb_task_status(source)==LB_TASK_PENDING && ++spins<5000) nanosleep(&delay,NULL);
        CHECK(lb_task_status(source)==LB_TASK_DONE);
        lb_engine_enter(engine);
        size_t live=m.live,bytes=m.bytes,members=lb_pid_count(world->space->pids);
        Eterm pid;
        for(size_t prefix=1;;++prefix) {
            CHECK(prefix<100); m.fail_host=m.host_calls+prefix;
            LbProcessStatus status=lb_task_spawn_result_locked(world,source,&pid); m.fail_host=0;
            if(status==LB_PROCESS_READY) break;
            CHECK(status==LB_PROCESS_NO_MEMORY && is_non_value(pid));
            CHECK(m.live==live && m.bytes==bytes && lb_pid_count(world->space->pids)==members);
        }
        CHECK(lb_pid_lookup(world->space->pids,pid));
        lb_task_release_locked(source); lb_code_entry_release(entry); // source heap cannot be borrowed
        lb_engine_leave(engine);
        if(kind<2) {
            for(spins=0;spins<5000;++spins) {
                lb_engine_enter(engine); int exited=!lb_pid_lookup(world->space->pids,pid); lb_engine_leave(engine);
                if(exited) break; nanosleep(&delay,NULL);
            }
            CHECK(spins<5000);
        } else {
            nanosleep(&delay,NULL); lb_engine_enter(engine);
            CHECK(lb_pid_lookup(world->space->pids,pid)); lb_engine_leave(engine);
            CHECK(lb_world_call(world,text("fun_slice"),text("nested"),text("peer"),&call)==LB_WORLD_OK);
            expect(call,LB_WORLD_OK); // persistent child does not prevent another runnable process
        }
    }
    CHECK(lb_world_call(world,text("fun_slice"),text("receive_roundtrip"),text("noise"),&call)==LB_WORLD_OK);
    expect(call,LB_WORLD_OK);
    CHECK(lb_world_call(world,text("fun_slice"),text("receive_timeout"),text("timed out"),&call)==LB_WORLD_OK);
    expect(call,LB_WORLD_OK);
    CHECK(lb_world_call(world,text("fun_slice"),text("spawn_loop"),text("guest loop"),&call)==LB_WORLD_OK);
    expect(call,LB_WORLD_OK);
    lb_engine_enter(engine); CHECK(lb_pid_count(world->space->pids)==3); lb_engine_leave(engine);
    CHECK(lb_world_call(world,text("fun_slice"),text("send_child"),text("child message"),&call)==LB_WORLD_OK);
    expect(call,LB_WORLD_OK);
    lb_engine_enter(engine);
    CHECK(lb_pid_count(world->space->pids)==4 && world->space->messages==1);
    lb_engine_leave(engine);
    CHECK(lb_world_call(world,text("fun_slice"),text("nested"),text("progress"),&call)==LB_WORLD_OK);
    expect(call,LB_WORLD_OK);
    LbReclamation *r; CHECK(lb_world_stop(world,&r)==LB_WORLD_OK); reclaimed(r);
    lb_reclamation_release(r); lb_world_release(world); lb_engine_release(engine);
    CHECK(!m.live && !m.bytes && m.worker_calls);
    puts("CORE_WORLD_CHILDREN_OK worker_execution=true prepared_before_runnable=true child_outlives_source=true stop_drains_children=true");
}
static LbCall *probe_call(LbWorld *world,const char *function,const char *input,const char *wanted)
{
    LbCall *call; LbBytesView result;
    CHECK(lb_world_call(world,text("probe"),text(function),text(input),&call)==LB_WORLD_OK);
    CHECK(await_call(call,&result)==LB_WORLD_OK);
    CHECK(result.size==strlen(wanted) && !memcmp(result.data,wanted,result.size));
    return call;
}
static void probe_boot(LbWorld *world,const char *wanted)
{
    LbCall *call; LbBytesView result;
    CHECK(lb_world_start(world,text("probe"),text("boot"),text(""),&call)==LB_WORLD_OK);
    CHECK(await_call(call,&result)==LB_WORLD_OK);
    CHECK(result.size==strlen(wanted) && !memcmp(result.data,wanted,result.size)); expect(call,LB_WORLD_OK);
}
static void stateful_probes(LbBytesView a_code,LbBytesView b_code)
{
    Memory m={0}; LbEngine *engine=create(&m); LbWorld *peer;
    CHECK(lb_world_create(engine,&peer)==LB_WORLD_OK && lb_world_load(peer,b_code)==LB_WORLD_OK);
    probe_boot(peer,"B:ready");
    size_t prefix;
    /* Real monitored boot, including copied closure, register, ready receive,
     * demonitor and result allocation. Every worker allocation prefix starts a
     * fresh A world; a waiting B actor remains live on the same worker. */
    for(prefix=1;;++prefix) {
        CHECK(prefix<500); LbWorld *world; LbCall *call; LbBytesView result;
        CHECK(lb_world_create(engine,&world)==LB_WORLD_OK && lb_world_load(world,a_code)==LB_WORLD_OK);
        lb_engine_enter(engine); size_t before=m.worker_calls; m.fail_worker=before+prefix; lb_engine_leave(engine);
        CHECK(lb_world_start(world,text("probe"),text("boot"),text(""),&call)==LB_WORLD_OK);
        LbWorldStatus status=await_call(call,&result);
        if(status!=LB_WORLD_OK && status!=LB_WORLD_NO_MEMORY) fprintf(stderr,"stateful boot prefix=%zu status=%d\n",prefix,status);
        CHECK(status==LB_WORLD_OK || status==LB_WORLD_NO_MEMORY);
        if(status==LB_WORLD_OK) CHECK(result.size==7 && !memcmp(result.data,"A:ready",7));
        lb_engine_enter(engine); size_t allocations=m.worker_calls-before; m.fail_worker=0; lb_engine_leave(engine);
        expect(call,status);
        if(status==LB_WORLD_OK) expect(probe_call(world,"request","read","A:0"),LB_WORLD_OK);
        LbReclamation *r; CHECK(lb_world_stop(world,&r)==LB_WORLD_OK); reclaimed(r);
        CHECK(!world->space); lb_reclamation_release(r); lb_world_release(world);
        expect(probe_call(peer,"request","read","B:0"),LB_WORLD_OK);
        if(status==LB_WORLD_OK && prefix>allocations) break;
    }
    for(unsigned round=0;round<32;++round) {
        LbWorld *world; LbCall *retained; LbBytesView detached;
        CHECK(lb_world_create(engine,&world)==LB_WORLD_OK && lb_world_load(world,a_code)==LB_WORLD_OK);
        probe_boot(world,"A:ready");
        for(unsigned count=1;count<=64;++count) {
            char reply[32]; CHECK(snprintf(reply,sizeof(reply),"A:%u",count)>0);
            expect(probe_call(world,"request","next",reply),LB_WORLD_OK);
            expect(probe_call(world,"request","read",reply),LB_WORLD_OK);
        }
        /* Unchanged boot's exceptional branch: the second child fails real
         * register/2, sends native error DOWN with a trace, and the parent
         * raises bootstrap_failed. The original named actor must survive. */
        LbCall *duplicate=NULL;
        CHECK(lb_world_call(world,text("probe"),text("boot"),text(""),&duplicate)==LB_WORLD_OK);
        expect(duplicate,LB_WORLD_EXCEPTION);
        retained=probe_call(world,"request","read","A:64");
        CHECK(lb_call_poll(retained,&detached)==LB_WORLD_OK);
        lb_engine_enter(engine);
        CHECK(world->space->registry && lb_pid_count(world->space->pids)==1);
        CHECK(world->space->messages==0);
        Eterm name; Uint cost=0;
        CHECK(lb_atoms_find(world->space->atoms,"probe_state",11,&name)==LB_ATOM_OK);
        LbProcess *actor=lb_registry_lookup(world->space,name,&cost);
        CHECK(actor && actor->collections>0 && actor->waiting);
        lb_engine_leave(engine);
        /* Several correlated invocations are queued when the waiting actor is
         * cancelled. Host reply storage outlives all guest code and heaps. */
        LbCall *queued[8];
        for(unsigned i=0;i<8;++i) CHECK(lb_world_call(world,text("probe"),text("request"),text("next"),&queued[i])==LB_WORLD_OK);
        LbReclamation *r; CHECK(lb_world_stop(world,&r)==LB_WORLD_OK); reclaimed(r);
        CHECK(!world->space && detached.size==4 && !memcmp(detached.data,"A:64",4));
        LbCall *closed=NULL;
        CHECK(lb_world_call(world,text("probe"),text("request"),text("read"),&closed)==LB_WORLD_CLOSED && !closed);
        for(unsigned i=0;i<8;++i) {
            LbBytesView result; LbWorldStatus status=await_call(queued[i],&result);
            CHECK(status==LB_WORLD_OK || status==LB_WORLD_CANCELLED); expect(queued[i],status);
        }
        expect(retained,LB_WORLD_OK); lb_reclamation_release(r); lb_world_release(world);
        expect(probe_call(peer,"request","read","B:0"),LB_WORLD_OK);
    }
    LbReclamation *r; CHECK(lb_world_stop(peer,&r)==LB_WORLD_OK); reclaimed(r);
    lb_reclamation_release(r); lb_world_release(peer);
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine);
    CHECK(!m.live && !m.bytes);
    printf("CORE_WORLD_STATEFUL_OK boot_failure_prefixes=%zu churn=32 correlated_calls=4096 duplicate_boot_errors=32 queued_stop=true detached_reply=true peer_survives=true exact_final_balance=true\n",prefix);
}
int main(int argc,char **argv)
{
    construction();
    if(argc>=2) { LbBytesView code=image(argv[1]); admission_failures(code); execution(code); physical_release(code); pending_owner_drop(code); free((void *)code.data); }
    if(argc>=3) { LbBytesView code=image(argv[2]); spawned_children(code); free((void *)code.data); }
    if(argc==5) {
        LbBytesView a=image(argv[3]),b=image(argv[4]); stateful_probes(a,b);
        free((void *)a.data); free((void *)b.data);
    }
    CHECK(argc==1 || argc==2 || argc==3 || argc==5); return 0;
}
