/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#include "process_internal.h"
#include "world_internal.h"
#include "work_budget_internal.h"
#include "alloc_internal.h"
#include "flatmap.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while(0)
static LbProcess *create(LbWorld *world,LbCodeModule *module,const char *name,const Eterm *args,size_t arity)
{
    Eterm function; LbCodeEntry *entry; LbProcess *p;
    CHECK(lb_atoms_find(world->space->atoms,name,strlen(name),&function)==LB_ATOM_OK);
    CHECK(lb_code_entry_acquire(world->space,module->name,function,arity,&entry)==LB_CODE_OK);
    CHECK(lb_process_create(entry,args,arity,512,&p)==LB_PROCESS_READY); lb_code_entry_release(entry); return p;
}
static LbProcessStatus run(LbProcess *p)
{
    LbProcessStatus status=LB_PROCESS_READY;
    for(size_t i=0;i<1000 && (status==LB_PROCESS_READY || status==LB_PROCESS_YIELDED);++i)
        status=lb_process_run(p,1,100);
    return status;
}
static Eterm atom(LbWorld *world,const char *name)
{ Eterm value; CHECK(lb_atoms_find(world->space->atoms,name,strlen(name),&value)==LB_ATOM_OK); return value; }
static void vectors(LbWorld *world,LbCodeModule *module)
{
    const char *names[]={"flat","large","guard_large"};
    for(size_t n=0;n<3;++n) for(unsigned hit=0;hit<2;++hit) {
        Eterm key=make_small(hit?1:99); LbProcess *p=create(world,module,names[n],&key,1);
        CHECK(run(p)==LB_PROCESS_DONE);
        CHECK(lb_process_result(p)==(n==2?atom(world,hit?"yes":"no"):hit?am_true:am_false)); lb_process_destroy(p);
    }
    Eterm value=make_small(123); LbProcess *p=create(world,module,"empty",&value,1);
    CHECK(run(p)==LB_PROCESS_DONE && lb_process_result(p)==am_false); lb_process_destroy(p);
    p=create(world,module,"badmap",&value,1);
    CHECK(run(p)==LB_PROCESS_EXCEPTION && is_tuple(p->exception_value));
    CHECK(arityval(*tuple_val(p->exception_value))==2 && tuple_val(p->exception_value)[1]==am_badmap && tuple_val(p->exception_value)[2]==value);
    lb_process_destroy(p);
    p=create(world,module,"cardinality",&value,1);
    CHECK(run(p)==LB_PROCESS_EXCEPTION && is_tuple(p->exception_value));
    CHECK(arityval(*tuple_val(p->exception_value))==2 && tuple_val(p->exception_value)[1]==am_badmap && tuple_val(p->exception_value)[2]==value);
    lb_process_destroy(p);
    p=create(world,module,"guard_size",&value,1);
    CHECK(run(p)==LB_PROCESS_DONE && lb_process_result(p)==atom(world,"no")); lb_process_destroy(p);
    Eterm args[]={make_small(1),value}; p=create(world,module,"guarded",args,2);
    CHECK(run(p)==LB_PROCESS_DONE && lb_process_result(p)==atom(world,"no")); lb_process_destroy(p);
    /* Binary keys are outside the admitted HAMT hashing profile. Neither
     * body nor guard may convert that limitation into a guest false. */
    for(unsigned guard=0;guard<2;++guard) {
        LbCodeEntry *entry;
        CHECK(lb_code_entry_acquire(world->space,module->name,atom(world,guard?"guard_large":"large"),1,&entry)==LB_CODE_OK);
        CHECK(lb_process_create_binary(entry,"key",3,512,&p)==LB_PROCESS_READY); lb_code_entry_release(entry);
        CHECK(run(p)==LB_PROCESS_REFUSED); lb_process_destroy(p);
    }
}
static void *deny(void *context,size_t bytes)
{ (void)context; (void)bytes; return NULL; }
static void allocation_free_lookup(LbWorld *world,LbCodeModule *module)
{
    LbAllocDomain *domain=world->space->domain; size_t baseline=domain->current;
    const struct { const char *name; size_t arity; Eterm expected; } cases[]={
        {"present",2,am_true},{"guarded",2,atom(world,"yes")},{"cardinality",1,make_small(1)}};
    for(size_t operation=0;operation<sizeof(cases)/sizeof(*cases);++operation) {
        for(unsigned mode=0;mode<2;++mode) {
            Eterm args[]={make_small(0),make_small(0)};
            LbProcess *p=create(world,module,cases[operation].name,args,cases[operation].arity);
            p->reg[1]=make_small(0);
            for(size_t depth=0;depth<40;++depth) for(size_t i=0;i<2;++i) {
                CHECK(p->stop-p->htop>=3);
                p->reg[i]=TUPLE2(p->htop,p->reg[i],make_small(1)); p->htop+=3;
            }
            Eterm keys=TUPLE1(p->htop,p->reg[1]); p->htop+=2;
            Eterm *map=p->htop; map[0]=LB_FLATMAP_HEADER; map[1]=1; map[2]=keys; map[3]=am_true;
            p->reg[1]=make_boxed(map); p->htop+=4;
            if(cases[operation].arity==1) p->reg[0]=p->reg[1];
            Eterm *heap=p->htop; size_t live=domain->current,limit=domain->limit;
            LbSystemAllocator system=domain->system;
            if(!mode) domain->limit=live; else domain->system.allocate=deny;
            LbProcessStatus status=run(p); domain->limit=limit; domain->system=system;
            CHECK(p->htop==heap && domain->current==live);
            CHECK(status==LB_PROCESS_DONE && lb_alloc_domain_failure(domain)==LB_ALLOC_OK);
            CHECK(lb_process_result(p)==cases[operation].expected);
            lb_alloc_domain_begin_operation(domain); lb_process_destroy(p); CHECK(domain->current==baseline);
        }
    }
}
static void work_boundaries(LbWorld *world,LbCodeModule *module)
{
    LbAllocDomain *domain=world->space->domain; size_t baseline=domain->current;
    const char *names[]={"flat","large","guard_large","size_empty","size_flat","size_large","guard_size_large"};
    const Eterm sizes[]={make_small(0),make_small(2),make_small(33),atom(world,"yes")};
    for(size_t n=0;n<sizeof(names)/sizeof(*names);++n) for(unsigned hit=0;hit<2;++hit) {
        Eterm expected=n>=3?sizes[n-3]:n==2?atom(world,hit?"yes":"no"):hit?am_true:am_false;
        uint64_t required=0;
        for(uint64_t pass=0;pass<=required+1;++pass) {
            Eterm key=make_small(hit?1:99); LbProcess *p=create(world,module,names[n],&key,1);
            LbWorkBudget *budget=lb_work_budget_create(world); CHECK(budget);
            uint64_t credit=pass?pass-1:1000000;
            budget->limit=budget->remaining=credit; lb_work_domain_borrow(domain,budget);
            LbProcessStatus status=run(p);
            if(!pass) { CHECK(status==LB_PROCESS_DONE); required=credit-budget->remaining; CHECK(required>1 && required<10000); }
            else if(credit==required) CHECK(status==LB_PROCESS_DONE && budget->remaining==0);
            else CHECK(status==LB_PROCESS_WORK_LIMIT);
            if(status==LB_PROCESS_DONE) CHECK(lb_process_result(p)==expected);
            lb_work_domain_borrow(domain,NULL); lb_work_domain_clear_failure(domain); lb_alloc_domain_begin_operation(domain);
            lb_process_destroy(p); lb_work_budget_release(budget); CHECK(domain->current==baseline);
        }
    }
}
int main(int argc,char **argv)
{
    FILE *file; long length; CHECK(argc==2 && (file=fopen(argv[1],"rb")) && !fseek(file,0,SEEK_END));
    length=ftell(file); CHECK(length>0 && !fseek(file,0,SEEK_SET)); unsigned char *bytes=malloc((size_t)length);
    CHECK(bytes && fread(bytes,1,(size_t)length,file)==(size_t)length && !fclose(file));
    LbEngine *engine; LbWorld *world; LbCodeModule *module; LbBeamError error;
    CHECK(lb_engine_create(NULL,&engine)==LB_ENGINE_OK && lb_world_create(engine,&world)==LB_WORLD_OK); lb_engine_enter(engine);
    LbCodeStatus loaded=lb_code_load(world->space,bytes,(size_t)length,&module,&error);
    if(loaded!=LB_CODE_OK) fprintf(stderr,"load=%d stage=%s offset=%zu\n",loaded,error.stage,error.offset);
    CHECK(loaded==LB_CODE_OK); free(bytes); vectors(world,module); allocation_free_lookup(world,module); work_boundaries(world,module);
    lb_engine_leave(engine); LbReclamation *reclamation;
    CHECK(lb_world_stop(world,&reclamation)==LB_WORLD_OK && lb_reclamation_poll(reclamation)==LB_WORLD_OK);
    CHECK(lb_reclamation_consume(reclamation)==LB_WORLD_OK); lb_reclamation_release(reclamation); lb_world_release(world);
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine);
    puts("CORE_MAP_KEY_OK flatmap=true hamt=true missing=true badmap=true guards=true profile_refusal=true allocation_free_lookup=true cardinality=true work_prefixes=true balanced=true");
    return 0;
}
