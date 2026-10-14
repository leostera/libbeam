/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#include "process_internal.h"
#include "world_internal.h"
#include "work_budget_internal.h"
#include "alloc_internal.h"
#include "big.h"
#include "opcodes.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while(0)
static LbProcess *input(LbWorld *world,LbCodeModule *module,const char *text,size_t size,int binary)
{
    Eterm function,arg=make_small(0); LbCodeEntry *entry; LbProcess *p;
    const char *name=binary==2?"radix_parse":binary?"binary_parse":"parse";
    CHECK(lb_atoms_find(world->space->atoms,name,strlen(name),&function)==LB_ATOM_OK);
    CHECK(lb_code_entry_acquire(world->space,module->name,function,1,&entry)==LB_CODE_OK);
    if(binary) CHECK(lb_process_create_binary(entry,text,size,256,&p)==LB_PROCESS_READY);
    else {
        CHECK(lb_process_create(entry,&arg,1,256,&p)==LB_PROCESS_READY);
        if(size) CHECK(lb_heap_reserve(p,size*2,MAX_REG));
        Eterm list=NIL;
        for(size_t i=size;i>0;--i) { list=CONS(p->htop,make_small((unsigned char)text[i-1]),list); p->htop+=2; }
        p->reg[0]=list;
    }
    /* Call setup may legitimately discard dead X registers. Use an explicit
     * moving root to witness source immutability across the entire call. */
    lb_code_entry_release(entry); p->construction_root=p->reg[0]; return p;
}
static LbProcessStatus run(LbProcess *p)
{
    LbProcessStatus status=LB_PROCESS_READY;
    for(size_t i=0;i<1000 && (status==LB_PROCESS_READY || status==LB_PROCESS_YIELDED);++i)
        status=lb_process_run(p,1,100);
    return status;
}
static void wide(Eterm term)
{ CHECK(lb_is_big(term) && *boxed_val(term)==_make_header(2,POS_BIG_SUBTAG) && boxed_val(term)[1]==0 && boxed_val(term)[2]==1); }
static void preserved(LbProcess *p,const char *text,int binary)
{
    Eterm root=p->construction_root; size_t size=strlen(text);
    if(binary) {
        LbBitstringView view; CHECK(lb_bitstring_view(root,&view) && view.bit_offset==0 && view.bit_size==size*8);
        CHECK(!memcmp(view.data,text,size));
    } else {
        for(size_t i=0;i<size;++i) { CHECK(is_list(root) && list_val(root)[0]==make_small((unsigned char)text[i])); root=list_val(root)[1]; }
        CHECK(is_nil(root));
    }
}
static void vectors(LbWorld *world,LbCodeModule *module)
{
    const struct { const char *text; Sint result; } small[]={
        {"0",0},{"-0",0},{"+000",0},{"+123",123},{"-123",-123},
        {"576460752303423487",MAX_SMALL},{"-576460752303423488",MIN_SMALL}};
    const char *bad[]={"","+","-"," 1","1 ","1_2","--1","++1","1.0","1a","1\n"};
    for(unsigned binary=0;binary<3;++binary) {
        for(size_t i=0;i<sizeof(small)/sizeof(*small);++i) {
            LbProcess *p=input(world,module,small[i].text,strlen(small[i].text),binary);
            CHECK(run(p)==LB_PROCESS_DONE && lb_process_result(p)==make_small(small[i].result));
            preserved(p,small[i].text,binary); lb_process_destroy(p);
        }
        for(size_t i=0;i<sizeof(bad)/sizeof(*bad);++i) {
            LbProcess *p=input(world,module,bad[i],strlen(bad[i]),binary);
            CHECK(run(p)==LB_PROCESS_EXCEPTION && p->exception_value==am_badarg); lb_process_destroy(p);
        }
        LbProcess *p=input(world,module,"1\0",2,binary);
        CHECK(run(p)==LB_PROCESS_EXCEPTION && p->exception_value==am_badarg); lb_process_destroy(p);
        p=input(world,module,"-18446744073709551616",21,binary);
        CHECK(run(p)==LB_PROCESS_DONE); Eterm result=lb_process_result(p);
        CHECK(lb_is_big(result) && *boxed_val(result)==_make_header(2,NEG_BIG_SUBTAG) && boxed_val(result)[1]==0 && boxed_val(result)[2]==1);
        lb_process_destroy(p);
        p=input(world,module,"576460752303423488",18,binary);
        CHECK(run(p)==LB_PROCESS_DONE); result=lb_process_result(p);
        CHECK(lb_is_big(result) && *boxed_val(result)==_make_header(1,POS_BIG_SUBTAG) && boxed_val(result)[1]==(Uint)MAX_SMALL+1);
        lb_process_destroy(p);
    }
    const struct { Sint a,b; size_t count; Sint expected; int valid; } exotic[]={
        {304,0,1,0,1},{305,306,2,12,1},{-207,-206,2,12,1},{304,49,2,1,1},{299,49,2,0,0}};
    for(size_t i=0;i<sizeof(exotic)/sizeof(*exotic);++i) {
        LbProcess *p=input(world,module,"00",exotic[i].count,0);
        Eterm *head=list_val(p->reg[0]); head[0]=make_small(exotic[i].a);
        if(exotic[i].count==2) list_val(head[1])[0]=make_small(exotic[i].b);
        CHECK(run(p)==(exotic[i].valid?LB_PROCESS_DONE:LB_PROCESS_EXCEPTION));
        if(exotic[i].valid) CHECK(lb_process_result(p)==make_small(exotic[i].expected));
        else CHECK(p->exception_value==am_badarg);
        lb_process_destroy(p);
    }
    for(size_t digits=17;digits<=18;++digits) {
        LbProcess *p=input(world,module,"100000000000000000",digits,0);
        list_val(p->reg[0])[0]=make_small(305);
        CHECK(run(p)==(digits==17?LB_PROCESS_DONE:LB_PROCESS_EXCEPTION));
        if(digits==17) CHECK(lb_process_result(p)==make_small(INT64_C(10000000000000000)));
        else CHECK(p->exception_value==am_badarg);
        lb_process_destroy(p);
    }
    for(unsigned invalid=0;invalid<5;++invalid) {
        LbProcess *p=input(world,module,"1",1,0);
        if(invalid==0) list_val(p->reg[0])[1]=make_small(2);
        if(invalid==1) list_val(p->reg[0])[0]=am_true;
        if(invalid==2) list_val(p->reg[0])[0]=make_small(-1);
        if(invalid==3) list_val(p->reg[0])[0]=make_small(256);
        if(invalid==4) p->reg[0]=am_true;
        CHECK(run(p)==LB_PROCESS_EXCEPTION && p->exception_value==am_badarg); lb_process_destroy(p);
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
static void boundaries(LbWorld *world,LbCodeModule *module)
{
    LbAllocDomain *domain=world->space->domain; size_t baseline=domain->current;
    const char *text="18446744073709551616";
    for(unsigned binary=0;binary<3;++binary) {
        int succeeded=0;
        for(size_t attempt=0;attempt<20 && !succeeded;++attempt) {
            LbProcess *p=input(world,module,text,strlen(text),binary);
            /* Enter the real native trampoline before injecting BIF failure.
             * Call setup may legitimately collect before a later failure. */
            for(size_t step=0;step<100 && *p->i!=op_call_bif_W;++step)
                CHECK(lb_process_run(p,1,100)==LB_PROCESS_YIELDED);
            CHECK(*p->i==op_call_bif_W);
            while(p->htop<p->stop) *p->htop++=make_small(0);
            Eterm *heap=p->htop,original=p->reg[0]; size_t live=domain->current,limit=domain->limit,collections=p->collections;
            Prefix prefix={domain->system,attempt?attempt-1:0};
            if(!attempt) domain->limit=live; else domain->system=(LbSystemAllocator){&prefix,allocate,release};
            LbProcessStatus status=run(p); domain->limit=limit; domain->system=prefix.backing;
            if(status==LB_PROCESS_DONE) {
                succeeded=1; CHECK(p->collections>collections); wide(lb_process_result(p)); preserved(p,text,binary);
            } else {
                if(status!=(attempt?LB_PROCESS_NO_MEMORY:LB_PROCESS_LIMIT))
                    fprintf(stderr,"binary=%u allocation_prefix=%zu status=%d expected=%d\n",binary,attempt,status,attempt?LB_PROCESS_NO_MEMORY:LB_PROCESS_LIMIT);
                CHECK(status==(attempt?LB_PROCESS_NO_MEMORY:LB_PROCESS_LIMIT));
                CHECK(p->htop==heap && p->reg[0]==original && domain->current==live);
            }
            lb_alloc_domain_begin_operation(domain); lb_process_destroy(p); CHECK(domain->current==baseline);
        }
        CHECK(succeeded);
        /* Separately cover memory denial during the native call setup. */
        LbProcess *setup=input(world,module,text,strlen(text),binary);
        while(setup->htop<setup->stop) *setup->htop++=make_small(0);
        size_t limit=domain->limit; domain->limit=domain->current;
        CHECK(run(setup)==LB_PROCESS_LIMIT); domain->limit=limit;
        preserved(setup,text,binary); lb_alloc_domain_begin_operation(domain);
        lb_process_destroy(setup); CHECK(domain->current==baseline);
        for(unsigned collect=0;collect<2;++collect) {
            uint64_t required=0;
            for(uint64_t pass=0;pass<=required+1;++pass) {
                LbProcess *p=input(world,module,text,strlen(text),binary);
                if(collect) while(p->htop<p->stop) *p->htop++=make_small(0);
                LbWorkBudget *budget=lb_work_budget_create(world); CHECK(budget);
                uint64_t credit=pass?pass-1:1000000;
                budget->limit=budget->remaining=credit; lb_work_domain_borrow(domain,budget);
                LbProcessStatus status=run(p);
                if(!pass) { CHECK(status==LB_PROCESS_DONE); required=credit-budget->remaining; CHECK(required>1 && required<50000); }
                else if(credit==required) CHECK(status==LB_PROCESS_DONE && budget->remaining==0);
                else CHECK(status==LB_PROCESS_WORK_LIMIT);
                if(status==LB_PROCESS_DONE) { wide(lb_process_result(p)); preserved(p,text,binary); }
                lb_work_domain_borrow(domain,NULL); lb_work_domain_clear_failure(domain); lb_alloc_domain_begin_operation(domain);
                lb_process_destroy(p); lb_work_budget_release(budget); CHECK(domain->current==baseline);
            }
        }
    }
}
static void long_inputs(LbWorld *world,LbCodeModule *module)
{
    LbAllocDomain *domain=world->space->domain; size_t baseline=domain->current;
    char *text=malloc(1300000); CHECK(text); memset(text,'9',1300000);
    LbProcess *p=input(world,module,text,1300000,1);
    CHECK(run(p)==LB_PROCESS_EXCEPTION && p->exception_value==am_system_limit); lb_process_destroy(p);
    p=input(world,module,text,20000,0); Eterm *heap=p->htop,original=p->reg[0];
    LbWorkBudget *budget=lb_work_budget_create(world); CHECK(budget);
    budget->limit=budget->remaining=100000; lb_work_domain_borrow(domain,budget);
    size_t live=domain->current;
    CHECK(run(p)==LB_PROCESS_WORK_LIMIT && p->htop==heap && p->reg[0]==original && domain->current==live);
    lb_work_domain_borrow(domain,NULL); lb_work_domain_clear_failure(domain); lb_alloc_domain_begin_operation(domain);
    lb_process_destroy(p); lb_work_budget_release(budget);
    memset(text,'0',5000); p=input(world,module,text,5000,0);
    CHECK(run(p)==LB_PROCESS_DONE && lb_process_result(p)==make_small(0)); lb_process_destroy(p);
    free(text); CHECK(domain->current==baseline);
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
    CHECK(loaded==LB_CODE_OK); free(bytes); vectors(world,module); boundaries(world,module); long_inputs(world,module);
    lb_engine_leave(engine); LbReclamation *reclamation;
    CHECK(lb_world_stop(world,&reclamation)==LB_WORLD_OK && lb_reclamation_poll(reclamation)==LB_WORLD_OK);
    CHECK(lb_reclamation_consume(reclamation)==LB_WORLD_OK); lb_reclamation_release(reclamation); lb_world_release(world);
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine);
    puts("CORE_LIST_INTEGER_OK lists=true binary_regression=true syntax=true proper_list=true small_big_boundary=true moving_roots=true allocation_prefixes=true work_prefixes=true long_input_charged=true balanced=true");
    return 0;
}
