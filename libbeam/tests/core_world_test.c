/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#define _POSIX_C_SOURCE 200809L
#include "world_internal.h"
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
int main(int argc,char **argv)
{
    construction();
    if(argc==2) { LbBytesView code=image(argv[1]); admission_failures(code); execution(code); physical_release(code); pending_owner_drop(code); free((void *)code.data); }
    CHECK(argc==1 || argc==2); return 0;
}
