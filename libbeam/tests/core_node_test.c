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
static LbCodeModule *load(LbWorld *w,const char *path)
{
    FILE *f=fopen(path,"rb"); CHECK(f && !fseek(f,0,SEEK_END)); long size=ftell(f); CHECK(size>0 && !fseek(f,0,SEEK_SET));
    unsigned char *bytes=malloc((size_t)size); CHECK(bytes && fread(bytes,1,(size_t)size,f)==(size_t)size && !fclose(f));
    LbCodeModule *m; LbBeamError error; LbCodeStatus s=lb_code_load(w->space,bytes,(size_t)size,&m,&error);
    if(s!=LB_CODE_OK) fprintf(stderr,"node load=%d stage=%s\n",s,error.stage);
    CHECK(s==LB_CODE_OK); free(bytes); return m;
}
static LbProcess *create(LbWorld *w,LbCodeModule *m,const char *name)
{
    Eterm function,input=NIL; LbCodeEntry *entry; LbProcess *p;
    CHECK(lb_atoms_find(w->space->atoms,name,strlen(name),&function)==LB_ATOM_OK);
    CHECK(lb_code_entry_acquire(w->space,m->name,function,1,&entry)==LB_CODE_OK);
    CHECK(lb_process_create(entry,&input,1,512,&p)==LB_PROCESS_READY); lb_code_entry_release(entry);
    if(!strcmp(name,"dynamic")) { p->reg[0]=TUPLE2(p->htop,am_erlang,am_node); p->htop+=3; }
    return p;
}
static LbProcessStatus run(LbProcess *p)
{ LbProcessStatus s=LB_PROCESS_READY; for(unsigned i=0;i<1000 && (s==LB_PROCESS_READY || s==LB_PROCESS_YIELDED);++i) s=lb_process_run(p,1,100); return s; }
static void *deny(void *context,size_t bytes) { (void)context; (void)bytes; return NULL; }
static void check(LbWorld *w,LbCodeModule *m,const char *name)
{
    LbAllocDomain *d=w->space->domain; size_t baseline=d->current,blocks=d->block_count,atoms=lb_atoms_count(w->space->atoms);
    LbCodeModule *modules=w->space->modules; Export *exports=w->space->exports;
    CHECK(w->space->node_name==am_Noname);
    uint64_t required=0;
    for(uint64_t pass=0;pass<=required+1;++pass) {
        LbProcess *p=create(w,m,name); LbWorkBudget *budget=lb_work_budget_create(w); CHECK(budget);
        uint64_t credit=pass?pass-1:1000000; budget->limit=budget->remaining=credit; lb_work_domain_borrow(d,budget);
        LbProcessStatus status=run(p);
        if(!pass) { CHECK(status==LB_PROCESS_DONE && lb_process_result(p)==am_Noname); required=credit-budget->remaining; CHECK(required>1 && required<10000); }
        else if(credit<required) CHECK(status==LB_PROCESS_WORK_LIMIT);
        else CHECK(status==LB_PROCESS_DONE && lb_process_result(p)==am_Noname && !budget->remaining);
        lb_work_domain_borrow(d,NULL); lb_work_domain_clear_failure(d); lb_alloc_domain_begin_operation(d);
        lb_process_destroy(p); lb_work_budget_release(budget); CHECK(d->current==baseline && d->block_count==blocks);
    }
    LbProcess *p=create(w,m,name); CHECK(run(p)==LB_PROCESS_DONE && lb_process_result(p)==am_Noname);
    CHECK(lb_process_collect(p,0)==LB_PROCESS_DONE && lb_process_result(p)==am_Noname); lb_process_destroy(p);
    CHECK(d->current==baseline && d->block_count==blocks && lb_atoms_count(w->space->atoms)==atoms);
    CHECK(w->space->modules==modules && w->space->exports==exports);
}
static void primitive(LbWorld *w,LbCodeModule *m)
{
    LbAllocDomain *d=w->space->domain; size_t baseline=d->current,blocks=d->block_count;
    for(unsigned credit=0;credit<3;++credit) for(unsigned mode=0;mode<2;++mode) {
        LbProcess *p=create(w,m,"query"); LbWorkBudget *budget=lb_work_budget_create(w); CHECK(budget);
        p->heap_reserved=7; while(p->htop<p->stop-S_RESERVED-p->heap_reserved) *p->htop++=NIL;
        Eterm *top=p->htop; Eterm input=p->reg[0]; size_t live=d->current,atoms=lb_atoms_count(w->space->atoms),limit=d->limit;
        LbSystemAllocator system=d->system; if(mode) d->system.allocate=deny; else d->limit=live;
        budget->limit=budget->remaining=credit; lb_work_domain_borrow(d,budget);
        Eterm result=lb_bif_node_0(p,NULL,NULL);
        if(!credit) CHECK(is_non_value(result) && p->status==LB_PROCESS_WORK_LIMIT);
        else CHECK(result==am_Noname && budget->remaining==credit-1);
        CHECK(p->htop==top && p->reg[0]==input && p->heap_reserved==7 && !p->collections && !p->fun_leases);
        CHECK(d->current==live && lb_atoms_count(w->space->atoms)==atoms);
        d->system=system; d->limit=limit; lb_work_domain_borrow(d,NULL); lb_work_domain_clear_failure(d); lb_alloc_domain_begin_operation(d);
        lb_process_destroy(p); lb_work_budget_release(budget); CHECK(d->current==baseline && d->block_count==blocks);
    }
}
static void close_world(LbWorld *w)
{ LbReclamation *r; CHECK(lb_world_stop(w,&r)==LB_WORLD_OK && lb_reclamation_poll(r)==LB_WORLD_OK && lb_reclamation_consume(r)==LB_WORLD_OK); lb_reclamation_release(r); lb_world_release(w); }
int main(int argc,char **argv)
{
    CHECK(argc==3); LbEngine *engine; LbWorld *w,*peer;
    CHECK(lb_engine_create(NULL,&engine)==LB_ENGINE_OK && lb_world_create(engine,&w)==LB_WORLD_OK && lb_world_create(engine,&peer)==LB_WORLD_OK);
    lb_engine_enter(engine); LbCodeModule *host=load(w,argv[1]),*locations=load(w,argv[2]),*other=load(peer,argv[1]);
    primitive(w,host); check(w,host,"query"); check(w,host,"kept"); check(w,host,"dynamic"); check(w,locations,"query"); check(w,locations,"query_y");
    lb_engine_leave(engine); close_world(w);
    lb_engine_enter(engine); primitive(peer,other); check(peer,other,"query"); lb_engine_leave(engine); close_world(peer);
    CHECK(lb_world_create(engine,&w)==LB_WORLD_OK); lb_engine_enter(engine); host=load(w,argv[1]); check(w,host,"query"); lb_engine_leave(engine); close_world(w);
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine);
    puts("CORE_NODE_OK unnamed_state=true actual_calls=true x_y=true dynamic_apply=true allocation_free=true work_prefixes=true gc_survival=true peer_replacement=true balanced=true"); return 0;
}
