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
static LbProcess *create(LbWorld *world,LbCodeModule *module,const char *name,const Eterm *args,size_t arity)
{
    Eterm function; LbCodeEntry *entry; LbProcess *p;
    CHECK(lb_atoms_find(world->space->atoms,name,strlen(name),&function)==LB_ATOM_OK);
    CHECK(lb_code_entry_acquire(world->space,module->name,function,arity,&entry)==LB_CODE_OK);
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
static int big_is_negative(Eterm term)
{ return (*boxed_val(term)&_TAG_HEADER_MASK)==NEG_BIG_SUBTAG; }
static void wide(Eterm term,int negative)
{
    CHECK(lb_is_big(term) && lb_big_size(term)==2);
    CHECK(!!big_is_negative(term)==!!negative);
    CHECK(boxed_val(term)[1]==1 && boxed_val(term)[2]==UINT64_MAX-1);
}
static void digit_vectors(void)
{
    Uint random=1234567;
    for(size_t i=0;i<512;++i) {
        random=random*6364136223846793005ULL+1; Uint a=random;
        random=random*6364136223846793005ULL+1; Uint b=random;
        if(i<4) { a=i; b=UINT64_MAX; }
        Eterm x[]={_make_header(1,i&1?NEG_BIG_SUBTAG:POS_BIG_SUBTAG),a};
        Eterm y[]={_make_header(1,i&2?NEG_BIG_SUBTAG:POS_BIG_SUBTAG),b},out[3];
        Eterm result=lb_big_times(make_boxed(x),make_boxed(y),out);
        __uint128_t expected=(__uint128_t)a*b; int negative=!!(i&1)^!!(i&2);
        if(is_small(result)) {
            Sint actual=signed_val(result); Uint magnitude=actual<0?-(Uint)actual:(Uint)actual;
            CHECK(expected==magnitude && (!magnitude || (actual<0)==negative));
        } else {
            CHECK(lb_is_big(result) && !!big_is_negative(result)==negative);
            CHECK(out[1]==(Uint)expected && lb_big_size(result)==((expected>>64)?2:1));
            if(expected>>64) CHECK(out[2]==(Uint)(expected>>64));
        }
    }
}
static LbProcess *input(LbWorld *world,LbCodeModule *module,const char *function,int collect)
{
    Eterm args[]={make_small(0),make_small(0)};
    LbProcess *p=create(world,module,function,args,2);
    for(size_t i=0;i<2;++i) {
        p->htop[0]=_make_header(1,POS_BIG_SUBTAG); p->htop[1]=UINT64_MAX;
        p->reg[i]=make_boxed(p->htop); p->htop+=2;
    }
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
    for(unsigned guard=0;guard<2;++guard) {
        int succeeded=0;
        for(size_t attempt=0;attempt<20 && !succeeded;++attempt) {
            LbProcess *p=input(world,module,guard?"guarded":"product",1);
            Eterm *heap=p->htop,a=p->reg[0],b=p->reg[1];
            size_t live=domain->current,limit=domain->limit;
            Prefix prefix={domain->system,attempt?attempt-1:0};
            if(!attempt) domain->limit=live;
            else domain->system=(LbSystemAllocator){&prefix,allocate,release};
            LbProcessStatus status=run(p);
            domain->limit=limit; domain->system=prefix.backing;
            if(status==LB_PROCESS_DONE) {
                succeeded=1; CHECK(p->collections>0);
                if(!guard) wide(lb_process_result(p),0);
            } else {
                CHECK(status==(attempt?LB_PROCESS_NO_MEMORY:LB_PROCESS_LIMIT));
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
            LbProcess *p=input(world,module,"product",collect);
            LbWorkBudget *budget=lb_work_budget_create(world); CHECK(budget);
            uint64_t credit=pass?pass-1:1000000;
            budget->limit=budget->remaining=credit; lb_work_domain_borrow(domain,budget);
            LbProcessStatus status=run(p);
            if(!pass) { CHECK(status==LB_PROCESS_DONE); required=credit-budget->remaining; CHECK(required>1 && required<50000); }
            else if(credit==required) CHECK(status==LB_PROCESS_DONE && budget->remaining==0);
            else CHECK(status==LB_PROCESS_WORK_LIMIT);
            if(status==LB_PROCESS_DONE) { wide(lb_process_result(p),0); if(collect) CHECK(p->collections>0); }
            lb_work_domain_borrow(domain,NULL); lb_work_domain_clear_failure(domain); lb_alloc_domain_begin_operation(domain);
            lb_process_destroy(p); lb_work_budget_release(budget); CHECK(domain->current==baseline);
        }
    }
}
static void size_and_quadratic_boundaries(LbWorld *world,LbCodeModule *module)
{
    LbAllocDomain *domain=world->space->domain; size_t baseline=domain->current;
    Eterm args[]={make_small(0),make_small(0)};
    LbProcess *p=create(world,module,"product",args,2);
    CHECK(lb_heap_reserve(p,8002,MAX_REG));
    for(size_t i=0;i<2;++i) {
        Eterm *words=p->htop; words[0]=_make_header(4000,POS_BIG_SUBTAG);
        for(size_t j=1;j<=4000;++j) words[j]=UINT64_MAX;
        p->reg[i]=make_boxed(words); p->htop+=4001;
    }
    LbWorkBudget *budget=lb_work_budget_create(world); CHECK(budget && budget->limit==10000000);
    size_t live=domain->current; Eterm *heap=p->htop;
    lb_work_domain_borrow(domain,budget);
    CHECK(is_non_value(lb_mixed_times(p,p->reg[0],p->reg[1])) && p->status==LB_PROCESS_WORK_LIMIT);
    CHECK(domain->current==live && p->htop==heap);
    lb_work_domain_borrow(domain,NULL); lb_work_domain_clear_failure(domain); lb_alloc_domain_begin_operation(domain);
    lb_process_destroy(p); lb_work_budget_release(budget); CHECK(domain->current==baseline);

    p=create(world,module,"product",args,2);
    CHECK(lb_heap_reserve(p,LB_BIG_ARITY_MAX+1,MAX_REG));
    Eterm *words=p->htop; memset(words,0,(LB_BIG_ARITY_MAX+1)*sizeof(Eterm));
    words[0]=_make_header(LB_BIG_ARITY_MAX,POS_BIG_SUBTAG); words[LB_BIG_ARITY_MAX]=1;
    p->reg[0]=make_boxed(words); p->htop+=LB_BIG_ARITY_MAX+1;
    Eterm result=lb_mixed_times(p,p->reg[0],make_small(1));
    CHECK(lb_is_big(result) && lb_big_size(result)==LB_BIG_ARITY_MAX && boxed_val(result)[LB_BIG_ARITY_MAX]==1);
    /* Same admitted maximum-size input, now requiring one additional limb. */
    boxed_val(p->reg[0])[LB_BIG_ARITY_MAX]=UINT64_MAX; heap=p->htop;
    result=lb_mixed_times(p,p->reg[0],make_small(2));
    CHECK(is_non_value(result) && p->freason==LB_EXC_SYSTEM_LIMIT && p->htop==heap);
    lb_process_destroy(p); CHECK(domain->current==baseline);
}
int main(int argc,char **argv)
{
    digit_vectors();
    FILE *file; long length; CHECK(argc==2 && (file=fopen(argv[1],"rb")) && !fseek(file,0,SEEK_END));
    length=ftell(file); CHECK(length>0 && !fseek(file,0,SEEK_SET)); unsigned char *bytes=malloc((size_t)length);
    CHECK(bytes && fread(bytes,1,(size_t)length,file)==(size_t)length && !fclose(file));
    LbEngine *engine; LbWorld *world; LbCodeModule *module; LbBeamError error;
    CHECK(lb_engine_create(NULL,&engine)==LB_ENGINE_OK && lb_world_create(engine,&world)==LB_WORLD_OK);
    lb_engine_enter(engine);
    LbCodeStatus loaded=lb_code_load(world->space,bytes,(size_t)length,&module,&error);
    if(loaded!=LB_CODE_OK) fprintf(stderr,"load=%d stage=%s offset=%zu\n",loaded,error.stage,error.offset);
    CHECK(loaded==LB_CODE_OK); free(bytes);
    const char *names[]={"small","wide","large","negative","floating","float_overflow","huge_float","bad_number","guard_yes","guard_bad"};
    for(size_t i=0;i<10;++i) {
        LbProcess *p=create(world,module,names[i],NULL,0);
        int rounding=fegetround(); CHECK(rounding!=-1 && !fesetround(FE_UPWARD));
        LbProcessStatus status=run(p); CHECK(fegetround()==FE_UPWARD && !fesetround(rounding));
        if(i>=5 && i<=7) CHECK(status==LB_PROCESS_EXCEPTION && p->exception_value==am_badarith);
        else {
            CHECK(status==LB_PROCESS_DONE); Eterm result=lb_process_result(p);
            if(i==0) CHECK(result==make_small(42));
            if(i==1 || i==3) wide(result,i==3);
            if(i==2) {
                const Uint digits[]={UINT64_MAX,UINT64_MAX,UINT64_MAX-2,1,0,4};
                CHECK(lb_is_big(result) && !big_is_negative(result) && lb_big_size(result)==6);
                CHECK(!memcmp(boxed_val(result)+1,digits,sizeof(digits)));
            }
            if(i==4) {
                double value; CHECK(is_boxed(result) && (*boxed_val(result)&_TAG_HEADER_MASK)==FLOAT_SUBTAG);
                memcpy(&value,boxed_val(result)+1,sizeof(value)); CHECK(value==6.0);
            }
            if(i>=8) {
                Eterm atom; const char *name=i==8?"yes":"no";
                CHECK(lb_atoms_find(world->space->atoms,name,strlen(name),&atom)==LB_ATOM_OK && result==atom);
            }
        }
        lb_process_destroy(p);
    }
    LbProcess *p=input(world,module,"kept",0);
    for(size_t step=0;step<100 && *p->i!=op_i_times_jssd;++step)
        CHECK(lb_process_run(p,1,100)==LB_PROCESS_YIELDED);
    CHECK(*p->i==op_i_times_jssd && ((p->i[1]>>BEAM_WIDE_SHIFT)&1));
    while(p->htop<p->stop) *p->htop++=make_small(0);
    size_t collections=p->collections;
    CHECK(run(p)==LB_PROCESS_DONE && p->collections>collections);
    wide(lb_process_result(p),0); lb_process_destroy(p);
    allocation_boundaries(world,module); work_boundaries(world,module);
    size_and_quadratic_boundaries(world,module);
    lb_engine_leave(engine);
    LbReclamation *reclamation;
    CHECK(lb_world_stop(world,&reclamation)==LB_WORLD_OK && lb_reclamation_poll(reclamation)==LB_WORLD_OK);
    CHECK(lb_reclamation_consume(reclamation)==LB_WORLD_OK); lb_reclamation_release(reclamation); lb_world_release(world);
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine);
    puts("CORE_TIMES_OK integer=true float=true native_guard=true badarith=true gc=true allocation_prefixes=true work_prefixes=true quadratic_precharge=true native_size_ceiling=true y_destination_gc=true balanced=true");
    return 0;
}
