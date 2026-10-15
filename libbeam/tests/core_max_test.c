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
static int minimum;
static const char *operation(void) { return minimum?"minimum":"maximum"; }
static LbProcess *create(LbCodeSpace *space,LbCodeModule *module,const char *name,
                         const Eterm *args,size_t arity)
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
static LbProcess *execute(LbCodeSpace *space,LbCodeModule *module,const char *name,
                         const Eterm *args,size_t arity,LbProcessStatus expected)
{
    LbProcess *p=create(space,module,name,args,arity);
    LbProcessStatus status=run(p);
    if(status!=expected) fprintf(stderr,"%s status=%d expected=%d\n",name,status,expected);
    CHECK(status==expected); return p;
}
static void *deny(void *context,size_t size)
{ (void)context; (void)size; return NULL; }
static void scratch_failures(LbWorld *world,LbCodeModule *module)
{
    LbAllocDomain *domain=world->space->domain;
    size_t baseline=domain->current;
    for(unsigned mode=0;mode<3;++mode) {
        Eterm args[]={make_small(0),make_small(0)};
        LbProcess *p=create(world->space,module,operation(),args,2);
        for(size_t i=0;i<10;++i) for(size_t j=0;j<2;++j) {
            CHECK(p->stop-p->htop>=3);
            p->reg[j]=TUPLE2(p->htop,p->reg[j],make_small(1)); p->htop+=3;
        }
        Eterm first=p->reg[0]; Eterm *heap=p->htop;
        size_t live=domain->current,limit=domain->limit;
        LbSystemAllocator system=domain->system;
        if(mode==1) domain->limit=live;
        if(mode==2) domain->system.allocate=deny;
        LbProcessStatus status=run(p);
        domain->system=system; domain->limit=limit;
        CHECK(status==(mode==0?LB_PROCESS_DONE:mode==1?LB_PROCESS_LIMIT:LB_PROCESS_NO_MEMORY));
        CHECK(p->htop==heap && domain->current==live && p->reg[0]==first);
        if(mode==0) CHECK(lb_process_result(p)==first);
        lb_alloc_domain_begin_operation(domain); lb_process_destroy(p);
        CHECK(domain->current==baseline);
    }
}
static void work_boundary(LbWorld *world,LbCodeModule *module)
{
    LbCodeSpace *space=world->space;
    size_t baseline=lb_alloc_domain_usage(space->domain).current;
    Eterm args[]={make_small(1),make_small(2)};
    uint64_t required=0;
    for(uint64_t pass=0;pass<=required+1;++pass) {
        LbProcess *p=create(space,module,operation(),args,2);
        LbWorkBudget *budget=lb_work_budget_create(world); CHECK(budget);
        uint64_t credit=pass?pass-1:1000000;
        budget->limit=budget->remaining=credit;
        lb_work_domain_borrow(space->domain,budget);
        LbProcessStatus status=run(p);
        if(!pass) { CHECK(status==LB_PROCESS_DONE); required=credit-budget->remaining; CHECK(required>1 && required<10000); }
        else if(credit==required) { CHECK(status==LB_PROCESS_DONE && budget->remaining==0); }
        else CHECK(status==LB_PROCESS_WORK_LIMIT);
        if(status==LB_PROCESS_DONE) CHECK(lb_process_result(p)==args[minimum?0:1]);
        lb_work_domain_borrow(space->domain,NULL); lb_work_domain_clear_failure(space->domain);
        lb_alloc_domain_begin_operation(space->domain);
        lb_process_destroy(p); lb_work_budget_release(budget);
        CHECK(lb_alloc_domain_usage(space->domain).current==baseline);
    }
}
int main(int argc,char **argv)
{
    CHECK(argc==2 || (argc==3 && !strcmp(argv[2],"min"))); minimum=argc==3;
    FILE *file; long length; CHECK((file=fopen(argv[1],"rb")) && !fseek(file,0,SEEK_END));
    length=ftell(file); CHECK(length>0 && !fseek(file,0,SEEK_SET));
    unsigned char *bytes=malloc((size_t)length);
    CHECK(bytes && fread(bytes,1,(size_t)length,file)==(size_t)length && !fclose(file));
    LbEngine *engine; LbWorld *world; LbCodeSpace *space; LbCodeModule *module; LbBeamError error;
    CHECK(lb_engine_create(NULL,&engine)==LB_ENGINE_OK && lb_world_create(engine,&world)==LB_WORLD_OK);
    lb_engine_enter(engine); space=world->space;
    LbCodeStatus loaded=lb_code_load(space,bytes,(size_t)length,&module,&error);
    if(loaded!=LB_CODE_OK) fprintf(stderr,"load=%d stage=%s offset=%zu\n",loaded,error.stage,error.offset);
    CHECK(loaded==LB_CODE_OK); free(bytes);
    const Eterm pairs[][2]={{make_small(-2),make_small(3)},{make_small(9),make_small(1)},
                           {make_small(5),make_small(5)},{am_false,am_true},{am_true,make_small(99)}};
    const Eterm wanted[]={make_small(3),make_small(9),make_small(5),am_true,am_true};
    const Eterm minima[]={make_small(-2),make_small(1),make_small(5),am_false,make_small(99)};
    for(size_t i=0;i<sizeof(wanted)/sizeof(*wanted);++i) {
        LbProcess *p=execute(space,module,operation(),pairs[i],2,LB_PROCESS_DONE);
        CHECK(lb_process_result(p)==(minimum?minima[i]:wanted[i])); lb_process_destroy(p);
    }
    LbProcess *p=execute(space,module,"float_first",NULL,0,LB_PROCESS_DONE);
    Eterm result=lb_process_result(p); CHECK(is_boxed(result) && (*boxed_val(result)&_TAG_HEADER_MASK)==FLOAT_SUBTAG);
    double value; memcpy(&value,boxed_val(result)+1,sizeof(value)); CHECK(value==1.0); lb_process_destroy(p);
    p=execute(space,module,"integer_first",NULL,0,LB_PROCESS_DONE);
    CHECK(lb_process_result(p)==make_small(1)); lb_process_destroy(p);
    p=execute(space,module,"tuple_refusal",NULL,0,LB_PROCESS_REFUSED); lb_process_destroy(p);
    p=execute(space,module,"guard_refusal",NULL,0,LB_PROCESS_REFUSED); lb_process_destroy(p);
    scratch_failures(world,module);
    work_boundary(world,module);
    lb_engine_leave(engine);
    LbReclamation *reclamation;
    CHECK(lb_world_stop(world,&reclamation)==LB_WORLD_OK && lb_reclamation_poll(reclamation)==LB_WORLD_OK);
    CHECK(lb_reclamation_consume(reclamation)==LB_WORLD_OK);
    lb_reclamation_release(reclamation); lb_world_release(world);
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine);
    printf("CORE_EXTREMUM_OK operation=%s numeric=true atom=true rank=true first_operand_tie=true profile_refusal=true guard_refusal=true exact_work_boundary=true scratch_oom=true scratch_memory_limit=true balanced=true\n",operation());
    return 0;
}
