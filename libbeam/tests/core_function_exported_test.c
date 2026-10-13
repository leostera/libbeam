/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#include "process_internal.h"
#include "world_internal.h"
#include "work_budget_internal.h"
#include "alloc_internal.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while(0)
typedef enum { SMALL_ARITY, BIG_ARITY, FLOAT_ARITY } ArityKind;
typedef struct { const char *module,*function; Sint arity; ArityKind kind; Eterm expected; } Query;
static Eterm atom(LbWorld *w,const char *text)
{
    Eterm value; if(!text) return NIL;
    if(lb_atoms_find(w->space->atoms,text,strlen(text),&value)==LB_ATOM_OK) return value;
    CHECK(lb_atoms_intern(w->space->atoms,text,strlen(text),&value)==LB_ATOM_OK); return value;
}
static LbCodeModule *load(LbWorld *w,const char *path)
{
    FILE *f=fopen(path,"rb"); CHECK(f && !fseek(f,0,SEEK_END)); long size=ftell(f); CHECK(size>0 && !fseek(f,0,SEEK_SET));
    unsigned char *data=malloc((size_t)size); CHECK(data && fread(data,1,(size_t)size,f)==(size_t)size && !fclose(f));
    LbCodeModule *module; LbBeamError error; CHECK(lb_code_load(w->space,data,(size_t)size,&module,&error)==LB_CODE_OK); free(data); return module;
}
static LbProcess *create(LbWorld *w,LbCodeModule *host,Query query)
{
    Eterm module=atom(w,query.module),function=atom(w,query.function),value=NIL;
    LbCodeEntry *entry; LbProcess *p;
    CHECK(lb_code_entry_acquire(w->space,host->name,atom(w,"query"),1,&entry)==LB_CODE_OK);
    CHECK(lb_process_create(entry,&value,1,512,&p)==LB_PROCESS_READY); lb_code_entry_release(entry);
    if(query.kind==SMALL_ARITY) value=make_small(query.arity);
    else {
        Eterm *hp=p->htop;
        if(query.kind==BIG_ARITY) { hp[0]=_make_header(1,POS_BIG_SUBTAG); hp[1]=(Uint)1<<60; }
        else { double number=1.0; hp[0]=_make_header(1,FLOAT_SUBTAG); memcpy(hp+1,&number,sizeof(number)); }
        value=make_boxed(hp); p->htop+=2;
    }
    p->reg[0]=TUPLE3(p->htop,module,function,value); p->htop+=4; return p;
}
static LbProcessStatus run(LbProcess *p)
{
    LbProcessStatus status=LB_PROCESS_READY;
    for(size_t i=0;i<1000 && (status==LB_PROCESS_READY || status==LB_PROCESS_YIELDED);++i) status=lb_process_run(p,1,100);
    return status;
}
static void outcome(LbProcess *p,Query query)
{
    if(is_non_value(query.expected)) CHECK(p->status==LB_PROCESS_EXCEPTION && p->exception_value==am_badarg);
    else CHECK(p->status==LB_PROCESS_DONE && lb_process_result(p)==query.expected);
    CHECK(!p->collections && !p->fun_leases);
}
static void enter_bif(LbProcess *p)
{
    for(unsigned i=0;i<100 && p->i[0]!=op_call_bif_W;++i) CHECK(lb_process_run(p,1,100)==LB_PROCESS_YIELDED);
    CHECK(p->i[0]==op_call_bif_W);
}
static void *deny(void *context,size_t bytes) { (void)context; (void)bytes; return NULL; }
static void check(LbWorld *w,LbCodeModule *host,Query query)
{
    LbAllocDomain *d=w->space->domain;
    /* Intern query atoms before taking balance snapshots or borrowing budgets. */
    (void)atom(w,query.module); (void)atom(w,query.function);
    size_t baseline=d->current,atoms=lb_atoms_count(w->space->atoms); LbCodeModule *modules=w->space->modules; Export *exports=w->space->exports;
    LbProcess *p=create(w,host,query); (void)run(p); outcome(p,query); lb_process_destroy(p);
    for(unsigned mode=0;mode<2;++mode) {
        p=create(w,host,query); enter_bif(p);
        while(p->htop<p->stop-S_RESERVED) *p->htop++=NIL;
        Eterm before[]={p->reg[0],p->reg[1],p->reg[2]}; Eterm *heap=p->htop;
        LbSystemAllocator system=d->system; size_t limit=d->limit,live=d->current;
        if(mode) d->system.allocate=deny; else d->limit=live;
        Eterm result=lb_bif_function_exported_3(p,p->reg,NULL);
        CHECK(result==query.expected && !memcmp(before,p->reg,sizeof(before)) && p->htop==heap && d->current==live && !p->collections);
        (void)run(p); outcome(p,query);
        d->system=system; d->limit=limit; lb_process_destroy(p);
    }
    uint64_t required=0;
    for(uint64_t pass=0;pass<=required+1;++pass) {
        p=create(w,host,query); LbWorkBudget *budget=lb_work_budget_create(w); CHECK(budget);
        uint64_t credit=pass?pass-1:1000000; budget->remaining=budget->limit=credit; lb_work_domain_borrow(d,budget);
        LbProcessStatus status=run(p);
        if(!pass) { outcome(p,query); required=credit-budget->remaining; CHECK(required>1 && required<10000); }
        else if(credit<required) CHECK(status==LB_PROCESS_WORK_LIMIT);
        else { outcome(p,query); CHECK(!budget->remaining); }
        lb_work_domain_borrow(d,NULL); lb_work_domain_clear_failure(d); lb_alloc_domain_begin_operation(d);
        lb_process_destroy(p); lb_work_budget_release(budget); CHECK(d->current==baseline);
    }
    CHECK(lb_atoms_count(w->space->atoms)==atoms && w->space->modules==modules && w->space->exports==exports);
}
static void close_world(LbWorld *w)
{
    LbReclamation *r; CHECK(lb_world_stop(w,&r)==LB_WORLD_OK && lb_reclamation_poll(r)==LB_WORLD_OK);
    CHECK(lb_reclamation_consume(r)==LB_WORLD_OK); lb_reclamation_release(r); lb_world_release(w);
}
int main(int argc,char **argv)
{
    CHECK(argc==3); LbEngine *engine; LbWorld *world,*peer;
    CHECK(lb_engine_create(NULL,&engine)==LB_ENGINE_OK && lb_world_create(engine,&world)==LB_WORLD_OK && lb_world_create(engine,&peer)==LB_WORLD_OK);
    lb_engine_enter(engine);
    LbCodeModule *host=load(world,argv[1]),*target=load(world,argv[2]),*peer_host=load(peer,argv[1]);
    Query cases[]={
        {"function_exported_slice","identity",1,SMALL_ARITY,am_true},
        {"function_exported_slice","private",1,SMALL_ARITY,am_false},
        {"apply_target","zero",0,SMALL_ARITY,am_true}, {"apply_target","one",1,SMALL_ARITY,am_true},
        {"erlang","abs",1,SMALL_ARITY,am_true}, {"erlang","apply",3,SMALL_ARITY,am_true},
        {"apply_target","missing",1,SMALL_ARITY,am_false}, {"unloaded_probe_module","missing",1,SMALL_ARITY,am_false},
        {"apply_target","one",-1,SMALL_ARITY,am_false}, {"apply_target","one",256,SMALL_ARITY,am_false},
        {"apply_target","one",INT64_C(4294967297),SMALL_ARITY,am_true},
        {"apply_target","one",-INT64_C(4294967295),SMALL_ARITY,am_true},
        {NULL,"one",1,SMALL_ARITY,THE_NON_VALUE}, {"apply_target",NULL,1,SMALL_ARITY,THE_NON_VALUE},
        {"apply_target","one",0,BIG_ARITY,THE_NON_VALUE}, {"apply_target","one",0,FLOAT_ARITY,THE_NON_VALUE}
    };
    for(size_t i=0;i<sizeof(cases)/sizeof(cases[0]);++i) { check(world,host,cases[i]); CHECK(!target->users); }
    Query absent={"apply_target","one",1,SMALL_ARITY,am_false}; check(peer,peer_host,absent);
    lb_engine_leave(engine); close_world(peer); close_world(world);
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine);
    puts("CORE_FUNCTION_EXPORTED_OK published_namespace=true native_instruction=true private_missing=false arity_wrap=true badarg=true allocation_free=true work_prefixes=true no_loading_or_leases=true peer_scope=true balanced=true"); return 0;
}
