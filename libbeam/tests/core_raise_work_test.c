/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#define _POSIX_C_SOURCE 200809L
#include "world_internal.h"
#include "work_budget_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while(0)
static LbBytesView text(const char *s) { return (LbBytesView){(const unsigned char *)s,strlen(s)}; }
static void tick(void) { struct timespec delay={0,1000000}; nanosleep(&delay,NULL); }
static LbWorld *make(LbEngine *engine,LbBytesView beam,uint64_t credit)
{
    LbWorld *world; LbResourceOverrides limits={.has_work=1,.invocation_work=credit};
    CHECK(lb_world_create_with_overrides(engine,&limits,&world)==LB_WORLD_OK);
    CHECK(lb_world_load(world,beam)==LB_WORLD_OK); return world;
}
static LbCall *invoke(LbWorld *world,const char *function,int start)
{
    LbCall *call; CHECK((start?lb_world_start:lb_world_call)(world,text("raise_slice"),text(function),text("payload"),&call)==LB_WORLD_OK);
    return call;
}
static void result(LbCall *call,LbWorldStatus expected)
{
    LbBytesView out; LbWorldStatus status=LB_WORLD_PENDING;
    for(size_t i=0;i<10000 && status==LB_WORLD_PENDING;++i) { status=lb_call_poll(call,&out); if(status==LB_WORLD_PENDING) tick(); }
    if(status!=expected) fprintf(stderr,"status=%d expected=%d\n",status,expected);
    CHECK(status==expected);
    if(expected==LB_WORLD_OK) CHECK(out.size==7 && !memcmp(out.data,"payload",7));
}
static uint64_t remaining(LbCall *call)
{
    LbEngine *engine=call->world->engine; lb_engine_enter(engine);
    uint64_t credit=call->budget->remaining; lb_engine_leave(engine); return credit;
}
static void consume(LbCall *call) { CHECK(lb_call_consume(call)==LB_WORLD_OK); lb_call_release(call); }
static void retire(LbWorld *world)
{
    LbReclamation *r; CHECK(lb_world_stop(world,&r)==LB_WORLD_OK);
    for(size_t i=0;i<10000 && lb_reclamation_poll(r)==LB_WORLD_PENDING;++i) tick();
    CHECK(lb_reclamation_consume(r)==LB_WORLD_OK); lb_reclamation_release(r); lb_world_release(world);
}
int main(int argc,char **argv)
{
    FILE *file; long size; CHECK(argc==2 && (file=fopen(argv[1],"rb")) && !fseek(file,0,SEEK_END));
    size=ftell(file); CHECK(size>0 && !fseek(file,0,SEEK_SET)); unsigned char *bytes=malloc((size_t)size);
    CHECK(bytes && fread(bytes,1,(size_t)size,file)==(size_t)size && !fclose(file));
    LbBytesView beam={bytes,(size_t)size}; LbEngine *engine; CHECK(lb_engine_create(NULL,&engine)==LB_ENGINE_OK);
    LbWorld *peer=make(engine,beam,1000000); LbCall *peer_call=invoke(peer,"identity",1); result(peer_call,LB_WORLD_OK); consume(peer_call);
    const char *names[]={"raised_error","raised_exit","raised_throw","plain_throw","fun_frame","monitored"};
    for(size_t i=0;i<6;++i) {
        LbWorldStatus expected=i==5?LB_WORLD_OK:LB_WORLD_EXCEPTION;
        LbWorld *world=make(engine,beam,1000000); LbCall *call=invoke(world,names[i],1); result(call,expected);
        uint64_t cost=1000000-remaining(call); CHECK(cost>1 && cost<1000000);
        fprintf(stderr,"%s units=%llu\n",names[i],(unsigned long long)cost); consume(call); retire(world);
        world=make(engine,beam,cost); call=invoke(world,names[i],1); result(call,expected); CHECK(remaining(call)==0); consume(call);
        call=invoke(world,"identity",0); result(call,LB_WORLD_OK); consume(call); retire(world);
        world=make(engine,beam,cost-1); call=invoke(world,names[i],1); result(call,LB_WORLD_WORK_LIMIT); consume(call);
        call=invoke(world,"identity",0); result(call,LB_WORLD_OK); consume(call); retire(world);
        peer_call=invoke(peer,"identity",0); result(peer_call,LB_WORLD_OK); consume(peer_call);
    }
    retire(peer); CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine); free(bytes);
    puts("CORE_RAISE_WORK_OK real_worker=true exact_threshold=true one_below=true monitor_causal_work=true private_reuse=true peer_survival=true reclaimed=true");
    return 0;
}
