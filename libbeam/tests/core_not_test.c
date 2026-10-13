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
typedef enum { TRUE_VALUE, FALSE_VALUE, ATOM_VALUE, NIL_VALUE, SMALL_VALUE, FLOAT_VALUE, TUPLE_VALUE, LIST_VALUE, BIG_VALUE, BINARY_VALUE, KIND_COUNT } Kind;
static Eterm atom(LbWorld *w,const char *name)
{ Eterm value; CHECK(lb_atoms_find(w->space->atoms,name,strlen(name),&value)==LB_ATOM_OK); return value; }
static LbCodeModule *load(LbWorld *w,const char *path)
{
    FILE *file=fopen(path,"rb"); CHECK(file && !fseek(file,0,SEEK_END));
    long length=ftell(file); CHECK(length>0 && !fseek(file,0,SEEK_SET)); unsigned char *bytes=malloc((size_t)length);
    CHECK(bytes && fread(bytes,1,(size_t)length,file)==(size_t)length && !fclose(file));
    LbCodeModule *module; LbBeamError error; LbCodeStatus status=lb_code_load(w->space,bytes,(size_t)length,&module,&error);
    if(status!=LB_CODE_OK) fprintf(stderr,"load=%d stage=%s offset=%zu\n",status,error.stage,error.offset);
    CHECK(status==LB_CODE_OK); free(bytes); return module;
}
static LbProcess *create(LbWorld *w,LbCodeModule *module,Kind kind)
{
    LbCodeEntry *entry; LbProcess *p; Eterm arg=kind==TRUE_VALUE?am_true:kind==FALSE_VALUE?am_false:
        kind==ATOM_VALUE?atom(w,"invalid"):kind==NIL_VALUE?NIL:make_small(0);
    CHECK(lb_code_entry_acquire(w->space,module->name,atom(w,"negate"),1,&entry)==LB_CODE_OK);
    if(kind==BINARY_VALUE) CHECK(lb_process_create_binary(entry,"data",4,512,&p)==LB_PROCESS_READY);
    else {
        CHECK(lb_process_create(entry,&arg,1,512,&p)==LB_PROCESS_READY); Eterm *hp=p->htop;
        if(kind==FLOAT_VALUE) {
            double value=1.0; hp[0]=_make_header(1,FLOAT_SUBTAG); memcpy(hp+1,&value,sizeof(value));
            p->reg[0]=make_boxed(hp); p->htop+=2;
        } else if(kind==TUPLE_VALUE) { p->reg[0]=TUPLE1(hp,make_small(1)); p->htop+=2; }
        else if(kind==LIST_VALUE) { hp[0]=am_true; hp[1]=NIL; p->reg[0]=make_list(hp); p->htop+=2; }
        else if(kind==BIG_VALUE) { hp[0]=_make_header(2,POS_BIG_SUBTAG); hp[1]=0; hp[2]=1; p->reg[0]=make_boxed(hp); p->htop+=3; }
    }
    lb_code_entry_release(entry); return p;
}
static LbProcessStatus run(LbProcess *p)
{
    LbProcessStatus status=LB_PROCESS_READY;
    for(size_t i=0;i<1000 && (status==LB_PROCESS_READY || status==LB_PROCESS_YIELDED);++i) status=lb_process_run(p,1,100);
    return status;
}
static LbProcessStatus expected_status(Kind kind,int guard)
{ return guard || kind<=FALSE_VALUE?LB_PROCESS_DONE:LB_PROCESS_EXCEPTION; }
static void outcome(LbWorld *w,LbProcess *p,Kind kind,int guard)
{
    CHECK(p->status==expected_status(kind,guard));
    if(p->status==LB_PROCESS_EXCEPTION) CHECK(p->exception_value==am_badarg);
    else CHECK(lb_process_result(p)==(kind==TRUE_VALUE?am_false:kind==FALSE_VALUE?am_true:atom(w,"invalid")));
}
static void *deny(void *context,size_t bytes) { (void)context; (void)bytes; return NULL; }
static void exercise(LbWorld *w,LbCodeModule *module,int guard)
{
    LbAllocDomain *domain=w->space->domain; size_t baseline=domain->current;
    for(Kind kind=TRUE_VALUE;kind<KIND_COUNT;++kind) {
        LbProcess *p=create(w,module,kind); CHECK(run(p)==expected_status(kind,guard)); outcome(w,p,kind,guard); lb_process_destroy(p);
        for(unsigned mode=0;mode<2 && (guard || kind<=FALSE_VALUE);++mode) {
            p=create(w,module,kind);
            BeamInstr boundary=guard?op_i_bif1_sfbd:op_i_bif1_body_sbd;
            for(unsigned step=0;step<100 && p->i[0]!=boundary;++step) CHECK(lb_process_run(p,1,100)==LB_PROCESS_YIELDED);
            CHECK(p->i[0]==boundary);
            while(p->htop<p->stop-S_RESERVED) *p->htop++=NIL;
            Eterm *heap=p->htop; size_t live=domain->current,limit=domain->limit; LbSystemAllocator system=domain->system;
            if(mode) domain->system.allocate=deny; else domain->limit=live;
            CHECK(run(p)==LB_PROCESS_DONE && p->htop==heap && !p->collections && domain->current==live);
            domain->limit=limit; domain->system=system; outcome(w,p,kind,guard); lb_process_destroy(p);
        }
        uint64_t required=0;
        for(uint64_t pass=0;pass<=required+1;++pass) {
            p=create(w,module,kind); LbWorkBudget *budget=lb_work_budget_create(w); CHECK(budget);
            uint64_t credit=pass?pass-1:1000000; budget->limit=budget->remaining=credit; lb_work_domain_borrow(domain,budget);
            LbProcessStatus status=run(p);
            if(!pass) { CHECK(status==expected_status(kind,guard)); required=credit-budget->remaining; CHECK(required>1 && required<10000); }
            else if(credit==required) CHECK(status==expected_status(kind,guard) && budget->remaining==0);
            else CHECK(status==LB_PROCESS_WORK_LIMIT);
            if(status==expected_status(kind,guard)) outcome(w,p,kind,guard);
            lb_work_domain_borrow(domain,NULL); lb_work_domain_clear_failure(domain); lb_alloc_domain_begin_operation(domain);
            lb_process_destroy(p); lb_work_budget_release(budget); CHECK(domain->current==baseline);
        }
    }
}
int main(int argc,char **argv)
{
    CHECK(argc==3); LbEngine *engine; LbWorld *world;
    CHECK(lb_engine_create(NULL,&engine)==LB_ENGINE_OK && lb_world_create(engine,&world)==LB_WORLD_OK); lb_engine_enter(engine);
    LbCodeModule *body=load(world,argv[1]),*guard=load(world,argv[2]); exercise(world,body,0); exercise(world,guard,1);
    lb_engine_leave(engine); LbReclamation *reclamation;
    CHECK(lb_world_stop(world,&reclamation)==LB_WORLD_OK && lb_reclamation_poll(reclamation)==LB_WORLD_OK);
    CHECK(lb_reclamation_consume(reclamation)==LB_WORLD_OK); lb_reclamation_release(reclamation); lb_world_release(world);
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine);
    puts("CORE_NOT_OK strict_boolean=true badarg=true y_guard=true allocation_free=true work_prefixes=true balanced=true"); return 0;
}
