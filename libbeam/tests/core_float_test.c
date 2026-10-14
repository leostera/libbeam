/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#include "process_internal.h"
#include "world_internal.h"
#include "work_budget_internal.h"
#include "alloc_internal.h"
#include "big.h"
#include <fenv.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"float:%d: %s\n",__LINE__,#x); abort(); } } while(0)
typedef struct { Eterm digits[18];size_t count;int negative,badarg;uint64_t bits; } Vector;
static Vector vector(const char *hex,const char *answer)
{
    Vector v={0};v.negative=*hex=='-';if(v.negative)++hex;
    size_t length=strlen(hex);CHECK(length && length<=sizeof(v.digits)*2);
    v.count=(length+15)/16;
    for(size_t i=0;i<length;++i) {
        char c=hex[length-1-i];unsigned digit=c>='0' && c<='9'?(unsigned)(c-'0'):c>='a' && c<='f'?(unsigned)(c-'a'+10):16;
        CHECK(digit<16);v.digits[i/16]|=(Eterm)digit<<((i%16)*4);
    }
    CHECK(v.count==1 || v.digits[v.count-1]);
    v.badarg=!strcmp(answer,"badarg");
    if(!v.badarg) { CHECK(strlen(answer)==16);char *end;v.bits=strtoull(answer,&end,16);CHECK(!*end); }
    return v;
}
static int small(const Vector *v)
{ return v->count==1 && v->digits[0]<=(v->negative?(Uint)MAX_SMALL+1:(Uint)MAX_SMALL); }
static Eterm small_value(const Vector *v)
{ return make_small(v->negative?-(Sint)v->digits[0]:(Sint)v->digits[0]); }
static Eterm atom(LbWorld *w,const char *s)
{ Eterm t;CHECK(lb_atoms_find(w->space->atoms,s,strlen(s),&t)==LB_ATOM_OK);return t; }
static LbProcess *create(LbWorld *w,LbCodeModule *m,const char *name,const Vector *v,int full)
{
    LbCodeEntry *entry;LbProcess *p;Eterm args[2]={NIL,NIL};int dynamic=!strcmp(name,"dynamic");
    CHECK(lb_code_entry_acquire(w->space,m->name,atom(w,name),dynamic?2:1,&entry)==LB_CODE_OK);
    CHECK(lb_process_create(entry,args,dynamic?2:1,1024,&p)==LB_PROCESS_READY);lb_code_entry_release(entry);
    Eterm value;
    if(small(v)) value=small_value(v);
    else { p->htop[0]=_make_header(v->count,v->negative?NEG_BIG_SUBTAG:POS_BIG_SUBTAG);memcpy(p->htop+1,v->digits,v->count*sizeof(Eterm));value=make_boxed(p->htop);p->htop+=v->count+1; }
    p->construction_root=p->reg[dynamic?1:0]=value;if(dynamic)p->reg[0]=am_float;
    p->heap_reserved=11;if(full)while(p->htop<p->stop-S_RESERVED-p->heap_reserved)*p->htop++=NIL;
    return p;
}
static void source(LbProcess *p,const Vector *v)
{
    Eterm t=p->construction_root;
    if(small(v)) CHECK(t==small_value(v));
    else { CHECK(lb_is_big(t) && lb_big_size(t)==v->count);CHECK((*boxed_val(t)&_TAG_HEADER_MASK)==(v->negative?NEG_BIG_SUBTAG:POS_BIG_SUBTAG));CHECK(!memcmp(boxed_val(t)+1,v->digits,v->count*sizeof(Eterm))); }
}
static void result(Eterm value,const Vector *v)
{
    CHECK(is_boxed(value) && *boxed_val(value)==_make_header(1,FLOAT_SUBTAG));uint64_t bits;memcpy(&bits,boxed_val(value)+1,sizeof(bits));CHECK(bits==v->bits);
}
static LbProcessStatus run(LbProcess *p)
{ LbProcessStatus s=LB_PROCESS_READY;for(unsigned i=0;i<1000 && (s==LB_PROCESS_READY || s==LB_PROCESS_YIELDED);++i)s=lb_process_run(p,1,100);return s; }
static void *deny(void *c,size_t n) { (void)c;(void)n;return NULL; }
static void semantics(LbWorld *w,LbCodeModule *m,const Vector *v)
{
    const int modes[]={FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO};fenv_t saved;CHECK(!fegetenv(&saved));
    for(unsigned mode=0;mode<4;++mode) {
        LbProcess *p=create(w,m,"identity",v,1);
        CHECK(!fesetround(modes[mode]) && !feclearexcept(FE_ALL_EXCEPT) && !feraiseexcept(FE_INVALID|FE_DIVBYZERO));int flags=fetestexcept(FE_ALL_EXCEPT);
        Eterm value=lb_bif_float_1(p,p->reg,NULL);
        CHECK(fegetround()==modes[mode] && fetestexcept(FE_ALL_EXCEPT)==flags && p->heap_reserved==11);source(p,v);
        if(v->badarg) CHECK(is_non_value(value) && p->freason==LB_EXC_BADARG && !p->collections);
        else {
            result(value,v);CHECK(p->collections);p->reg[0]=value;
            LbAllocDomain *d=w->space->domain;LbSystemAllocator allocator=d->system;size_t limit=d->limit;Eterm *top=p->htop;
            d->system.allocate=deny;d->limit=d->current;
            CHECK(lb_bif_float_1(p,p->reg,NULL)==value && p->last_native_cost==1 && p->htop==top);
            d->system=allocator;d->limit=limit;
        }
        lb_process_destroy(p);
    }
    CHECK(!fesetenv(&saved));
    const char *names[]={"body","kept","dynamic"};
    for(unsigned i=0;i<3;++i) {
        LbProcess *p=create(w,m,names[i],v,0);LbProcessStatus s=run(p);
        if(v->badarg) CHECK(s==LB_PROCESS_EXCEPTION && p->exception_value==am_badarg);
        else { CHECK(s==LB_PROCESS_DONE);result(lb_process_result(p),v);CHECK(lb_process_collect(p,0)==LB_PROCESS_DONE);result(lb_process_result(p),v); }
        source(p,v);lb_process_destroy(p);
    }
}
typedef struct { LbSystemAllocator system; size_t remaining; } AllocationPrefix;
static void *prefix_allocate(void *context,size_t bytes)
{
    AllocationPrefix *prefix=context; if(!prefix->remaining) return NULL;
    --prefix->remaining; return prefix->system.allocate(prefix->system.context,bytes);
}
static void prefix_release(void *context,void *memory)
{ AllocationPrefix *prefix=context; prefix->system.release(prefix->system.context,memory); }
static void failures(LbWorld *w,LbCodeModule *m,const Vector *v)
{
    LbAllocDomain *d=w->space->domain;size_t baseline=d->current,blocks=d->block_count;
    for(unsigned mode=0;mode<2;++mode) {
        LbProcess *p=create(w,m,"guarded",v,1);LbSystemAllocator allocator=d->system;size_t limit=d->limit;
        if(mode)d->limit=d->current;else d->system.allocate=deny;
        CHECK(run(p)==(mode?LB_PROCESS_LIMIT:LB_PROCESS_NO_MEMORY));CHECK(p->last_native_cost>=129);source(p,v);
        d->system=allocator;d->limit=limit;lb_alloc_domain_begin_operation(d);lb_process_destroy(p);CHECK(d->current==baseline && d->block_count==blocks);
    }
    int succeeded=0;
    for(size_t allowance=0;allowance<64 && !succeeded;++allowance) {
        LbProcess *p=create(w,m,"identity",v,1); AllocationPrefix prefix={d->system,allowance};
        d->system=(LbSystemAllocator){&prefix,prefix_allocate,prefix_release};
        Eterm value=lb_bif_float_1(p,p->reg,NULL);
        if(is_non_value(value)) CHECK(p->status==LB_PROCESS_NO_MEMORY);
        else { result(value,v); succeeded=1; }
        source(p,v); CHECK(p->heap_reserved==11); d->system=prefix.system;
        lb_alloc_domain_begin_operation(d);lb_process_destroy(p);CHECK(d->current==baseline && d->block_count==blocks);
    }
    CHECK(succeeded);
    uint64_t required=0;
    for(uint64_t pass=0;pass<=required+1;++pass) {
        LbProcess *p=create(w,m,"guarded",v,1);LbWorkBudget *b=lb_work_budget_create(w);CHECK(b);
        uint64_t credit=pass?pass-1:1000000;b->remaining=b->limit=credit;lb_work_domain_borrow(d,b);LbProcessStatus s=run(p);
        if(!pass) { CHECK(s==LB_PROCESS_DONE);required=credit-b->remaining;CHECK(required>129 && required<100000); }
        else if(credit<required)CHECK(s==LB_PROCESS_WORK_LIMIT);
        else CHECK(s==LB_PROCESS_DONE && !b->remaining);
        source(p,v);lb_work_domain_borrow(d,NULL);lb_work_domain_clear_failure(d);lb_alloc_domain_begin_operation(d);lb_process_destroy(p);lb_work_budget_release(b);
        CHECK(d->current==baseline && d->block_count==blocks);
    }
}
int main(int argc,char **argv)
{
    CHECK(argc==3);FILE *f=fopen(argv[1],"rb");CHECK(f && !fseek(f,0,SEEK_END));long n=ftell(f);CHECK(n>0 && !fseek(f,0,SEEK_SET));
    unsigned char *bytes=malloc((size_t)n);CHECK(bytes && fread(bytes,1,(size_t)n,f)==(size_t)n && !fclose(f));
    LbEngine *e;LbWorld *w;CHECK(lb_engine_create(NULL,&e)==LB_ENGINE_OK && lb_world_create(e,&w)==LB_WORLD_OK);lb_engine_enter(e);
    LbCodeModule *m;LbBeamError error;LbCodeStatus status=lb_code_load(w->space,bytes,(size_t)n,&m,&error);
    if(status!=LB_CODE_OK)fprintf(stderr,"load=%d stage=%s offset=%zu\n",status,error.stage,error.offset);
    CHECK(status==LB_CODE_OK);free(bytes);
    FILE *oracle=fopen(argv[2],"r");CHECK(oracle);char hex[320],answer[17];unsigned rows=0;
    int parsed;
    while((parsed=fscanf(oracle,"%319s %16s",hex,answer))==2) { Vector v=vector(hex,answer);semantics(w,m,&v);++rows; }
    CHECK(parsed==EOF && feof(oracle) && !fclose(oracle) && rows>1000);
    Vector zero=vector("0","0000000000000000"),big=vector("10000000000000000","43f0000000000000");failures(w,m,&zero);failures(w,m,&big);
    for(unsigned guard=0;guard<2;++guard) {
        LbProcess *p=create(w,m,guard?"guarded":"body",&zero,0);p->reg[0]=NIL;
        CHECK(run(p)==(guard?LB_PROCESS_DONE:LB_PROCESS_EXCEPTION));CHECK(guard?lb_process_result(p)==am_no:p->exception_value==am_badarg);lb_process_destroy(p);
    }
    lb_engine_leave(e);LbReclamation *r;CHECK(lb_world_stop(w,&r)==LB_WORLD_OK && lb_reclamation_poll(r)==LB_WORLD_OK && lb_reclamation_consume(r)==LB_WORLD_OK);lb_reclamation_release(r);
    LbWorldResourceUsage usage;CHECK(lb_world_resource_usage(w,&usage)==LB_WORLD_OK && usage.closed && !usage.current);lb_world_release(w);
    CHECK(lb_engine_shutdown(e)==LB_ENGINE_OK);lb_engine_release(e);
    printf("CORE_FLOAT_OK vectors=%u native_rounding=true overflow_badarg=true identity=true fenv=true gc=true credit=true guards_resources=true reclaimed=true\n",rows);
}
