/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#include "process_internal.h"
#include "world_internal.h"
#include "work_budget_internal.h"
#include "alloc_internal.h"
#include "big.h"
#include "opcodes.h"
#include <fenv.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while(0)
static LbProcess *create(LbWorld *world,LbCodeModule *module,const char *name)
{
    Eterm function,arg=make_small(0); LbCodeEntry *entry; LbProcess *p;
    CHECK(lb_atoms_find(world->space->atoms,name,strlen(name),&function)==LB_ATOM_OK);
    CHECK(lb_code_entry_acquire(world->space,module->name,function,1,&entry)==LB_CODE_OK);
    CHECK(lb_process_create(entry,&arg,1,256,&p)==LB_PROCESS_READY); lb_code_entry_release(entry); return p;
}
static LbProcessStatus run(LbProcess *p)
{
    LbProcessStatus status=LB_PROCESS_READY;
    for(size_t i=0;i<1000 && (status==LB_PROCESS_READY || status==LB_PROCESS_YIELDED);++i)
        status=lb_process_run(p,1,100);
    return status;
}
static void wide(Eterm term)
{ CHECK(lb_is_big(term) && *boxed_val(term)==_make_header(2,POS_BIG_SUBTAG) && boxed_val(term)[1]==1 && boxed_val(term)[2]==UINT64_MAX-1); }
static LbProcess *input(LbWorld *world,LbCodeModule *module,const char *function,int collect)
{
    LbProcess *p=create(world,module,function);
    p->reg[0]=p->reg[MAX_REG-1]=make_boxed(p->htop);
    p->htop[0]=_make_header(2,NEG_BIG_SUBTAG); p->htop[1]=1; p->htop[2]=UINT64_MAX-1; p->htop+=3;
    if(collect) while(p->htop<p->stop) *p->htop++=make_small(0);
    return p;
}
static void vectors(LbWorld *world,LbCodeModule *module)
{
    const Sint integers[]={MIN_SMALL,MAX_SMALL,-42,0,42};
    for(size_t i=0;i<sizeof(integers)/sizeof(*integers);++i) {
        LbProcess *p=create(world,module,"magnitude"); p->reg[0]=make_small(integers[i]);
        CHECK(run(p)==LB_PROCESS_DONE); Eterm result=lb_process_result(p);
        if(integers[i]==MIN_SMALL) CHECK(lb_is_big(result) && *boxed_val(result)==_make_header(1,POS_BIG_SUBTAG) && boxed_val(result)[1]==(Uint)MAX_SMALL+1);
        else CHECK(result==make_small(integers[i]<0?-integers[i]:integers[i]));
        lb_process_destroy(p);
    }
    const Uint bits[]={0,UINT64_C(0x8000000000000000),UINT64_C(0x3ff8000000000000),UINT64_C(0xbff8000000000000),
                       1,UINT64_C(0x8000000000000001),UINT64_C(0x7fefffffffffffff),UINT64_C(0xffefffffffffffff)};
    for(size_t i=0;i<sizeof(bits)/sizeof(*bits);++i) {
        LbProcess *p=create(world,module,"magnitude"); p->reg[0]=make_boxed(p->htop);
        p->htop[0]=_make_header(1,FLOAT_SUBTAG); p->htop[1]=bits[i]; p->htop+=2;
        Eterm original=p->reg[0]; Eterm *heap=p->htop;
        int rounding=fegetround(); CHECK(rounding!=-1 && !fesetround(FE_UPWARD));
        CHECK(run(p)==LB_PROCESS_DONE && fegetround()==FE_UPWARD && !fesetround(rounding));
        Eterm result=lb_process_result(p);
        CHECK(is_boxed(result) && *boxed_val(result)==_make_header(1,FLOAT_SUBTAG));
        CHECK(boxed_val(result)[1]==(bits[i]&UINT64_C(0x7fffffffffffffff)));
        if(!(bits[i]>>63) && bits[i]) CHECK(result==original && p->htop==heap);
        else CHECK(result!=original);
        lb_process_destroy(p);
    }
    LbProcess *p=input(world,module,"magnitude",0);
    *boxed_val(p->reg[0])=_make_header(2,POS_BIG_SUBTAG); Eterm original=p->reg[0]; Eterm *heap=p->htop;
    CHECK(run(p)==LB_PROCESS_DONE && lb_process_result(p)==original && p->htop==heap); lb_process_destroy(p);
    p=create(world,module,"guarded"); p->reg[0]=make_small(-42); CHECK(run(p)==LB_PROCESS_DONE);
    Eterm yes; CHECK(lb_atoms_find(world->space->atoms,"yes",3,&yes)==LB_ATOM_OK && lb_process_result(p)==yes);
    lb_process_destroy(p);
    for(unsigned guard=0;guard<2;++guard) for(unsigned bad=0;bad<2;++bad) {
        p=create(world,module,guard?"guarded":"magnitude"); p->reg[0]=bad?NIL:am_true;
        CHECK(run(p)==(guard?LB_PROCESS_DONE:LB_PROCESS_EXCEPTION));
        if(!guard) CHECK(p->exception_value==am_badarg);
        else { Eterm no; CHECK(lb_atoms_find(world->space->atoms,"no",2,&no)==LB_ATOM_OK && lb_process_result(p)==no); }
        lb_process_destroy(p);
    }
}
typedef struct { LbSystemAllocator backing; size_t remaining; } Prefix;
static void *allocate(void *context,size_t bytes)
{
    Prefix *prefix=context; if(!prefix->remaining) return NULL;
    --prefix->remaining; return prefix->backing.allocate(prefix->backing.context,bytes);
}
static void release(void *context,void *ptr)
{ Prefix *prefix=context; prefix->backing.release(prefix->backing.context,ptr); }
static void allocation_boundaries(LbWorld *world,LbCodeModule *module)
{
    LbAllocDomain *domain=world->space->domain; size_t baseline=domain->current;
    for(unsigned guard=0;guard<2;++guard) {
        int succeeded=0;
        for(size_t attempt=0;attempt<20 && !succeeded;++attempt) {
            LbProcess *p=input(world,module,guard?"guarded":"magnitude",1);
            Eterm *heap=p->htop,original=p->reg[0]; size_t live=domain->current,limit=domain->limit;
            Prefix prefix={domain->system,attempt?attempt-1:0};
            if(!attempt) domain->limit=live; else domain->system=(LbSystemAllocator){&prefix,allocate,release};
            LbProcessStatus status=run(p); domain->limit=limit; domain->system=prefix.backing;
            if(status==LB_PROCESS_DONE) {
                succeeded=1; CHECK(p->collections>0); if(!guard) wide(lb_process_result(p));
                CHECK(*boxed_val(p->reg[MAX_REG-1])==_make_header(2,NEG_BIG_SUBTAG));
            } else {
                CHECK(status==(attempt?LB_PROCESS_NO_MEMORY:LB_PROCESS_LIMIT));
                CHECK(p->htop==heap && p->reg[0]==original && domain->current==live);
            }
            lb_alloc_domain_begin_operation(domain); lb_process_destroy(p); CHECK(domain->current==baseline);
        }
        CHECK(succeeded);
    }
}
static void work_boundaries(LbWorld *world,LbCodeModule *module)
{
    LbAllocDomain *domain=world->space->domain; size_t baseline=domain->current;
    for(unsigned collect=0;collect<2;++collect) {
        uint64_t required=0;
        for(uint64_t pass=0;pass<=required+1;++pass) {
            LbProcess *p=input(world,module,"magnitude",collect);
            LbWorkBudget *budget=lb_work_budget_create(world); CHECK(budget);
            uint64_t credit=pass?pass-1:1000000;
            budget->limit=budget->remaining=credit; lb_work_domain_borrow(domain,budget);
            LbProcessStatus status=run(p);
            if(!pass) { CHECK(status==LB_PROCESS_DONE); required=credit-budget->remaining; CHECK(required>1 && required<50000); }
            else if(credit==required) CHECK(status==LB_PROCESS_DONE && budget->remaining==0);
            else CHECK(status==LB_PROCESS_WORK_LIMIT);
            if(status==LB_PROCESS_DONE) { wide(lb_process_result(p)); if(collect) CHECK(p->collections>0); }
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
    CHECK(loaded==LB_CODE_OK); free(bytes); vectors(world,module);
    LbProcess *p=input(world,module,"kept",0);
    for(size_t step=0;step<100 && *p->i!=op_i_bif1_body_sbd;++step) CHECK(lb_process_run(p,1,100)==LB_PROCESS_YIELDED);
    CHECK(*p->i==op_i_bif1_body_sbd && (p->i[3]&1));
    while(p->htop<p->stop) *p->htop++=make_small(0);
    size_t collections=p->collections; CHECK(run(p)==LB_PROCESS_DONE && p->collections>collections);
    wide(lb_process_result(p)); CHECK(*boxed_val(p->reg[MAX_REG-1])==_make_header(2,NEG_BIG_SUBTAG)); lb_process_destroy(p);
    allocation_boundaries(world,module); work_boundaries(world,module);
    p=create(world,module,"magnitude"); CHECK(lb_heap_reserve(p,LB_BIG_ARITY_MAX+1,MAX_REG));
    p->reg[0]=make_boxed(p->htop); *p->htop++=_make_header(LB_BIG_ARITY_MAX,NEG_BIG_SUBTAG);
    for(size_t i=0;i<LB_BIG_ARITY_MAX;++i) *p->htop++=UINT64_MAX;
    size_t reserved=p->heap_reserved=(size_t)(p->stop-p->htop); collections=p->collections;
    Eterm local=p->reg[0],result=lb_bif_abs_1(p,&local,NULL);
    CHECK(lb_is_big(result) && *boxed_val(result)==_make_header(LB_BIG_ARITY_MAX,POS_BIG_SUBTAG));
    CHECK(boxed_val(result)[1]==UINT64_MAX && boxed_val(result)[LB_BIG_ARITY_MAX]==UINT64_MAX);
    CHECK(p->collections>collections && p->heap_reserved==reserved && result!=p->reg[0]);
    CHECK(*boxed_val(p->reg[0])==_make_header(LB_BIG_ARITY_MAX,NEG_BIG_SUBTAG));
    p->reg[1]=result; CHECK(lb_process_collect(p,0)==LB_PROCESS_READY);
    CHECK(*boxed_val(p->reg[1])==_make_header(LB_BIG_ARITY_MAX,POS_BIG_SUBTAG)); lb_process_destroy(p);
    lb_engine_leave(engine); LbReclamation *reclamation;
    CHECK(lb_world_stop(world,&reclamation)==LB_WORLD_OK && lb_reclamation_poll(reclamation)==LB_WORLD_OK);
    CHECK(lb_reclamation_consume(reclamation)==LB_WORLD_OK); lb_reclamation_release(reclamation); lb_world_release(world);
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine);
    puts("CORE_ABS_OK min_small=true signed_zero=true float_bits=true positive_reuse=true badarg=true guards=true y_destination_gc=true source_unchanged=true allocation_prefixes=true work_prefixes=true balanced=true");
    return 0;
}
