/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#include "process_internal.h"
#include "world_internal.h"
#include "work_budget_internal.h"
#include "alloc_internal.h"
#include "big.h"
#include "code_internal.h"
#include "opcodes.h"
#include <fenv.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while(0)
static LbProcess *create_arity(LbWorld *world,LbCodeModule *module,const char *name,size_t arity)
{
    Eterm function,args[2]={make_small(0),make_small(0)}; LbCodeEntry *entry; LbProcess *p; CHECK(arity && arity<=2);
    CHECK(lb_atoms_find(world->space->atoms,name,strlen(name),&function)==LB_ATOM_OK);
    CHECK(lb_code_entry_acquire(world->space,module->name,function,arity,&entry)==LB_CODE_OK);
    CHECK(lb_process_create(entry,args,arity,256,&p)==LB_PROCESS_READY); lb_code_entry_release(entry); return p;
}
static LbProcess *create(LbWorld *world,LbCodeModule *module,const char *name)
{ return create_arity(world,module,name,1); }
static LbProcessStatus run(LbProcess *p)
{
    LbProcessStatus status=LB_PROCESS_READY;
    for(size_t i=0;i<1000 && (status==LB_PROCESS_READY || status==LB_PROCESS_YIELDED);++i)
        status=lb_process_run(p,1,100);
    return status;
}
static void wide(Eterm term)
{ CHECK(lb_is_big(term) && *boxed_val(term)==_make_header(2,NEG_BIG_SUBTAG) && boxed_val(term)[1]==1 && boxed_val(term)[2]==UINT64_MAX-1); }
static LbProcess *input(LbWorld *world,LbCodeModule *module,const char *function,int collect)
{
    LbProcess *p=create(world,module,function);
    p->reg[0]=p->reg[MAX_REG-1]=make_boxed(p->htop);
    p->htop[0]=_make_header(2,POS_BIG_SUBTAG); p->htop[1]=1; p->htop[2]=UINT64_MAX-1; p->htop+=3;
    if(collect) while(p->htop<p->stop) *p->htop++=make_small(0);
    return p;
}
static void vectors(LbWorld *world,LbCodeModule *module)
{
    const Sint integers[]={MIN_SMALL,MAX_SMALL,-42,0,42};
    for(size_t i=0;i<sizeof(integers)/sizeof(*integers);++i) {
        LbProcess *p=create(world,module,"negate"); p->reg[0]=make_small(integers[i]);
        CHECK(run(p)==LB_PROCESS_DONE); Eterm result=lb_process_result(p);
        if(integers[i]==MIN_SMALL) CHECK(lb_is_big(result) && *boxed_val(result)==_make_header(1,POS_BIG_SUBTAG) && boxed_val(result)[1]==(Uint)MAX_SMALL+1);
        else CHECK(result==make_small(-integers[i]));
        lb_process_destroy(p);
    }
    const Uint bits[]={0,UINT64_C(0x8000000000000000),UINT64_C(0x3ff8000000000000),UINT64_C(0xbff8000000000000),
                       1,UINT64_C(0x8000000000000001),UINT64_C(0x7fefffffffffffff),UINT64_C(0xffefffffffffffff)};
    for(size_t i=0;i<sizeof(bits)/sizeof(*bits);++i) {
        LbProcess *p=create(world,module,"negate"); p->reg[0]=make_boxed(p->htop);
        p->htop[0]=_make_header(1,FLOAT_SUBTAG); p->htop[1]=bits[i]; p->htop+=2;
        Eterm original=p->reg[0];
        int rounding=fegetround(); CHECK(rounding!=-1 && !fesetround(FE_UPWARD));
        CHECK(run(p)==LB_PROCESS_DONE && fegetround()==FE_UPWARD && !fesetround(rounding));
        Eterm result=lb_process_result(p);
        CHECK(is_boxed(result) && *boxed_val(result)==_make_header(1,FLOAT_SUBTAG));
        CHECK(result!=original && boxed_val(result)[1]==(bits[i]^UINT64_C(0x8000000000000000)));
        CHECK(boxed_val(original)[1]==bits[i]); lb_process_destroy(p);
    }
    LbProcess *p=create(world,module,"negate");
    p->reg[0]=make_boxed(p->htop); *p->htop++=_make_header(1,POS_BIG_SUBTAG); *p->htop++=(Uint)MAX_SMALL+1;
    CHECK(run(p)==LB_PROCESS_DONE && lb_process_result(p)==make_small(MIN_SMALL)); lb_process_destroy(p);
    p=input(world,module,"negate",0); *boxed_val(p->reg[0])=_make_header(2,NEG_BIG_SUBTAG);
    CHECK(run(p)==LB_PROCESS_DONE && *boxed_val(lb_process_result(p))==_make_header(2,POS_BIG_SUBTAG));
    CHECK(*boxed_val(p->reg[MAX_REG-1])==_make_header(2,NEG_BIG_SUBTAG)); lb_process_destroy(p);
    p=create(world,module,"guarded"); p->reg[0]=make_small(-42); CHECK(run(p)==LB_PROCESS_DONE);
    Eterm yes; CHECK(lb_atoms_find(world->space->atoms,"yes",3,&yes)==LB_ATOM_OK && lb_process_result(p)==yes); lb_process_destroy(p);
    for(unsigned guard=0;guard<2;++guard) for(unsigned bad=0;bad<2;++bad) {
        p=create(world,module,guard?"guarded":"negate"); p->reg[0]=bad?NIL:am_true;
        CHECK(run(p)==(guard?LB_PROCESS_DONE:LB_PROCESS_EXCEPTION));
        if(!guard) CHECK(p->exception_value==am_badarith);
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
            LbProcess *p=input(world,module,guard?"guarded":"negate",1);
            Eterm *heap=p->htop,original=p->reg[0]; size_t live=domain->current,limit=domain->limit;
            Prefix prefix={domain->system,attempt?attempt-1:0};
            if(!attempt) domain->limit=live; else domain->system=(LbSystemAllocator){&prefix,allocate,release};
            LbProcessStatus status=run(p); domain->limit=limit; domain->system=prefix.backing;
            if(status==LB_PROCESS_DONE) {
                succeeded=1; CHECK(p->collections>0); if(!guard) wide(lb_process_result(p));
                CHECK(*boxed_val(p->reg[MAX_REG-1])==_make_header(2,POS_BIG_SUBTAG));
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
            LbProcess *p=input(world,module,"negate",collect);
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
static void positive(LbWorld *world,LbCodeModule *body,LbCodeModule *guard)
{
    const Sint smalls[]={MIN_SMALL,MAX_SMALL,-42,0,42};
    const Uint floats[]={0,UINT64_C(0x8000000000000000),UINT64_C(0x3ff8000000000000),UINT64_C(0xbff8000000000000),1,UINT64_C(0x8000000000000001),UINT64_C(0x7fefffffffffffff),UINT64_C(0xffefffffffffffff)};
    LbAllocDomain *domain=world->space->domain; size_t baseline=domain->current;
    /* 5 smalls, 2 signed bignums, 8 float bit patterns, 4 invalid kinds. */
    for(unsigned form=0;form<4;++form) for(unsigned kind=0;kind<19;++kind) {
        int guarded=form==1 || form==3; uint64_t cost=0;
        for(unsigned mode=0;mode<6;++mode) {
            if(mode>=4 && (form==2 || (form==0 && kind>=15))) continue;
            LbProcess *p=create_arity(world,form==1?guard:body,form==2?"positive_dynamic":form==3?"positive_guarded":"positive",form==2?2:1);
            Eterm value; Eterm *hp=p->htop;
            if(kind<5) value=make_small(smalls[kind]);
            else if(kind<7) { hp[0]=_make_header(2,kind==5?POS_BIG_SUBTAG:NEG_BIG_SUBTAG); hp[1]=1; hp[2]=UINT64_MAX-1; value=make_boxed(hp); p->htop+=3; }
            else if(kind<15) { hp[0]=_make_header(1,FLOAT_SUBTAG); hp[1]=floats[kind-7]; value=make_boxed(hp); p->htop+=2; }
            else if(kind==15) value=am_true;
            else if(kind==16) value=NIL;
            else if(kind==17) { value=TUPLE1(hp,make_small(1)); p->htop+=2; }
            else { value=CONS(hp,make_small(1),NIL); p->htop+=2; }
            p->reg[form==2?1:0]=value; if(form==2) p->reg[0]=am_Plus;
            LbProcessStatus expected=kind<15 || guarded?LB_PROCESS_DONE:LB_PROCESS_EXCEPTION;
            LbWorkBudget *budget=NULL; size_t limit=domain->limit,used=domain->current; LbSystemAllocator system=domain->system;
            Prefix prefix={system,0}; Eterm *top=NULL;
            if(mode<4) {
                budget=lb_work_budget_create(world); CHECK(budget);
                uint64_t credit=mode==0?1000000:mode==1?cost:mode==2?cost-1:0;
                budget->limit=budget->remaining=credit; lb_work_domain_borrow(domain,budget);
            } else {
                BeamInstr op=guarded?op_i_bif1_sfbd:op_i_bif1_body_sbd;
                for(unsigned step=0;step<100 && *p->i!=op;++step) CHECK(lb_process_run(p,1,100)==LB_PROCESS_YIELDED);
                CHECK(*p->i==op); while(p->htop<p->stop-S_RESERVED) *p->htop++=NIL;
                top=p->htop; used=domain->current;
                if(mode==4) domain->limit=used; else domain->system=(LbSystemAllocator){&prefix,allocate,release};
            }
            int rounding=fegetround(); CHECK(rounding!=-1 && !fesetround(FE_DOWNWARD));
            LbProcessStatus status=run(p); CHECK(fegetround()==FE_DOWNWARD && !fesetround(rounding));
            domain->limit=limit; domain->system=system;
            if(mode==0) { CHECK(status==expected); cost=1000000-budget->remaining; CHECK(cost>1 && cost<10000); }
            else if(mode==1) CHECK(status==expected && !budget->remaining);
            else if(mode<4) CHECK(status==LB_PROCESS_WORK_LIMIT);
            else CHECK(status==LB_PROCESS_DONE && p->htop==top && domain->current==used);
            if(status==expected) {
                if(status==LB_PROCESS_EXCEPTION) CHECK(p->exception_value==am_badarith);
                else CHECK(lb_process_result(p)==(kind<15?value:am_no));
            }
            CHECK(!p->collections);
            if(kind>=7 && kind<15) CHECK(boxed_val(value)[1]==floats[kind-7]);
            lb_work_domain_borrow(domain,NULL); lb_work_domain_clear_failure(domain); lb_alloc_domain_begin_operation(domain);
            lb_process_destroy(p); if(budget) lb_work_budget_release(budget); CHECK(domain->current==baseline);
        }
    }
    for(unsigned kind=0;kind<9;++kind) {
        LbProcess *p=create(world,body,"positive_kept"); Eterm *heap=p->heap;
        p->reg[0]=make_boxed(p->htop); p->construction_root=p->reg[0];
        if(!kind) { *p->htop++=_make_header(2,NEG_BIG_SUBTAG); *p->htop++=1; *p->htop++=UINT64_MAX-1; }
        else { *p->htop++=_make_header(1,FLOAT_SUBTAG); *p->htop++=floats[kind-1]; }
        CHECK(run(p)==LB_PROCESS_DONE && p->collections && p->heap!=heap && lb_process_result(p)==p->construction_root);
        if(!kind) wide(lb_process_result(p)); else CHECK(boxed_val(lb_process_result(p))[1]==floats[kind-1]);
        lb_process_destroy(p); CHECK(domain->current==baseline);
    }
    LbProcess *p=create(world,guard,"positive"); p->reg[0]=am_true;
    LbWorkBudget *budget=lb_work_budget_create(world); CHECK(budget); budget->limit=budget->remaining=0;
    lb_work_domain_borrow(domain,budget);
    CHECK(is_non_value(lb_bif_plus_1(p,p->reg,NULL)) && p->status==LB_PROCESS_WORK_LIMIT && p->freason==LB_EXC_NULL && !p->last_native_cost);
    lb_work_domain_borrow(domain,NULL); lb_work_domain_clear_failure(domain); lb_alloc_domain_begin_operation(domain);
    lb_process_destroy(p); lb_work_budget_release(budget); CHECK(domain->current==baseline);
    puts("CORE_POSITIVE_OK identity=true signed_zero=true float_bits=true badarith=true body=true dynamic=true y_guard=true native_allocation_free=true exact_credit=true one_below=true zero=true fenv_preserved=true balanced=true");
}
int main(int argc,char **argv)
{
    FILE *file; long length; CHECK(argc==3 && (file=fopen(argv[1],"rb")) && !fseek(file,0,SEEK_END));
    length=ftell(file); CHECK(length>0 && !fseek(file,0,SEEK_SET)); unsigned char *bytes=malloc((size_t)length);
    CHECK(bytes && fread(bytes,1,(size_t)length,file)==(size_t)length && !fclose(file));
    LbEngine *engine; LbWorld *world; LbCodeModule *module; LbBeamError error;
    CHECK(lb_engine_create(NULL,&engine)==LB_ENGINE_OK && lb_world_create(engine,&world)==LB_WORLD_OK); lb_engine_enter(engine);
    LbCodeStatus loaded=lb_code_load(world->space,bytes,(size_t)length,&module,&error);
    if(loaded!=LB_CODE_OK) fprintf(stderr,"load=%d stage=%s offset=%zu\n",loaded,error.stage,error.offset);
    CHECK(loaded==LB_CODE_OK); free(bytes); vectors(world,module);
    LbProcess *p=input(world,module,"kept",0);
    for(size_t step=0;step<100 && *p->i!=op_i_unary_minus_yjd;++step) CHECK(lb_process_run(p,1,100)==LB_PROCESS_YIELDED);
    CHECK(*p->i==op_i_unary_minus_yjd && ((p->i[2]>>BEAM_WIDE_SHIFT)&1));
    while(p->htop<p->stop) *p->htop++=make_small(0);
    size_t collections=p->collections; CHECK(run(p)==LB_PROCESS_DONE && p->collections>collections);
    wide(lb_process_result(p)); CHECK(*boxed_val(p->reg[MAX_REG-1])==_make_header(2,POS_BIG_SUBTAG)); lb_process_destroy(p);
    allocation_boundaries(world,module); work_boundaries(world,module);
    p=create(world,module,"negate"); CHECK(lb_heap_reserve(p,LB_BIG_ARITY_MAX+1,MAX_REG));
    p->reg[0]=make_boxed(p->htop); *p->htop++=_make_header(LB_BIG_ARITY_MAX,POS_BIG_SUBTAG);
    for(size_t i=0;i<LB_BIG_ARITY_MAX;++i) *p->htop++=UINT64_MAX;
    size_t reserved=p->heap_reserved=(size_t)(p->stop-p->htop); collections=p->collections;
    Eterm result=lb_unary_minus(p,p->reg[0]);
    CHECK(lb_is_big(result) && *boxed_val(result)==_make_header(LB_BIG_ARITY_MAX,NEG_BIG_SUBTAG));
    CHECK(boxed_val(result)[1]==UINT64_MAX && boxed_val(result)[LB_BIG_ARITY_MAX]==UINT64_MAX);
    CHECK(p->collections>collections && p->heap_reserved==reserved && result!=p->reg[0]);
    CHECK(*boxed_val(p->reg[0])==_make_header(LB_BIG_ARITY_MAX,POS_BIG_SUBTAG));
    size_t limit=world->domain.limit,used=world->domain.current; Prefix prefix={world->domain.system,0};
    world->domain.limit=used; world->domain.system=(LbSystemAllocator){&prefix,allocate,release};
    CHECK(lb_bif_plus_1(p,p->reg,NULL)==p->reg[0] && p->last_native_cost==1 && world->domain.current==used);
    world->domain.limit=limit; world->domain.system=prefix.backing; lb_process_destroy(p);
    CHECK((file=fopen(argv[2],"rb")) && !fseek(file,0,SEEK_END)); length=ftell(file);
    CHECK(length>0 && !fseek(file,0,SEEK_SET)); bytes=malloc((size_t)length);
    CHECK(bytes && fread(bytes,1,(size_t)length,file)==(size_t)length && !fclose(file));
    LbCodeModule *guard; loaded=lb_code_load(world->space,bytes,(size_t)length,&guard,&error);
    if(loaded!=LB_CODE_OK) fprintf(stderr,"guard load=%d stage=%s\n",loaded,error.stage);
    CHECK(loaded==LB_CODE_OK); free(bytes); positive(world,module,guard);
    lb_engine_leave(engine); LbReclamation *reclamation;
    CHECK(lb_world_stop(world,&reclamation)==LB_WORLD_OK && lb_reclamation_poll(reclamation)==LB_WORLD_OK);
    CHECK(lb_reclamation_consume(reclamation)==LB_WORLD_OK); lb_reclamation_release(reclamation); lb_world_release(world);
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine);
    puts("CORE_NEGATE_OK min_small=true normalize_small=true signed_zero=true float_bits=true badarith=true guards=true y_destination_gc=true source_unchanged=true allocation_prefixes=true work_prefixes=true balanced=true");
    return 0;
}
