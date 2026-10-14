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
static void module_names(Eterm list,const Eterm *names,size_t count)
{
    unsigned seen=0; CHECK(count<sizeof(seen)*8);
    while(is_list(list)) {
        Eterm name=CAR(list_val(list)); size_t i=0;
        while(i<count && names[i]!=name) ++i;
        CHECK(i<count && !(seen&(1u<<i))); seen|=1u<<i; list=CDR(list_val(list));
    }
    CHECK(list==NIL && seen==((1u<<count)-1));
}
static Eterm enumerate(LbProcess *p,int preloaded)
{ return preloaded?lb_bif_pre_loaded_0(p,NULL,NULL):lb_bif_loaded_0(p,NULL,NULL); }
static void enumeration(LbWorld *w,LbCodeModule *host,const Eterm *names,size_t count)
{
    LbAllocDomain *d=w->space->domain; size_t baseline=d->current,blocks=d->block_count;
    for(unsigned preloaded=0;preloaded<2;++preloaded) {
        Eterm boot=am_erlang; const Eterm *expected=preloaded?&boot:names; size_t n=preloaded?1:count;
        for(unsigned mode=0;mode<3;++mode) {
            LbProcess *p=create(w,host,NIL); p->heap_reserved=17;
            if(mode) while(p->htop<p->stop-S_RESERVED-17) *p->htop++=NIL;
            Eterm *heap=p->heap,*top=p->htop; size_t live=d->current,limit=d->limit;
            LbSystemAllocator system=d->system;
            if(mode==1) d->system.allocate=deny;
            if(mode==2) d->limit=live;
            Eterm result=enumerate(p,preloaded);
            d->system=system; d->limit=limit;
            if(mode) {
                CHECK(is_non_value(result) && p->status==(mode==1?LB_PROCESS_NO_MEMORY:LB_PROCESS_LIMIT));
                CHECK(p->heap==heap && p->htop==top && !p->collections && d->current==live);
                lb_alloc_domain_begin_operation(d); p->status=LB_PROCESS_READY;
                result=enumerate(p,preloaded); CHECK(p->collections>0);
            }
            CHECK(p->heap_reserved==17); module_names(result,expected,n);
            p->reg[0]=result; CHECK(lb_process_collect_live(p,0,1)); module_names(p->reg[0],expected,n);
            CHECK(!p->fun_leases); lb_process_destroy(p);
            CHECK(d->current==baseline && d->block_count==blocks);
        }
        uint64_t required=0;
        for(uint64_t pass=0;pass<=required+1;++pass) {
            LbProcess *p=create(w,host,NIL); LbWorkBudget *budget=lb_work_budget_create(w); CHECK(budget);
            uint64_t credit=pass?pass-1:budget->limit; budget->limit=budget->remaining=credit;
            Eterm *top=p->htop; size_t live=d->current;
            lb_work_domain_borrow(d,budget); Eterm result=enumerate(p,preloaded);
            if(!pass) { module_names(result,expected,n); required=credit-budget->remaining; CHECK(required>1 && required<100); }
            else if(credit<required) CHECK(is_non_value(result) && p->status==LB_PROCESS_WORK_LIMIT && p->htop==top && d->current==live);
            else { module_names(result,expected,n); CHECK(!budget->remaining); }
            lb_work_domain_borrow(d,NULL); lb_work_domain_clear_failure(d); lb_alloc_domain_begin_operation(d);
            lb_process_destroy(p); lb_work_budget_release(budget); CHECK(d->current==baseline && d->block_count==blocks);
        }
    }
}
static void close_world(LbWorld *w)
{ LbReclamation *r; CHECK(lb_world_stop(w,&r)==LB_WORLD_OK && lb_reclamation_poll(r)==LB_WORLD_OK && lb_reclamation_consume(r)==LB_WORLD_OK); lb_reclamation_release(r); lb_world_release(w); }
int main(int argc,char **argv)
{
    CHECK(argc==4); LbEngine *engine; LbWorld *w,*peer;
    CHECK(lb_engine_create(NULL,&engine)==LB_ENGINE_OK && lb_world_create(engine,&w)==LB_WORLD_OK && lb_world_create(engine,&peer)==LB_WORLD_OK);
    lb_engine_enter(engine); LbCodeModule *host=load(w,argv[1]),*target=load(w,argv[2]),*other=load(peer,argv[1]);
    Eterm target_name=target->name;
    Eterm names[]={am_erlang,host->name,target_name},peer_names[]={am_erlang,other->name};
    enumeration(w,host,names,3); enumeration(peer,other,peer_names,2);
    check(w,host,host->name,am_true); check(w,host,am_erlang,am_true); check(w,host,target_name,am_true);
    check(peer,other,atom(peer,"apply_target"),am_false);
    check(w,host,atom(w,"unloaded_module_probe"),am_false); check(w,host,atom(w,""),am_false);
    check(w,host,am_true,am_false); check(w,host,NIL,THE_NON_VALUE); check(w,host,make_small(0),THE_NON_VALUE); boxed_badargs(w,host);
    LbProcess *snapshot=create(w,host,NIL); snapshot->reg[0]=lb_bif_loaded_0(snapshot,NULL,NULL);
    module_names(snapshot->reg[0],names,3);
    CHECK(!target->users && lb_code_unload(target)==LB_CODE_OK); check(w,host,target_name,am_false);
    CHECK(lb_process_collect_live(snapshot,0,1)); module_names(snapshot->reg[0],names,3); lb_process_destroy(snapshot);
    enumeration(w,host,names,2);
    target=load(w,argv[2]); check(w,host,target_name,am_true); CHECK(!target->users);
    refused_on_load(w,host,argv[3]); enumeration(w,host,names,3);
    lb_engine_leave(engine); close_world(peer); close_world(w); CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine);
    puts("CORE_MODULE_LOADED_OK actual_published_code=true preloaded=true absent=false badarg=true allocation_free=true work_prefixes=true no_leases=true unload_reload=true peer_scope=true enumeration_gc=true snapshot_after_unload=true enumeration_work_memory=true balanced=true"); return 0;
}
