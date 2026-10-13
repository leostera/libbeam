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
typedef enum { TEST_DIVIDE, TEST_REMAINDER } Operation;
static Operation operation;
static const char *primary_function(void)
{ return operation==TEST_REMAINDER?"remainder":"divide"; }
static LbProcess *create(LbWorld *world,LbCodeModule *module,const char *name)
{
    Eterm function,args[]={make_small(0),make_small(0)}; LbCodeEntry *entry; LbProcess *p;
    CHECK(lb_atoms_find(world->space->atoms,name,strlen(name),&function)==LB_ATOM_OK);
    CHECK(lb_code_entry_acquire(world->space,module->name,function,2,&entry)==LB_CODE_OK);
    CHECK(lb_process_create(entry,args,2,256,&p)==LB_PROCESS_READY); lb_code_entry_release(entry); return p;
}
static LbProcessStatus run(LbProcess *p)
{
    LbProcessStatus status=LB_PROCESS_READY;
    for(size_t i=0;i<1000 && (status==LB_PROCESS_READY || status==LB_PROCESS_YIELDED);++i)
        status=lb_process_run(p,1,100);
    return status;
}
static size_t parse(const char *hex,Eterm *words)
{
    size_t length=strlen(hex),size=(length+15)/16; CHECK(size && size<=64);
    memset(words,0,(size+1)*sizeof(Eterm)); words[0]=_make_header(size,POS_BIG_SUBTAG);
    for(size_t i=0;i<length;++i) {
        char ch=hex[length-1-i]; unsigned digit=ch>='0' && ch<='9'?(unsigned)(ch-'0'):(unsigned)(ch-'a'+10);
        CHECK(digit<16); words[1+i/16]|=(Uint)digit<<((i%16)*4);
    }
    CHECK(size==1 || words[size]); return size;
}
static Eterm canonical(Eterm *words,int negative)
{
    size_t size=lb_big_size(make_boxed(words));
    words[0]=_make_header(size,negative?NEG_BIG_SUBTAG:POS_BIG_SUBTAG);
    if(size==1 && words[1]<=(Uint)MAX_SMALL+(negative?1:0))
        return make_small(negative?-(Sint)words[1]:(Sint)words[1]);
    return make_boxed(words);
}
static void equal(Eterm actual,Eterm *expected,int negative)
{
    Eterm term=canonical(expected,negative);
    if(is_small(term)) CHECK(actual==term);
    else {
        CHECK(lb_is_big(actual) && boxed_val(actual)[0]==expected[0]);
        CHECK(!memcmp(boxed_val(actual)+1,expected+1,lb_big_size(term)*sizeof(Eterm)));
    }
}
static Eterm operand(LbProcess *p,Eterm *words,int negative)
{
    Eterm term=canonical(words,negative);
    if(is_small(term)) return term;
    size_t count=lb_big_size(term)+1; CHECK((size_t)(p->stop-p->htop)>=count);
    Eterm result=make_boxed(p->htop); memcpy(p->htop,words,count*sizeof(Eterm)); p->htop+=count; return result;
}
static void vectors(LbWorld *world,LbCodeModule *module,const char *path)
{
    FILE *file=fopen(path,"r"); CHECK(file); size_t count=0; int fields;
    char hex[4][1025];
    while((fields=fscanf(file,"%1024s %1024s %1024s %1024s",hex[0],hex[1],hex[2],hex[3]))==4) {
        Eterm x[65],y[65],expected_q[65],expected_r[65],q[65],r[65],scratch[130];
        parse(hex[0],x); parse(hex[1],y); parse(hex[2],expected_q); parse(hex[3],expected_r);
        for(unsigned signs=0;signs<4;++signs) {
            int xn=!!(signs&1),yn=!!(signs&2);
            canonical(x,xn); canonical(y,yn);
            LbBigDivision result;
            CHECK(lb_big_div_rem(make_boxed(x),make_boxed(y),q,r,scratch,130,&result));
            equal(result.quotient,expected_q,xn^yn); equal(result.remainder,expected_r,xn);
            LbProcess *p=create(world,module,primary_function());
            p->reg[0]=operand(p,x,xn); p->reg[1]=operand(p,y,yn);
            CHECK(run(p)==LB_PROCESS_DONE);
            equal(lb_process_result(p),operation==TEST_REMAINDER?expected_r:expected_q,operation==TEST_REMAINDER?xn:xn^yn);
            lb_process_destroy(p);
        }
        ++count;
    }
    CHECK(fields==EOF && !fclose(file) && count>300);
}
static void wide(Eterm term)
{ CHECK(lb_is_big(term) && lb_big_size(term)==2 && boxed_val(term)[1]==0 && boxed_val(term)[2]==UINT64_MAX); }
static void check_selected(Eterm term)
{
    if(operation==TEST_REMAINDER)
        CHECK(lb_is_big(term) && *boxed_val(term)==_make_header(1,POS_BIG_SUBTAG) && boxed_val(term)[1]==UINT64_MAX);
    else wide(term);
}
static LbProcess *input(LbWorld *world,LbCodeModule *module,const char *function,int collect)
{
    LbProcess *p=create(world,module,function);
    p->reg[0]=make_boxed(p->htop); p->htop[0]=_make_header(3,POS_BIG_SUBTAG);
    p->htop[1]=p->htop[2]=p->htop[3]=UINT64_MAX; p->htop+=4;
    p->reg[1]=make_boxed(p->htop); p->htop[0]=_make_header(2,POS_BIG_SUBTAG);
    p->htop[1]=p->htop[2]=1; p->htop+=3;
    if(collect) while(p->htop<p->stop) *p->htop++=make_small(0);
    return p;
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
    for(unsigned guard=0;guard<3;++guard) {
        int succeeded=0;
        for(size_t attempt=0;attempt<20 && !succeeded;++attempt) {
            LbProcess *p=input(world,module,guard==1?"guarded":primary_function(),guard!=2);
            if(guard==2) p->heap_reserved=(size_t)(p->stop-p->htop);
            size_t reserved=p->heap_reserved;
            Eterm *heap=p->htop,a=p->reg[0],b=p->reg[1];
            size_t live=domain->current,limit=domain->limit;
            Prefix prefix={domain->system,attempt?attempt-1:0};
            if(!attempt) domain->limit=live; else domain->system=(LbSystemAllocator){&prefix,allocate,release};
            Eterm q=make_small(123),r=make_small(456);
            LbProcessStatus status=guard==2?(lb_int_div_rem(p,a,b,&q,&r)?LB_PROCESS_DONE:p->status):run(p);
            domain->limit=limit; domain->system=prefix.backing;
            if(status==LB_PROCESS_DONE) {
                succeeded=1; CHECK(p->collections>0); if(!guard) check_selected(lb_process_result(p));
                if(guard==2) {
                    CHECK(p->heap_reserved==reserved); wide(q);
                    CHECK(lb_is_big(r) && lb_big_size(r)==1 && boxed_val(r)[1]==UINT64_MAX);
                    p->reg[0]=q; p->reg[1]=r;
                    CHECK(lb_process_collect(p,0)==LB_PROCESS_READY); wide(p->reg[0]);
                    CHECK(boxed_val(p->reg[1])[1]==UINT64_MAX);
                }
            } else {
                CHECK(status==(attempt?LB_PROCESS_NO_MEMORY:LB_PROCESS_LIMIT));
                if(guard==2) CHECK(q==make_small(123) && r==make_small(456) && p->heap_reserved==reserved);
                CHECK(p->htop==heap && p->reg[0]==a && p->reg[1]==b && domain->current==live);
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
            LbProcess *p=input(world,module,primary_function(),collect);
            LbWorkBudget *budget=lb_work_budget_create(world); CHECK(budget);
            uint64_t credit=pass?pass-1:1000000;
            budget->limit=budget->remaining=credit; lb_work_domain_borrow(domain,budget);
            LbProcessStatus status=run(p);
            if(!pass) { CHECK(status==LB_PROCESS_DONE); required=credit-budget->remaining; CHECK(required>1 && required<50000); }
            else if(credit==required) CHECK(status==LB_PROCESS_DONE && budget->remaining==0);
            else CHECK(status==LB_PROCESS_WORK_LIMIT);
            if(status==LB_PROCESS_DONE) { check_selected(lb_process_result(p)); if(collect) CHECK(p->collections>0); }
            lb_work_domain_borrow(domain,NULL); lb_work_domain_clear_failure(domain); lb_alloc_domain_begin_operation(domain);
            lb_process_destroy(p); lb_work_budget_release(budget); CHECK(domain->current==baseline);
        }
    }
}
static void quadratic_boundary(LbWorld *world,LbCodeModule *module)
{
    LbAllocDomain *domain=world->space->domain; size_t baseline=domain->current;
    LbProcess *p=create(world,module,primary_function()); CHECK(lb_heap_reserve(p,6002,MAX_REG));
    for(size_t i=0;i<2;++i) {
        size_t count=i?2000:4000; Eterm *words=p->htop;
        words[0]=_make_header(count,POS_BIG_SUBTAG);
        for(size_t j=1;j<=count;++j) words[j]=UINT64_MAX;
        p->reg[i]=make_boxed(words); p->htop+=count+1;
    }
    LbWorkBudget *budget=lb_work_budget_create(world); CHECK(budget && budget->limit==10000000);
    size_t live=domain->current; Eterm *heap=p->htop,q=make_small(123),r=make_small(456);
    lb_work_domain_borrow(domain,budget);
    CHECK(!lb_int_div_rem(p,p->reg[0],p->reg[1],&q,&r) && p->status==LB_PROCESS_WORK_LIMIT);
    CHECK(domain->current==live && p->htop==heap && q==make_small(123) && r==make_small(456));
    lb_work_domain_borrow(domain,NULL); lb_work_domain_clear_failure(domain); lb_alloc_domain_begin_operation(domain);
    lb_process_destroy(p); lb_work_budget_release(budget); CHECK(domain->current==baseline);
}
int main(int argc,char **argv)
{
    CHECK(argc==3 || (argc==4 && !strcmp(argv[3],"rem")));
    operation=argc==4?TEST_REMAINDER:TEST_DIVIDE;
    FILE *file; long length; CHECK((file=fopen(argv[1],"rb")) && !fseek(file,0,SEEK_END));
    length=ftell(file); CHECK(length>0 && !fseek(file,0,SEEK_SET)); unsigned char *bytes=malloc((size_t)length);
    CHECK(bytes && fread(bytes,1,(size_t)length,file)==(size_t)length && !fclose(file));
    LbEngine *engine; LbWorld *world; LbCodeModule *module; LbBeamError error;
    CHECK(lb_engine_create(NULL,&engine)==LB_ENGINE_OK && lb_world_create(engine,&world)==LB_WORLD_OK);
    lb_engine_enter(engine);
    LbCodeStatus loaded=lb_code_load(world->space,bytes,(size_t)length,&module,&error);
    if(loaded!=LB_CODE_OK) fprintf(stderr,"load=%d stage=%s offset=%zu\n",loaded,error.stage,error.offset);
    CHECK(loaded==LB_CODE_OK); free(bytes);
    vectors(world,module,argv[2]);
    {
        Eterm one[]={_make_header(1,POS_BIG_SUBTAG),1},zero[]={_make_header(1,POS_BIG_SUBTAG),0};
        Eterm q[2],r[2],scratch[3]; LbBigDivision result={make_small(123),make_small(456)};
        CHECK(!lb_big_div_rem(make_boxed(one),make_boxed(zero),q,r,scratch,3,&result));
        CHECK(!lb_big_div_rem(make_boxed(one),make_boxed(one),q,r,scratch,2,&result));
        CHECK(result.quotient==make_small(123) && result.remainder==make_small(456));
        LbProcess *p=create(world,module,primary_function());
        CHECK(lb_heap_reserve(p,LB_BIG_ARITY_MAX+1,MAX_REG));
        Eterm *words=p->htop; words[0]=_make_header(LB_BIG_ARITY_MAX,POS_BIG_SUBTAG);
        for(size_t i=1;i<=LB_BIG_ARITY_MAX;++i) words[i]=UINT64_MAX;
        p->reg[0]=make_boxed(words); p->htop+=LB_BIG_ARITY_MAX+1;
        CHECK(lb_int_div_rem(p,p->reg[0],make_small(-1),&result.quotient,&result.remainder));
        CHECK(lb_is_big(result.quotient) && lb_big_size(result.quotient)==LB_BIG_ARITY_MAX);
        CHECK((*boxed_val(result.quotient)&_TAG_HEADER_MASK)==NEG_BIG_SUBTAG);
        CHECK(boxed_val(result.quotient)[1]==UINT64_MAX && boxed_val(result.quotient)[LB_BIG_ARITY_MAX]==UINT64_MAX);
        CHECK(result.remainder==make_small(0)); lb_process_destroy(p);
    }
    {
        LbProcess *p=create(world,module,"guarded");
        p->reg[0]=make_small(operation==TEST_REMAINDER?85:84);
        p->reg[1]=make_small(operation==TEST_REMAINDER?43:2);
        Eterm yes; CHECK(run(p)==LB_PROCESS_DONE);
        CHECK(lb_atoms_find(world->space->atoms,"yes",3,&yes)==LB_ATOM_OK && lb_process_result(p)==yes);
        lb_process_destroy(p);
    }
    for(unsigned guard=0;guard<2;++guard) for(unsigned bad=0;bad<3;++bad) {
        LbProcess *p=create(world,module,guard?"guarded":primary_function());
        p->reg[0]=make_small(84); p->reg[1]=bad==0?make_small(0):bad==1?am_true:make_boxed(p->htop);
        if(bad==2) { double value=2.0; p->htop[0]=_make_header(1,FLOAT_SUBTAG); memcpy(p->htop+1,&value,sizeof(value)); p->htop+=2; }
        CHECK(run(p)==(guard?LB_PROCESS_DONE:LB_PROCESS_EXCEPTION));
        if(!guard) CHECK(p->exception_value==am_badarith);
        else { Eterm no; CHECK(lb_atoms_find(world->space->atoms,"no",2,&no)==LB_ATOM_OK && lb_process_result(p)==no); }
        lb_process_destroy(p);
    }
    LbProcess *p=input(world,module,"kept",0);
    BeamInstr opcode=operation==TEST_REMAINDER?op_i_rem_ssjd:op_i_int_div_jssd;
    size_t packed=operation==TEST_REMAINDER?3:1;
    for(size_t step=0;step<100 && *p->i!=opcode;++step)
        CHECK(lb_process_run(p,1,100)==LB_PROCESS_YIELDED);
    CHECK(*p->i==opcode && ((p->i[packed]>>BEAM_WIDE_SHIFT)&1));
    while(p->htop<p->stop) *p->htop++=make_small(0);
    size_t collections=p->collections; CHECK(run(p)==LB_PROCESS_DONE && p->collections>collections);
    check_selected(lb_process_result(p)); lb_process_destroy(p);
    allocation_boundaries(world,module); work_boundaries(world,module); quadratic_boundary(world,module);
    lb_engine_leave(engine);
    LbReclamation *reclamation;
    CHECK(lb_world_stop(world,&reclamation)==LB_WORLD_OK && lb_reclamation_poll(reclamation)==LB_WORLD_OK);
    CHECK(lb_reclamation_consume(reclamation)==LB_WORLD_OK); lb_reclamation_release(reclamation); lb_world_release(world);
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine);
    printf("CORE_DIV_REM_OK operation=%s python_vectors=true signs=true remainder=true badarith=true guards=true y_destination_gc=true allocation_prefixes=true work_prefixes=true joint_publication=true quadratic_precharge=true balanced=true\n",primary_function());
    return 0;
}
