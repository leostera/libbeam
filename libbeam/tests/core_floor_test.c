/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#include "process_internal.h"
#include "world_internal.h"
#include "work_budget_internal.h"
#include "alloc_internal.h"
#include "big.h"
#include <fenv.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while(0)
/* Independent integer/bit oracle, not the production floating scaling loop. */
static void expected(Eterm result,uint64_t bits)
{
    uint64_t limbs[16]={0},fraction=bits&UINT64_C(0xfffffffffffff); unsigned exponent=(unsigned)((bits>>52)&2047); int negative=(int)(bits>>63);
    int e=(int)exponent-1023; uint64_t mant=fraction|(exponent?UINT64_C(1)<<52:0);
    if(e<0) limbs[0]=negative && mant?1:0;
    else if(e<52) { unsigned shift=52-(unsigned)e; limbs[0]=mant>>shift; if(negative && (mant&((UINT64_C(1)<<shift)-1))) ++limbs[0]; }
    else {
        unsigned shift=(unsigned)e-52,index=shift/64,offset=shift%64; limbs[index]=mant<<offset;
        if(offset && (mant>>(64-offset))) { CHECK(index+1<16); limbs[index+1]=mant>>(64-offset); }
    }
    size_t count=16; while(count>1 && !limbs[count-1]) --count;
    int force_big=bits==UINT64_C(0xc3a0000000000000); /* Native strict MIN_SMALL threshold. */
    if(!force_big && count==1 && limbs[0]<=(negative?(Uint)MAX_SMALL+1:(Uint)MAX_SMALL)) {
        CHECK(result==make_small(negative?-(Sint)limbs[0]:(Sint)limbs[0])); return;
    }
    CHECK(lb_is_big(result) && lb_big_size(result)==count);
    CHECK((*boxed_val(result)&_TAG_HEADER_MASK)==(negative?NEG_BIG_SUBTAG:POS_BIG_SUBTAG));
    CHECK(!memcmp(boxed_val(result)+1,limbs,count*sizeof(*limbs)));
}
static LbProcess *create(LbWorld *w,LbCodeModule *m,const char *name,uint64_t bits,int gc)
{
    Eterm function,value=NIL; LbCodeEntry *entry; LbProcess *p;
    CHECK(lb_atoms_find(w->space->atoms,name,strlen(name),&function)==LB_ATOM_OK);
    CHECK(lb_code_entry_acquire(w->space,m->name,function,1,&entry)==LB_CODE_OK);
    CHECK(lb_process_create(entry,&value,1,1024,&p)==LB_PROCESS_READY); lb_code_entry_release(entry);
    p->htop[0]=_make_header(1,FLOAT_SUBTAG); memcpy(p->htop+1,&bits,sizeof(bits)); p->reg[0]=make_boxed(p->htop); p->htop+=2;
    p->construction_root=p->reg[0]; p->heap_reserved=13;
    if(gc) while(p->htop<p->stop-S_RESERVED-p->heap_reserved) *p->htop++=NIL;
    return p;
}
static void source(LbProcess *p,uint64_t bits)
{ uint64_t actual; memcpy(&actual,boxed_val(p->construction_root)+1,sizeof(actual)); CHECK(actual==bits); }
static LbProcessStatus run(LbProcess *p)
{ LbProcessStatus s=LB_PROCESS_READY; for(unsigned i=0;i<1000 && (s==LB_PROCESS_READY || s==LB_PROCESS_YIELDED);++i) s=lb_process_run(p,1,100); return s; }
static const uint64_t vectors[]={0,UINT64_C(0x8000000000000000),1,UINT64_C(0x8000000000000001),
    UINT64_C(0x3ff4000000000000),UINT64_C(0xbff4000000000000),UINT64_C(0x3fefffffffffffff),UINT64_C(0xbfefffffffffffff),
    UINT64_C(0x439fffffffffffff),UINT64_C(0x43a0000000000000),UINT64_C(0xc39fffffffffffff),UINT64_C(0xc3a0000000000000),
    UINT64_C(0xc3a0000000000001),UINT64_C(0x43f0000000000000),UINT64_C(0x4630000000000000),
    UINT64_C(0x7fefffffffffffff),UINT64_C(0xffefffffffffffff)};
static void *deny(void *ctx,size_t bytes);
static void semantics(LbWorld *w,LbCodeModule *m)
{
    const int modes[]={FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO}; fenv_t saved; CHECK(!fegetenv(&saved));
    for(size_t i=0;i<sizeof(vectors)/sizeof(*vectors);++i) for(unsigned mode=0;mode<4;++mode) {
        LbProcess *p=create(w,m,"identity",vectors[i],1);
        CHECK(!fesetround(modes[mode]) && !feclearexcept(FE_ALL_EXCEPT) && !feraiseexcept(FE_INVALID|FE_DIVBYZERO));
        int flags=fetestexcept(FE_ALL_EXCEPT); Eterm result=lb_bif_floor_1(p,p->reg,NULL);
        CHECK(fegetround()==modes[mode] && fetestexcept(FE_ALL_EXCEPT)==flags && p->heap_reserved==13);
        expected(result,vectors[i]); source(p,vectors[i]);
        if(lb_is_big(result)) CHECK(p->collections);
        p->reg[0]=result;
        LbAllocDomain *d=w->space->domain; LbSystemAllocator allocator=d->system; size_t limit=d->limit;
        d->system.allocate=deny; d->limit=d->current;
        Eterm *top=p->htop; size_t collections=p->collections;
        CHECK(lb_bif_floor_1(p,p->reg,NULL)==result && p->last_native_cost==1 && p->htop==top && p->collections==collections);
        d->system=allocator; d->limit=limit; lb_process_destroy(p);
    }
    CHECK(!fesetenv(&saved));
    const uint64_t mantissas[]={0,1,UINT64_C(0xfffffffffffff)};
    for(unsigned exponent=0;exponent<2047;exponent+=17) for(unsigned sign=0;sign<2;++sign) for(unsigned mant=0;mant<3;++mant) {
        uint64_t bits=((uint64_t)sign<<63)|((uint64_t)exponent<<52)|mantissas[mant];
        LbProcess *p=create(w,m,"identity",bits,1); Eterm result=lb_bif_floor_1(p,p->reg,NULL);
        expected(result,bits); source(p,bits); CHECK(p->heap_reserved==13); lb_process_destroy(p);
    }
    const char *names[]={"body","kept"};
    for(unsigned name=0;name<2;++name) for(size_t i=0;i<sizeof(vectors)/sizeof(*vectors);++i) {
        LbProcess *p=create(w,m,names[name],vectors[i],0); CHECK(run(p)==LB_PROCESS_DONE); expected(lb_process_result(p),vectors[i]);
        CHECK(lb_process_collect(p,0)==LB_PROCESS_DONE); expected(lb_process_result(p),vectors[i]); source(p,vectors[i]); lb_process_destroy(p);
    }
    LbProcess *p=create(w,m,"guarded",UINT64_C(0xbff4000000000000),0); CHECK(run(p)==LB_PROCESS_DONE && lb_process_result(p)==am_yes); lb_process_destroy(p);
    p=create(w,m,"guarded",0,0); p->reg[0]=am_ok; CHECK(run(p)==LB_PROCESS_DONE && lb_process_result(p)==am_no); lb_process_destroy(p);
    p=create(w,m,"body",0,0); p->reg[0]=NIL; CHECK(run(p)==LB_PROCESS_EXCEPTION && p->exception_value==am_badarg); lb_process_destroy(p);
}
static void *deny(void *ctx,size_t bytes) { (void)ctx; (void)bytes; return NULL; }
typedef struct { LbSystemAllocator system; size_t remaining; } AllocationPrefix;
static void *prefix_allocate(void *context,size_t bytes)
{
    AllocationPrefix *prefix=context;
    if(!prefix->remaining) return NULL;
    --prefix->remaining; return prefix->system.allocate(prefix->system.context,bytes);
}
static void prefix_release(void *context,void *memory)
{ AllocationPrefix *prefix=context; prefix->system.release(prefix->system.context,memory); }
static void guard_failures(LbWorld *w,LbCodeModule *m)
{
    LbAllocDomain *d=w->space->domain;
    for(unsigned mode=0;mode<3;++mode) {
        LbProcess *p=create(w,m,"guarded",UINT64_C(0x7fefffffffffffff),1);
        LbSystemAllocator allocator=d->system; size_t limit=d->limit;
        LbWorkBudget *budget=NULL;
        if(mode==0) d->system.allocate=deny;
        if(mode==1) d->limit=d->current;
        if(mode==2) {
            budget=lb_work_budget_create(w); CHECK(budget); budget->limit=budget->remaining=65;
            lb_work_domain_borrow(d,budget);
        }
        CHECK(run(p)==(mode==0?LB_PROCESS_NO_MEMORY:mode==1?LB_PROCESS_LIMIT:LB_PROCESS_WORK_LIMIT));
        if(mode<2) CHECK(p->last_native_cost>=65); /* Allocation denial actually reached the BIF. */
        source(p,UINT64_C(0x7fefffffffffffff)); d->system=allocator; d->limit=limit;
        lb_work_domain_borrow(d,NULL); lb_work_domain_clear_failure(d); lb_alloc_domain_begin_operation(d);
        lb_process_destroy(p); if(budget) lb_work_budget_release(budget);
    }
    uint64_t required=0;
    for(uint64_t pass=0;pass<=required+1;++pass) {
        LbProcess *p=create(w,m,"guarded",UINT64_C(0x7fefffffffffffff),1);
        LbWorkBudget *budget=lb_work_budget_create(w); CHECK(budget);
        uint64_t credit=pass?pass-1:1000000; budget->limit=budget->remaining=credit; lb_work_domain_borrow(d,budget);
        LbProcessStatus status=run(p);
        if(!pass) { CHECK(status==LB_PROCESS_DONE); required=credit-budget->remaining; CHECK(required>65 && required<100000); }
        else if(credit<required) CHECK(status==LB_PROCESS_WORK_LIMIT);
        else CHECK(status==LB_PROCESS_DONE && lb_process_result(p)==am_no && !budget->remaining);
        source(p,UINT64_C(0x7fefffffffffffff));
        lb_work_domain_borrow(d,NULL); lb_work_domain_clear_failure(d); lb_alloc_domain_begin_operation(d);
        lb_process_destroy(p); lb_work_budget_release(budget);
    }
}
static void prefixes(LbWorld *w,LbCodeModule *m,uint64_t bits,int boxed)
{
    LbAllocDomain *d=w->space->domain; size_t baseline=d->current,blocks=d->block_count;
    for(unsigned mode=0;mode<3;++mode) {
        LbProcess *p=create(w,m,"identity",bits,1); size_t limit=d->limit; LbSystemAllocator system=d->system;
        if(mode==0) d->limit=d->current; if(mode==1) d->system.allocate=deny;
        Eterm result=lb_bif_floor_1(p,p->reg,NULL);
        if(boxed && mode<2) CHECK(is_non_value(result) && p->status==(mode?LB_PROCESS_NO_MEMORY:LB_PROCESS_LIMIT));
        else expected(result,bits);
        CHECK(p->heap_reserved==13); source(p,bits); d->system=system; d->limit=limit;
        lb_alloc_domain_begin_operation(d); lb_process_destroy(p); CHECK(d->current==baseline && d->block_count==blocks);
    }
    int succeeded=0;
    for(size_t allowance=0;allowance<64 && !succeeded;++allowance) {
        LbProcess *p=create(w,m,"identity",bits,1);
        AllocationPrefix prefix={d->system,allowance};
        d->system=(LbSystemAllocator){&prefix,prefix_allocate,prefix_release};
        Eterm result=lb_bif_floor_1(p,p->reg,NULL);
        if(is_non_value(result)) CHECK(p->status==LB_PROCESS_NO_MEMORY);
        else { expected(result,bits); succeeded=1; }
        source(p,bits); CHECK(p->heap_reserved==13); d->system=prefix.system;
        lb_alloc_domain_begin_operation(d); lb_process_destroy(p); CHECK(d->current==baseline && d->block_count==blocks);
    }
    CHECK(succeeded);
    for(int gc=0;gc<2;++gc) {
        uint64_t required=0;
        for(uint64_t pass=0;pass<=required+1;++pass) {
            LbProcess *p=create(w,m,"identity",bits,gc); LbWorkBudget *budget=lb_work_budget_create(w); CHECK(budget);
            uint64_t credit=pass?pass-1:1000000; budget->limit=budget->remaining=credit; lb_work_domain_borrow(d,budget);
            Eterm result=lb_bif_floor_1(p,p->reg,NULL);
            if(!pass) { expected(result,bits); required=credit-budget->remaining; CHECK(required>1 && required<100000); }
            else if(credit<required) CHECK(is_non_value(result) && p->status==LB_PROCESS_WORK_LIMIT);
            else { expected(result,bits); CHECK(!budget->remaining); }
            source(p,bits); CHECK(p->heap_reserved==13);
            lb_work_domain_borrow(d,NULL); lb_work_domain_clear_failure(d); lb_alloc_domain_begin_operation(d);
            lb_process_destroy(p); lb_work_budget_release(budget); CHECK(d->current==baseline && d->block_count==blocks);
        }
    }
}
int main(int argc,char **argv)
{
    CHECK(argc==2); FILE *f=fopen(argv[1],"rb"); CHECK(f && !fseek(f,0,SEEK_END)); long size=ftell(f); CHECK(size>0 && !fseek(f,0,SEEK_SET));
    unsigned char *bytes=malloc((size_t)size); CHECK(bytes && fread(bytes,1,(size_t)size,f)==(size_t)size && !fclose(f));
    LbEngine *engine; LbWorld *w; CHECK(lb_engine_create(NULL,&engine)==LB_ENGINE_OK && lb_world_create(engine,&w)==LB_WORLD_OK);
    lb_engine_enter(engine); LbCodeModule *m; LbBeamError error; CHECK(lb_code_load(w->space,bytes,(size_t)size,&m,&error)==LB_CODE_OK); free(bytes);
    size_t baseline=w->space->domain->current; semantics(w,m); guard_failures(w,m); prefixes(w,m,UINT64_C(0xbff4000000000000),0); prefixes(w,m,UINT64_C(0x7fefffffffffffff),1);
    CHECK(w->space->domain->current==baseline); lb_engine_leave(engine);
    LbReclamation *r; CHECK(lb_world_stop(w,&r)==LB_WORLD_OK && lb_reclamation_poll(r)==LB_WORLD_OK && lb_reclamation_consume(r)==LB_WORLD_OK); lb_reclamation_release(r); lb_world_release(w);
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine);
    puts("CORE_FLOOR_OK actual_bif=true body_guards_y=true ieee_bits=true native_min_boundary=true max_finite=true integer_identity=true fenv_preserved=true moving_gc=true allocation_work_prefixes=true balanced=true"); return 0;
}
