/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Actual public-world calls on the shared worker; host never interprets BEAM.
 */
#define _POSIX_C_SOURCE 200809L
#include "world_internal.h"
#include "work_budget_internal.h"
#include "executor.h"
#include "md5.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"CHECK %d: %s\n",__LINE__,#c); abort(); } } while(0)
static LbBytesView text(const char *s) { return (LbBytesView){(const unsigned char *)s,strlen(s)}; }
static void tick(void) { struct timespec delay={0,1000000}; nanosleep(&delay,NULL); }
static LbWorld *make(LbEngine *engine,LbBytesView beam,uint64_t work)
{
    LbWorld *world; LbResourceOverrides limits={.has_work=1,.invocation_work=work};
    CHECK(lb_world_create_with_overrides(engine,&limits,&world)==LB_WORLD_OK);
    LbWorldStatus loaded=lb_world_load(world,beam);
    if(loaded!=LB_WORLD_OK) fprintf(stderr,"fixture load status=%d\n",loaded);
    CHECK(loaded==LB_WORLD_OK); return world;
}
static LbCall *invoke(LbWorld *world,const char *function,int start)
{
    LbCall *call=NULL;
    LbWorldStatus status=(start?lb_world_start:lb_world_call)(world,text("work_slice"),text(function),text("value"),&call);
    if(status!=LB_WORLD_OK) fprintf(stderr,"admission %s status=%d\n",function,status);
    CHECK(status==LB_WORLD_OK && call); return call;
}
static LbWorldStatus poll(LbCall *call,LbBytesView *out)
{
    for(unsigned i=0;i<10000;++i) {
        LbWorldStatus status=lb_call_poll(call,out); if(status!=LB_WORLD_PENDING) return status;
        tick();
    }
    CHECK(0); return LB_WORLD_PENDING;
}
static void consume(LbCall *call) { CHECK(lb_call_consume(call)==LB_WORLD_OK); lb_call_release(call); }
static void retire(LbWorld *world)
{
    LbWorldResourceUsage usage; CHECK(lb_world_resource_usage(world,&usage)==LB_WORLD_OK);
    if(!usage.closed) {
        LbReclamation *r; CHECK(lb_world_stop(world,&r)==LB_WORLD_OK);
        for(unsigned i=0;lb_reclamation_poll(r)==LB_WORLD_PENDING && i<10000;++i) tick();
        CHECK(lb_reclamation_consume(r)==LB_WORLD_OK); lb_reclamation_release(r);
    }
    lb_world_release(world);
}
static uint64_t remaining(LbCall *call)
{
    LbEngine *engine=call->world->engine; lb_engine_enter(engine);
    uint64_t value=call->budget->remaining; lb_engine_leave(engine); return value;
}
static void result(LbCall *call,const char *expected)
{
    LbBytesView out; LbWorldStatus status=poll(call,&out);
    if(status!=LB_WORLD_OK) fprintf(stderr,"result expected=%s status=%d remaining=%llu\n",expected,status,(unsigned long long)remaining(call));
    CHECK(status==LB_WORLD_OK);
    CHECK(out.size==strlen(expected) && !memcmp(out.data,expected,out.size));
}
static void native_result(LbCall *call,int decimal)
{
    LbBytesView out; CHECK(poll(call,&out)==LB_WORLD_OK);
    if(!decimal) { CHECK(out.size==5 && !memcmp(out.data,"equal",5)); return; }
    // Independently computed from Python's exact str(2**8192), not guest output.
    static const unsigned char expected[16]={0x8c,0x4b,0x7c,0x19,0x4d,0x66,0xf3,0x0d,0x68,0x61,0xbd,0xe4,0x10,0x41,0xf2,0x35};
    unsigned char digest[16]; erts_md5(out.data,out.size,digest);
    CHECK(out.size==2467 && !memcmp(digest,expected,sizeof(digest)));
}
static uint64_t native_threshold(LbEngine *engine,LbBytesView beam,const char *function,int decimal)
{
    LbWorld *world=make(engine,beam,1000000); LbCall *call=invoke(world,function,1);
    native_result(call,decimal); uint64_t cost=1000000-remaining(call);
    fprintf(stderr,"native %s units=%llu\n",function,(unsigned long long)cost);
    CHECK(cost>1000 && cost<1000000);
    consume(call); retire(world);
    world=make(engine,beam,cost); call=invoke(world,function,1);
    native_result(call,decimal); CHECK(!remaining(call)); consume(call); retire(world);
    world=make(engine,beam,cost-1); call=invoke(world,function,1); LbBytesView out;
    CHECK(poll(call,&out)==LB_WORLD_WORK_LIMIT); consume(call);
    call=invoke(world,"pure",0); result(call,"value"); consume(call); retire(world);
    return cost;
}
static uint64_t small_decimal_threshold(LbEngine *engine,LbBytesView beam)
{
    LbWorld *world=make(engine,beam,1000000); LbCall *call=invoke(world,"native_small_decimal",1);
    result(call,"7"); uint64_t cost=1000000-remaining(call);
    CHECK(cost>100 && cost<1000000); consume(call); retire(world);
    world=make(engine,beam,cost); call=invoke(world,"native_small_decimal",1);
    result(call,"7"); CHECK(!remaining(call)); consume(call); retire(world);
    world=make(engine,beam,cost-1); call=invoke(world,"native_small_decimal",1); LbBytesView out;
    CHECK(poll(call,&out)==LB_WORLD_WORK_LIMIT); consume(call);
    call=invoke(world,"pure",0); result(call,"value"); consume(call); retire(world);
    return cost;
}
static void float_result(LbCall *call)
{
    static const unsigned char expected[8]={0x58,0xf0,0,0,0,0,0,0};
    LbBytesView out; CHECK(poll(call,&out)==LB_WORLD_OK);
    CHECK(out.size==sizeof(expected) && !memcmp(out.data,expected,sizeof(expected)));
}
static uint64_t float_threshold(LbEngine *engine,LbBytesView beam)
{
    LbWorld *world=make(engine,beam,1000000); LbCall *call=invoke(world,"native_big_float_binary",1);
    float_result(call); uint64_t cost=1000000-remaining(call);
    CHECK(cost>100 && cost<1000000); consume(call); retire(world);
    world=make(engine,beam,cost); call=invoke(world,"native_big_float_binary",1);
    float_result(call); CHECK(!remaining(call)); consume(call); retire(world);
    world=make(engine,beam,cost-1); call=invoke(world,"native_big_float_binary",1); LbBytesView out;
    CHECK(poll(call,&out)==LB_WORLD_WORK_LIMIT); consume(call);
    call=invoke(world,"pure",0); result(call,"value"); consume(call); retire(world);
    return cost;
}
static uint64_t monitor_threshold(LbEngine *engine,LbBytesView beam)
{
    LbWorld *world=make(engine,beam,1000000); LbCall *call=invoke(world,"monitor_work",1);
    result(call,"value"); uint64_t cost=1000000-remaining(call);
    CHECK(cost>100 && cost<1000000); consume(call); retire(world);
    world=make(engine,beam,cost); call=invoke(world,"monitor_work",1);
    result(call,"value"); CHECK(!remaining(call)); consume(call); retire(world);
    world=make(engine,beam,cost-1); call=invoke(world,"monitor_work",1); LbBytesView out;
    CHECK(poll(call,&out)==LB_WORLD_WORK_LIMIT); consume(call);
    LbWorldResourceUsage usage; CHECK(lb_world_resource_usage(world,&usage)==LB_WORLD_OK && !usage.closed);
    call=invoke(world,"pure",0); result(call,"value"); consume(call); retire(world);
    return cost;
}
int main(int argc,char **argv)
{
    if(argc==1) return 0; CHECK(argc==2);
    FILE *file=fopen(argv[1],"rb"); CHECK(file); CHECK(!fseek(file,0,SEEK_END)); long size=ftell(file); CHECK(size>0);
    rewind(file); unsigned char *bytes=malloc((size_t)size); CHECK(bytes);
    CHECK(fread(bytes,1,(size_t)size,file)==(size_t)size && !fclose(file)); LbBytesView beam={bytes,(size_t)size};
    LbEngine *engine; CHECK(lb_engine_create(NULL,&engine)==LB_ENGINE_OK);
    uint64_t decimal_cost=native_threshold(engine,beam,"native_decimal",1);
    uint64_t equality_cost=native_threshold(engine,beam,"native_equal",0);
    uint64_t small_decimal_cost=small_decimal_threshold(engine,beam);
    uint64_t float_cost=float_threshold(engine,beam);
    uint64_t monitor_cost=monitor_threshold(engine,beam);
    LbWorld *gc=make(engine,beam,1000000); LbCall *gc_call=invoke(gc,"gc_wait",1);
    size_t collections=0; uint64_t gc_credit=0; int gc_idle=0;
    for(unsigned i=0;i<10000 && !gc_idle;++i) {
        LbTaskObservation observed=lb_task_observe(gc_call->task); CHECK(observed.status==LB_TASK_PENDING);
        gc_idle=observed.waiting; collections=observed.collections; gc_credit=remaining(gc_call);
        if(!gc_idle) tick();
    }
    CHECK(gc_idle && collections>0 && gc_credit<1000000);
    tick(); CHECK(remaining(gc_call)==gc_credit);
    CHECK(lb_call_cancel(gc_call)==LB_WORLD_OK); consume(gc_call); retire(gc);
    LbWorld *probe=make(engine,beam,1000000); LbCall *call=invoke(probe,"pure",1); result(call,"value");
    uint64_t pure_cost=1000000-remaining(call); CHECK(pure_cost>1 && pure_cost<1000);
    consume(call); retire(probe);
    LbWorld *exact=make(engine,beam,pure_cost); call=invoke(exact,"pure",1); result(call,"value");
    CHECK(remaining(call)==0); consume(call); retire(exact);
    LbWorld *below=make(engine,beam,pure_cost-1); call=invoke(below,"pure",1);
    LbBytesView out; CHECK(poll(call,&out)==LB_WORLD_WORK_LIMIT); consume(call); retire(below);
    LbWorld *world=make(engine,beam,4096),*peer=make(engine,beam,4096);
    LbCall *waiting=invoke(world,"waiting",1); tick(); uint64_t idle=remaining(waiting);
    for(unsigned i=0;i<8;++i) tick(); CHECK(remaining(waiting)==idle);
    call=invoke(world,"loop",0); CHECK(poll(call,&out)==LB_WORLD_WORK_LIMIT); consume(call);
    LbWorldResourceUsage usage; CHECK(lb_world_resource_usage(world,&usage)==LB_WORLD_OK && !usage.closed);
    call=invoke(world,"pure",0); result(call,"value"); consume(call);
    call=invoke(peer,"pure",1); result(call,"value"); consume(call);
    CHECK(lb_call_cancel(waiting)==LB_WORLD_OK && poll(waiting,&out)==LB_WORLD_CANCELLED); consume(waiting);
    call=invoke(world,"private_wait",0); CHECK(poll(call,&out)==LB_WORLD_WORK_LIMIT); consume(call);
    CHECK(lb_world_resource_usage(world,&usage)==LB_WORLD_OK && !usage.closed);
    call=invoke(world,"background",0);
    // The root reply survives later exhaustion of its old background allowance.
    for(unsigned i=0;i<25;++i) tick(); result(call,"ready"); consume(call);
    CHECK(lb_world_resource_usage(world,&usage)==LB_WORLD_OK && !usage.closed);
    /* This witness targets explicit cancellation after registration, not work
     * exhaustion. Give its own finite allowance enough startup headroom as the
     * native export catalog grows; the 4096-unit exhaustion worlds stay intact. */
    LbWorld *registered=make(engine,beam,1000000);
    call=invoke(registered,"registered_wait",1);
    int escaped=0;
    for(unsigned i=0;i<10000 && !escaped;++i) {
        lb_engine_enter(engine); escaped=call->budget->escaped; lb_engine_leave(engine); if(!escaped) tick();
    }
    if(!escaped || lb_task_observe(call->task).status!=LB_TASK_PENDING)
        fprintf(stderr,"registered cancellation witness escaped=%d task_status=%d remaining=%llu\n",escaped,(int)lb_task_observe(call->task).status,(unsigned long long)remaining(call));
    CHECK(escaped && lb_call_cancel(call)==LB_WORLD_OK && poll(call,&out)==LB_WORLD_CANCELLED); consume(call);
    CHECK(lb_world_resource_usage(registered,&usage)==LB_WORLD_OK && usage.closed); retire(registered);
    LbWorld *unsafe=make(engine,beam,4096); call=invoke(unsafe,"escaped_wait",1);
    CHECK(poll(call,&out)==LB_WORLD_WORK_LIMIT); consume(call);
    CHECK(lb_world_resource_usage(unsafe,&usage)==LB_WORLD_OK && usage.closed);
    call=invoke(peer,"pure",0); result(call,"value"); consume(call);
    LbWorld *causal=make(engine,beam,1000000);
    call=invoke(causal,"boot",1); result(call,"boot");
    lb_engine_enter(engine); LbWorkBudget *birth=call->budget;
    CHECK(lb_work_budget_retain(birth)==LB_ALLOC_OK); lb_engine_leave(engine); consume(call);
    LbCall *noise=invoke(causal,"noise",0); result(noise,"noise"); uint64_t noise_before=remaining(noise);
    call=invoke(causal,"request",0); result(call,"value"); consume(call);
    lb_engine_enter(engine); uint64_t dormant=birth->remaining; lb_engine_leave(engine);
    for(unsigned i=0;i<32;++i) { call=invoke(causal,"request",0); result(call,"value"); consume(call); }
    CHECK(remaining(noise)<noise_before); // Skipped candidate work belongs to its original sender.
    lb_engine_enter(engine); CHECK(birth->remaining==dormant); lb_work_budget_release(birth); lb_engine_leave(engine);
    consume(noise);
    /* Dedicated causal-transfer witness: leave enough for sender completion,
     * but not the receiver's additional 32-KiB host copy. Native catalog growth
     * changes PC-validation work; defaults and exact-threshold tests above stay
     * unchanged. The assertions below require this interval to remain real. */
    LbWorld *foreign=make(engine,beam,5000); LbCall *receiver=invoke(foreign,"foreign_wait",1);
    escaped=0;
    for(unsigned i=0;i<10000 && !escaped;++i) {
        lb_engine_enter(engine); escaped=receiver->budget->escaped; lb_engine_leave(engine); if(!escaped) tick();
    }
    CHECK(escaped);
    unsigned char large[32768]; memset(large,0x75,sizeof(large));
    CHECK(lb_world_call(foreign,text("work_slice"),text("deliver"),(LbBytesView){large,sizeof(large)},&call)==LB_WORLD_OK);
    lb_engine_enter(engine);
    LbWorkBudget *sender_credit=call->budget;
    CHECK(lb_work_budget_retain(sender_credit)==LB_ALLOC_OK); lb_engine_leave(engine);
    // Sender completed first; the receiving root later exceeds that sender's
    // remaining credit while copying its actual 32-KiB result to the host.
    result(call,"delivered");
    CHECK(poll(receiver,&out)==LB_WORLD_CANCELLED);
    lb_engine_enter(engine);
    CHECK(sender_credit->exhausted && !receiver->budget->exhausted);
    lb_work_budget_release(sender_credit);
    lb_engine_leave(engine);
    consume(receiver); consume(call);
    CHECK(lb_world_resource_usage(foreign,&usage)==LB_WORLD_OK && usage.closed);
    retire(foreign); retire(causal); retire(world); retire(unsafe); retire(peer); CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine); free(bytes);
    printf("CORE_WORK_NATIVE_OK decimal_units=%llu small_decimal_units=%llu big_float_units=%llu equality_units=%llu monitor_units=%llu worker_collections=%zu native_thresholds=true monitor_failure_reuses=true pure_reuse=true execution=shared_worker\n",(unsigned long long)decimal_cost,(unsigned long long)small_decimal_cost,(unsigned long long)float_cost,(unsigned long long)equality_cost,(unsigned long long)monitor_cost,collections);
    printf("CORE_WORK_RUNTIME_OK pure_cost=%llu exact_threshold=true fresh_credit=true private_family_reuse=true idle_unspent=true background_retained=true escaped_retirement=true candidate_sender_charged=true birth_credit_not_borrowed=true foreign_completion_contained=true peer_survives=true execution=shared_worker\n",(unsigned long long)pure_cost);
}
