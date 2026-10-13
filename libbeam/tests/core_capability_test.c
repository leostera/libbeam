/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#define _POSIX_C_SOURCE 200809L
#include "process_internal.h"
#include "world_internal.h"
#include "work_budget_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"capability:%d: %s\n",__LINE__,#x); abort(); } } while(0)
static const struct { const char *name,*direct; unsigned arity; LbCapability capability; LbBifFn fn; } entries[]={
#define LB_DENIAL(id, name, arity, category) {#name,"d_" #name "_" #arity,arity,LB_CAPABILITY_##category,lb_bif_deny_##name##_##arity},
#include "capability_denials.def"
#undef LB_DENIAL
};
static size_t blocks; static int deny_allocations;
static void *allocate(void *ctx,size_t size) { (void)ctx; if(deny_allocations) return NULL; void *p=malloc(size); if(p) ++blocks; return p; }
static void release(void *ctx,void *p) { (void)ctx; CHECK(p && blocks); --blocks; free(p); }
static LbBytesView text(const char *s) { return (LbBytesView){(const unsigned char *)s,strlen(s)}; }
static void tick(void) { struct timespec t={0,1000000}; nanosleep(&t,NULL); }
static LbWorldStatus poll(LbCall *call)
{
    LbBytesView bytes;
    for(unsigned i=0;i<10000;++i) { LbWorldStatus s=lb_call_poll(call,&bytes); if(s!=LB_WORLD_PENDING) return s; tick(); }
    CHECK(0); return LB_WORLD_PENDING;
}
static LbCall *invoke(LbWorld *w,const char *fn,LbBytesView input,int start)
{
    LbCall *call;
    CHECK((start?lb_world_start:lb_world_call)(w,text("capability_slice"),text(fn),input,&call)==LB_WORLD_OK);
    return call;
}
static void consume(LbCall *call)
{
    CHECK(lb_call_consume(call)==LB_WORLD_OK && !lb_call_capability_failure(call)); lb_call_release(call);
}
static LbWorld *make(LbEngine *engine,LbBytesView beam)
{
    LbWorld *w; CHECK(lb_world_create(engine,&w)==LB_WORLD_OK);
    LbWorldLoadReport report; LbWorldStatus s=lb_world_load_report(w,beam,&report);
    if(s!=LB_WORLD_OK) fprintf(stderr,"load=%d stage=%s offset=%zu\n",s,report.stage,report.offset);
    CHECK(s==LB_WORLD_OK); return w;
}
static void retire(LbWorld *w)
{
    LbWorldResourceUsage u; CHECK(lb_world_resource_usage(w,&u)==LB_WORLD_OK);
    if(!u.closed) {
        LbReclamation *r; CHECK(lb_world_stop(w,&r)==LB_WORLD_OK);
        for(unsigned i=0;i<10000 && lb_reclamation_poll(r)==LB_WORLD_PENDING;++i) tick();
        CHECK(lb_reclamation_consume(r)==LB_WORLD_OK); lb_reclamation_release(r);
    }
    for(unsigned i=0;i<10000;++i) {
        lb_engine_enter(w->engine); int done=w->phase==LB_WORLD_RECLAIMED; lb_engine_leave(w->engine);
        if(done) break; tick();
    }
    lb_engine_enter(w->engine); CHECK(w->phase==LB_WORLD_RECLAIMED && !w->domain.current && !w->domain.block_count); lb_engine_leave(w->engine);
    lb_world_release(w);
}
static void primitive(LbWorld *w)
{
    Eterm module,function,input=NIL;
    CHECK(lb_atoms_find(w->space->atoms,"capability_slice",16,&module)==LB_ATOM_OK);
    CHECK(lb_atoms_find(w->space->atoms,"identity",8,&function)==LB_ATOM_OK);
    size_t baseline=w->domain.current,baseline_blocks=blocks,atoms=lb_atoms_count(w->space->atoms);
    for(size_t i=0;i<sizeof(entries)/sizeof(entries[0]);++i) for(unsigned credit=0;credit<3;++credit) {
        LbCodeEntry *entry; LbProcess *p;
        CHECK(lb_code_entry_acquire(w->space,module,function,1,&entry)==LB_CODE_OK);
        CHECK(lb_process_create(entry,&input,1,512,&p)==LB_PROCESS_READY); lb_code_entry_release(entry);
        LbWorkBudget *budget=lb_work_budget_create(w); CHECK(budget);
        budget->remaining=budget->limit=credit;
        p->heap_reserved=7; while(p->htop<p->stop-S_RESERVED-7) *p->htop++=NIL;
        Eterm *top=p->htop; size_t live=w->domain.current,limit=w->domain.limit;
        w->domain.limit=live; deny_allocations=1; lb_work_domain_borrow(&w->domain,budget);
        CHECK(is_non_value(entries[i].fn(p,NULL,NULL)));
        if(!credit) CHECK(p->status==LB_PROCESS_WORK_LIMIT && !p->capability_failure);
        else {
            CHECK(p->status==LB_PROCESS_CAPABILITY_DENIED && budget->remaining==credit-1);
            const LbCapabilityFailure *f=p->capability_failure;
            CHECK(f && !strcmp(f->module,"erlang") && !strcmp(f->function,entries[i].name));
            CHECK(f->arity==entries[i].arity && f->capability==entries[i].capability && strlen(f->message)<256);
        }
        CHECK(p->htop==top && p->heap_reserved==7 && !p->collections && w->domain.current==live);
        CHECK(p->reg[0]==input && lb_atoms_count(w->space->atoms)==atoms);
        deny_allocations=0; w->domain.limit=limit; lb_work_domain_borrow(&w->domain,NULL);
        lb_work_domain_clear_failure(&w->domain); lb_alloc_domain_begin_operation(&w->domain);
        lb_process_destroy(p); lb_work_budget_release(budget);
        CHECK(w->domain.current==baseline && blocks==baseline_blocks);
    }
}
static LbBytesView request(unsigned char *buffer,const char *name,unsigned arity)
{
    size_t n=strlen(name),i=0; CHECK(n<128 && arity<4);
    buffer[i++]=131; buffer[i++]=104; buffer[i++]=2; buffer[i++]=119; buffer[i++]=(unsigned char)n;
    memcpy(buffer+i,name,n); i+=n;
    if(arity) { buffer[i++]=108; buffer[i++]=0; buffer[i++]=0; buffer[i++]=0; buffer[i++]=(unsigned char)arity;
        for(unsigned a=0;a<arity;++a) buffer[i++]=106; }
    buffer[i++]=106; return (LbBytesView){buffer,i};
}
int main(int argc,char **argv)
{
    CHECK(argc==2); FILE *file=fopen(argv[1],"rb"); CHECK(file && !fseek(file,0,SEEK_END));
    long size=ftell(file); CHECK(size>0 && !fseek(file,0,SEEK_SET));
    unsigned char *bytes=malloc((size_t)size); CHECK(bytes && fread(bytes,1,(size_t)size,file)==(size_t)size && !fclose(file));
    LbBytesView beam={bytes,(size_t)size}; LbSystemAllocator system={NULL,allocate,release}; LbEngine *engine;
    CHECK(lb_engine_create(&system,&engine)==LB_ENGINE_OK);
    LbWorld *w=make(engine,beam),*peer=make(engine,beam);
    lb_engine_enter(engine); primitive(w); lb_engine_leave(engine);
    const LbCapabilityFailure *retained=NULL;
    for(size_t i=0;i<sizeof(entries)/sizeof(entries[0]);++i) for(unsigned dynamic=0;dynamic<2;++dynamic) {
        unsigned char input[160]; LbBytesView payload=request(input,entries[i].name,entries[i].arity);
        LbCall *call=invoke(w,dynamic?"dynamic":entries[i].direct,payload,i==0 && !dynamic);
        LbWorldStatus status=poll(call);
        if(status!=LB_WORLD_CAPABILITY_DENIED) fprintf(stderr,"%s dynamic=%u status=%d\n",entries[i].name,dynamic,status);
        CHECK(status==LB_WORLD_CAPABILITY_DENIED); retained=lb_call_capability_failure(call);
        CHECK(retained && !strcmp(retained->function,entries[i].name) && retained->arity==entries[i].arity);
        consume(call);
    }
    LbCall *call=invoke(w,"monitored",text(""),0); CHECK(poll(call)==LB_WORLD_CAPABILITY_DENIED);
    CHECK(!strcmp(lb_call_capability_failure(call)->function,"load_nif")); consume(call);
    call=invoke(w,"identity",text("reusable"),0); CHECK(poll(call)==LB_WORLD_OK); consume(call);
    call=invoke(w,"escaped",text(""),0); CHECK(poll(call)==LB_WORLD_CAPABILITY_DENIED); consume(call);
    LbWorldResourceUsage u; CHECK(lb_world_resource_usage(w,&u)==LB_WORLD_OK && u.closed); retire(w);
    CHECK(retained && !strcmp(retained->module,"erlang"));
    call=invoke(peer,"identity",text("peer"),1); CHECK(poll(call)==LB_WORLD_OK); consume(call); retire(peer);
    w=make(engine,beam); call=invoke(w,"direct",text(""),1); CHECK(poll(call)==LB_WORLD_CAPABILITY_DENIED); consume(call); retire(w);
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine); CHECK(!blocks); free(bytes);
    puts("CORE_CAPABILITY_OK direct=27 dynamic=27 primitive_prefixes=true child_propagation=true escaped_closed=true peer_replacement=true balanced=true catch_unwind_pending=true");
    return 0;
}
