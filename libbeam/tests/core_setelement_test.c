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
static LbProcess *create(LbCodeSpace *space,LbCodeModule *module,const char *name,const Eterm *args,size_t arity)
{
    Eterm function; LbCodeEntry *entry; LbProcess *p;
    CHECK(lb_atoms_find(space->atoms,name,strlen(name),&function)==LB_ATOM_OK);
    CHECK(lb_code_entry_acquire(space,module->name,function,arity,&entry)==LB_CODE_OK);
    CHECK(lb_process_create(entry,args,arity,256,&p)==LB_PROCESS_READY);
    lb_code_entry_release(entry); return p;
}
static LbProcessStatus run(LbProcess *p)
{
    LbProcessStatus status=LB_PROCESS_READY;
    for(size_t i=0;i<1000 && (status==LB_PROCESS_READY || status==LB_PROCESS_YIELDED);++i)
        status=lb_process_run(p,1,100);
    return status;
}
static LbProcess *input(LbCodeSpace *space,LbCodeModule *module)
{
    Eterm args[]={make_small(2),NIL,NIL};
    LbProcess *p=create(space,module,"replace",args,3);
    Eterm list=CONS(p->htop,make_small(7),NIL); p->htop+=2;
    p->reg[1]=TUPLE3(p->htop,list,make_small(0),list); p->htop+=4;
    p->reg[2]=TUPLE2(p->htop,am_true,list); p->htop+=3;
    return p;
}
static void output(LbProcess *p,Eterm result)
{
    CHECK(is_tuple(result) && result!=p->reg[1]);
    Eterm *copy=tuple_val(result),*original=tuple_val(p->reg[1]);
    CHECK(arityval(copy[0])==3 && copy[1]==original[1] && copy[3]==original[3]);
    CHECK(original[2]==make_small(0) && copy[2]==p->reg[2]);
    CHECK(tuple_val(copy[2])[1]==am_true && tuple_val(copy[2])[2]==copy[1]);
    CHECK(list_val(copy[1])[0]==make_small(7) && list_val(copy[1])[1]==NIL);
}
typedef struct { LbSystemAllocator backing; size_t remaining; } Prefix;
static void *allocate(void *context,size_t bytes)
{
    Prefix *prefix=context;
    if(!prefix->remaining) return NULL;
    --prefix->remaining; return prefix->backing.allocate(prefix->backing.context,bytes);
}
static void release(void *context,void *ptr)
{ Prefix *prefix=context; prefix->backing.release(prefix->backing.context,ptr); }
static void allocation_boundaries(LbWorld *world,LbCodeModule *module)
{
    LbAllocDomain *domain=world->space->domain; size_t baseline=domain->current;
    int succeeded=0;
    for(size_t attempt=0;attempt<20 && !succeeded;++attempt) {
        LbProcess *p=input(world->space,module);
        /* Valid outstanding credit consumes the remaining gap. Copying four
         * additional words must collect without consuming that credit. */
        size_t reserved=p->heap_reserved=(size_t)(p->stop-p->htop);
        Eterm *heap=p->htop,original=p->reg[1],value=p->reg[2];
        size_t live=domain->current,limit=domain->limit,collections=p->collections;
        Prefix prefix={domain->system,attempt?attempt-1:0};
        if(!attempt) domain->limit=live;
        else domain->system=(LbSystemAllocator){&prefix,allocate,release};
        Eterm result=lb_bif_setelement_3(p,p->reg,NULL);
        domain->limit=limit; domain->system=prefix.backing;
        CHECK(p->heap_reserved==reserved);
        if(is_non_value(result)) {
            CHECK(p->status==(attempt?LB_PROCESS_NO_MEMORY:LB_PROCESS_LIMIT));
            CHECK(p->htop==heap && p->reg[1]==original && p->reg[2]==value && domain->current==live);
        } else {
            succeeded=1; CHECK(p->collections>collections); output(p,result);
            p->reg[0]=result; CHECK(lb_process_collect(p,0)==LB_PROCESS_READY); output(p,p->reg[0]);
        }
        lb_alloc_domain_begin_operation(domain); lb_process_destroy(p); CHECK(domain->current==baseline);
    }
    CHECK(succeeded);
}
static void work_boundaries(LbWorld *world,LbCodeModule *module)
{
    LbAllocDomain *domain=world->space->domain; size_t baseline=domain->current;
    for(unsigned collecting=0;collecting<2;++collecting) {
      uint64_t required=0;
      for(uint64_t pass=0;pass<=required+1;++pass) {
        LbProcess *p=input(world->space,module);
        Eterm original=p->reg[1],value=p->reg[2],*heap=p->htop;
        if(collecting) p->heap_reserved=(size_t)(p->stop-p->htop);
        LbWorkBudget *budget=lb_work_budget_create(world); CHECK(budget);
        uint64_t credit=pass?pass-1:1000000;
        budget->limit=budget->remaining=credit; lb_work_domain_borrow(domain,budget);
        Eterm result=THE_NON_VALUE;
        LbProcessStatus status;
        if(collecting) { result=lb_bif_setelement_3(p,p->reg,NULL); status=p->status; }
        else { status=run(p); if(status==LB_PROCESS_DONE) result=lb_process_result(p); }
        if(!pass) { CHECK(!is_non_value(result)); required=credit-budget->remaining; CHECK(required>1 && required<50000); }
        else if(credit==required) CHECK(!is_non_value(result) && budget->remaining==0);
        else {
            CHECK(is_non_value(result) && status==LB_PROCESS_WORK_LIMIT);
            if(collecting) CHECK(p->htop==heap && p->reg[1]==original && p->reg[2]==value);
        }
        if(!is_non_value(result)) { output(p,result); if(collecting) CHECK(p->collections>0); }
        lb_work_domain_borrow(domain,NULL); lb_work_domain_clear_failure(domain); lb_alloc_domain_begin_operation(domain);
        lb_process_destroy(p); lb_work_budget_release(budget); CHECK(domain->current==baseline);
      }
    }
}
int main(int argc,char **argv)
{
    FILE *file; long length; CHECK(argc==2 && (file=fopen(argv[1],"rb")) && !fseek(file,0,SEEK_END));
    length=ftell(file); CHECK(length>0 && !fseek(file,0,SEEK_SET));
    unsigned char *bytes=malloc((size_t)length);
    CHECK(bytes && fread(bytes,1,(size_t)length,file)==(size_t)length && !fclose(file));
    LbEngine *engine; LbWorld *world; LbCodeModule *module; LbBeamError error;
    CHECK(lb_engine_create(NULL,&engine)==LB_ENGINE_OK && lb_world_create(engine,&world)==LB_WORLD_OK);
    lb_engine_enter(engine);
    LbCodeStatus loaded=lb_code_load(world->space,bytes,(size_t)length,&module,&error);
    if(loaded!=LB_CODE_OK) fprintf(stderr,"load=%d stage=%s offset=%zu\n",loaded,error.stage,error.offset);
    CHECK(loaded==LB_CODE_OK); free(bytes);
    LbProcess *p=create(world->space,module,"sample",NULL,0); CHECK(run(p)==LB_PROCESS_DONE);
    Eterm result=lb_process_result(p),expected;
    CHECK(lb_atoms_find(world->space->atoms,"new",3,&expected)==LB_ATOM_OK);
    CHECK(is_tuple(result) && arityval(*tuple_val(result))==3 && tuple_val(result)[2]==expected);
    lb_process_destroy(p);
    {
        Eterm args[]={NIL,NIL,NIL},original;
        p=create(world->space,module,"update_twice",args,3);
        original=TUPLE3(p->htop,make_small(1),make_small(2),make_small(3)); p->htop+=4;
        p->reg[0]=original; p->reg[1]=make_small(10); p->reg[2]=make_small(20);
        CHECK(run(p)==LB_PROCESS_DONE);
        result=lb_process_result(p);
        CHECK(is_tuple(result) && result!=original && arityval(*tuple_val(result))==3);
        CHECK(tuple_val(result)[1]==make_small(10) && tuple_val(result)[2]==make_small(20) &&
              tuple_val(result)[3]==make_small(3));
        CHECK(tuple_val(original)[1]==make_small(1) && tuple_val(original)[2]==make_small(2) &&
              tuple_val(original)[3]==make_small(3));
        lb_process_destroy(p);
    }
    for(size_t index=1;index<=3;index+=2) {
        p=input(world->space,module); p->reg[0]=make_small(index);
        CHECK(run(p)==LB_PROCESS_DONE);
        CHECK(tuple_val(lb_process_result(p))[index]==p->reg[2]);
        CHECK(tuple_val(p->reg[1])[2]==make_small(0)); lb_process_destroy(p);
    }
    for(size_t i=0;i<6;++i) {
        p=input(world->space,module);
        if(i==0) p->reg[0]=make_small(0);
        if(i==1) p->reg[0]=make_small(-1);
        if(i==2) p->reg[0]=make_small(4);
        if(i==3) p->reg[0]=am_true;
        if(i==4) p->reg[1]=NIL;
        if(i==5) { p->reg[1]=make_tuple(p->htop); *p->htop++=make_arityval_zero(); }
        CHECK(run(p)==LB_PROCESS_EXCEPTION && p->exception_value==am_badarg);
        lb_process_destroy(p);
    }
    allocation_boundaries(world,module); work_boundaries(world,module);
    lb_engine_leave(engine);
    LbReclamation *reclamation;
    CHECK(lb_world_stop(world,&reclamation)==LB_WORLD_OK && lb_reclamation_poll(reclamation)==LB_WORLD_OK);
    CHECK(lb_reclamation_consume(reclamation)==LB_WORLD_OK);
    lb_reclamation_release(reclamation); lb_world_release(world);
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine);
    puts("CORE_SETELEMENT_OK execution=true copy=true badarg=true moving_roots=true reserved_credit=true allocation_prefixes=true work_prefixes=true balanced=true");
    return 0;
}
