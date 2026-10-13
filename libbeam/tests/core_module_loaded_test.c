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
static Eterm atom(LbWorld *w,const char *name)
{ Eterm result; CHECK(lb_atoms_intern(w->space->atoms,name,strlen(name),&result)==LB_ATOM_OK); return result; }
static LbCodeModule *load(LbWorld *w,const char *path)
{
    FILE *f=fopen(path,"rb"); CHECK(f && !fseek(f,0,SEEK_END)); long size=ftell(f); CHECK(size>0 && !fseek(f,0,SEEK_SET));
    unsigned char *bytes=malloc((size_t)size); CHECK(bytes && fread(bytes,1,(size_t)size,f)==(size_t)size && !fclose(f));
    LbCodeModule *m; LbBeamError error; CHECK(lb_code_load(w->space,bytes,(size_t)size,&m,&error)==LB_CODE_OK); free(bytes); return m;
}
static LbProcess *create(LbWorld *w,LbCodeModule *m,Eterm input)
{
    Eterm function; LbCodeEntry *entry; LbProcess *p;
    CHECK(lb_atoms_find(w->space->atoms,"query",5,&function)==LB_ATOM_OK);
    CHECK(lb_code_entry_acquire(w->space,m->name,function,1,&entry)==LB_CODE_OK);
    CHECK(lb_process_create(entry,&input,1,512,&p)==LB_PROCESS_READY); lb_code_entry_release(entry); return p;
}
static LbProcessStatus run(LbProcess *p)
{ LbProcessStatus s=LB_PROCESS_READY; for(unsigned i=0;i<1000 && (s==LB_PROCESS_READY || s==LB_PROCESS_YIELDED);++i) s=lb_process_run(p,1,100); return s; }
static void outcome(LbProcess *p,Eterm expected)
{
    if(is_non_value(expected)) CHECK(p->status==LB_PROCESS_EXCEPTION && p->exception_value==am_badarg);
    else CHECK(p->status==LB_PROCESS_DONE && lb_process_result(p)==expected);
    CHECK(!p->collections && !p->fun_leases);
}
static void *deny(void *context,size_t size) { (void)context; (void)size; return NULL; }
static void check(LbWorld *w,LbCodeModule *m,Eterm input,Eterm expected)
{
    LbAllocDomain *d=w->space->domain; size_t baseline=d->current,blocks=d->block_count,atoms=lb_atoms_count(w->space->atoms);
    LbCodeModule *modules=w->space->modules; Export *exports=w->space->exports;
    for(unsigned mode=0;mode<2;++mode) {
        LbProcess *p=create(w,m,input);
        while(p->i[0]!=op_call_bif_W) CHECK(lb_process_run(p,1,100)==LB_PROCESS_YIELDED);
        while(p->htop<p->stop-S_RESERVED) *p->htop++=NIL;
        Eterm *top=p->htop; Eterm source=p->reg[0]; size_t live=d->current,limit=d->limit;
        LbSystemAllocator system=d->system;
        if(mode) d->system.allocate=deny; else d->limit=live;
        CHECK(lb_bif_module_loaded_1(p,p->reg,NULL)==expected && p->reg[0]==source && p->htop==top && d->current==live);
        (void)run(p); outcome(p,expected); d->system=system; d->limit=limit; lb_process_destroy(p);
        CHECK(d->current==baseline && d->block_count==blocks);
    }
    uint64_t required=0;
    for(uint64_t pass=0;pass<=required+1;++pass) {
        LbProcess *p=create(w,m,input); LbWorkBudget *budget=lb_work_budget_create(w); CHECK(budget);
        uint64_t credit=pass?pass-1:1000000; budget->limit=budget->remaining=credit; lb_work_domain_borrow(d,budget);
        LbProcessStatus status=run(p);
        if(!pass) { outcome(p,expected); required=credit-budget->remaining; CHECK(required>1 && required<10000); }
        else if(credit<required) CHECK(status==LB_PROCESS_WORK_LIMIT);
        else { outcome(p,expected); CHECK(!budget->remaining); }
        lb_work_domain_borrow(d,NULL); lb_work_domain_clear_failure(d); lb_alloc_domain_begin_operation(d);
        lb_process_destroy(p); lb_work_budget_release(budget); CHECK(d->current==baseline && d->block_count==blocks);
    }
    CHECK(lb_atoms_count(w->space->atoms)==atoms && w->space->modules==modules && w->space->exports==exports);
}
static void boxed_badargs(LbWorld *w,LbCodeModule *m)
{
    for(unsigned kind=0;kind<4;++kind) {
        LbProcess *p=create(w,m,NIL); Eterm *hp=p->htop;
        if(kind==0) { p->reg[0]=TUPLE1(hp,am_ok); p->htop+=2; }
        if(kind==1) { p->reg[0]=CONS(hp,am_ok,NIL); p->htop+=2; }
        if(kind==2) { hp[0]=_make_header(1,POS_BIG_SUBTAG); hp[1]=(Uint)1<<60; p->reg[0]=make_boxed(hp); p->htop+=2; }
        if(kind==3) { double f=1.0; hp[0]=_make_header(1,FLOAT_SUBTAG); memcpy(hp+1,&f,sizeof(f)); p->reg[0]=make_boxed(hp); p->htop+=2; }
        (void)run(p); outcome(p,THE_NON_VALUE); lb_process_destroy(p);
    }
}
static void refused_on_load(LbWorld *w,LbCodeModule *host,const char *path)
{
    Eterm name=atom(w,"module_on_load_refused"); LbAllocDomain *d=w->space->domain;
    size_t live=d->current,blocks=d->block_count,atoms=lb_atoms_count(w->space->atoms); LbCodeModule *head=w->space->modules;
    FILE *f=fopen(path,"rb"); CHECK(f && !fseek(f,0,SEEK_END)); long size=ftell(f); CHECK(size>0 && !fseek(f,0,SEEK_SET));
    unsigned char *bytes=malloc((size_t)size); CHECK(bytes && fread(bytes,1,(size_t)size,f)==(size_t)size && !fclose(f));
    LbCodeModule *module=NULL; LbBeamError error;
    CHECK(lb_code_load(w->space,bytes,(size_t)size,&module,&error)==LB_CODE_UNSUPPORTED && !module); free(bytes);
    CHECK(d->current==live && d->block_count==blocks && w->space->modules==head && lb_atoms_count(w->space->atoms)==atoms);
    check(w,host,name,am_false);
}
static void close_world(LbWorld *w)
{ LbReclamation *r; CHECK(lb_world_stop(w,&r)==LB_WORLD_OK && lb_reclamation_poll(r)==LB_WORLD_OK && lb_reclamation_consume(r)==LB_WORLD_OK); lb_reclamation_release(r); lb_world_release(w); }
int main(int argc,char **argv)
{
    CHECK(argc==4); LbEngine *engine; LbWorld *w,*peer;
    CHECK(lb_engine_create(NULL,&engine)==LB_ENGINE_OK && lb_world_create(engine,&w)==LB_WORLD_OK && lb_world_create(engine,&peer)==LB_WORLD_OK);
    lb_engine_enter(engine); LbCodeModule *host=load(w,argv[1]),*target=load(w,argv[2]),*other=load(peer,argv[1]);
    Eterm target_name=target->name;
    check(w,host,host->name,am_true); check(w,host,am_erlang,am_true); check(w,host,target_name,am_true);
    check(peer,other,atom(peer,"apply_target"),am_false);
    check(w,host,atom(w,"unloaded_module_probe"),am_false); check(w,host,atom(w,""),am_false);
    check(w,host,am_true,am_false); check(w,host,NIL,THE_NON_VALUE); check(w,host,make_small(0),THE_NON_VALUE); boxed_badargs(w,host);
    CHECK(!target->users && lb_code_unload(target)==LB_CODE_OK); check(w,host,target_name,am_false);
    target=load(w,argv[2]); check(w,host,target_name,am_true); CHECK(!target->users);
    refused_on_load(w,host,argv[3]);
    lb_engine_leave(engine); close_world(peer); close_world(w); CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine);
    puts("CORE_MODULE_LOADED_OK actual_published_code=true preloaded=true absent=false badarg=true allocation_free=true work_prefixes=true no_leases=true unload_reload=true peer_scope=true balanced=true"); return 0;
}
