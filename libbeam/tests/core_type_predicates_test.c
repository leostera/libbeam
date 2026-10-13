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
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"types:%d: %s\n",__LINE__,#x); abort(); } } while(0)
typedef enum { SMALL, BIG, FLOAT, TRUE, FALSE, ATOM, NIL_VALUE, CONS_VALUE, EMPTY_TUPLE, TUPLE, MAP, BINARY, BITS, PID, REF, PORT_TOKEN, KINDS } Kind;
static const char *names[]={"is_bitstring","is_boolean","is_float","is_map","is_number","is_pid","is_port","is_reference","is_tuple","tuple_size"};
static const char *bodies[]={"bitstring_body","boolean_body","float_body","map_body","number_body","pid_body","port_body","reference_body","tuple_body","size_body"};
static const char *guards[]={"bitstring_guard","boolean_guard","float_guard","map_guard","number_guard","pid_guard","port_guard","reference_guard","tuple_guard","size_guard"};
static Eterm (*const primitives[])(LbProcess *,Eterm *,const BeamInstr *)={
#define LB_TYPE_PREDICATE(name, predicate) lb_bif_##name##_1,
#include "type_predicates.def"
#undef LB_TYPE_PREDICATE
    lb_bif_tuple_size_1
};
_Static_assert(sizeof(primitives)/sizeof(primitives[0])==10,"complete predicate batch");
static Eterm atom(LbWorld *w,const char *s)
{ Eterm a; CHECK(lb_atoms_find(w->space->atoms,s,strlen(s),&a)==LB_ATOM_OK); return a; }
static Eterm expected(unsigned index,Kind k,int guard)
{
    int yes=0;
    switch(index) {
    case 0:yes=k==BINARY || k==BITS;break;
    case 1:yes=k==TRUE || k==FALSE;break;
    case 2:yes=k==FLOAT;break;
    case 3:yes=k==MAP;break;
    case 4:yes=k==SMALL || k==BIG || k==FLOAT;break;
    case 5:yes=k==PID;break;
    case 6:yes=k==PORT_TOKEN;break; /* Inert native identity, not an opened port. */
    case 7:yes=k==REF;break;
    case 8:yes=k==TUPLE || k==EMPTY_TUPLE;break;
    case 9:
        if(guard) return k==TUPLE?am_true:am_false;
        return k==TUPLE?make_small(3):k==EMPTY_TUPLE?make_small(0):THE_NON_VALUE;
    default:CHECK(0);
    }
    return yes?am_true:am_false;
}
static Eterm sample(LbProcess *p,Kind kind)
{
    Eterm *h=p->htop;
    switch(kind) {
    case SMALL:return make_small(-1);
    case BIG:h[0]=_make_header(2,NEG_BIG_SUBTAG);h[1]=0;h[2]=1;p->htop+=3;return make_boxed(h);
    case FLOAT:{ double f=-0.5;h[0]=_make_header(1,FLOAT_SUBTAG);memcpy(h+1,&f,sizeof(f));p->htop+=2;return make_boxed(h); }
    case TRUE:return am_true;case FALSE:return am_false;case ATOM:return am_ok;case NIL_VALUE:return NIL;
    case CONS_VALUE:p->htop+=2;return CONS(h,am_ok,am_false);
    case EMPTY_TUPLE:h[0]=make_arityval_zero();h[1]=THE_NON_VALUE;p->htop+=2;return make_tuple(h);
    case TUPLE:p->htop+=4;return TUPLE3(h,am_ok,am_true,NIL);
    case MAP:h[0]=LB_FLATMAP_HEADER;h[1]=0;h[2]=make_tuple(h+3);h[3]=make_arityval_zero();h[4]=THE_NON_VALUE;p->htop+=5;return make_boxed(h);
    case BINARY:case BITS:{ unsigned char b=0xa5;Eterm term;CHECK(lb_bitstring_build(p->entry_module->space->domain,&p->off_heap,&p->htop,&b,kind==BINARY?8:7,0,&term)==LB_ALLOC_OK);return term; }
    case PID:return lb_bif_self_0(p,NULL,NULL);
    case REF:return lb_bif_make_ref_0(p,NULL,NULL);
    case PORT_TOKEN:return _TAG_IMMED1_PORT; /* Native #Port<0.0> tag; no host resource exists here. */
    default:CHECK(0);return THE_NON_VALUE;
    }
}
static LbProcess *create(LbWorld *w,LbCodeModule *m,const char *function,unsigned index,Kind kind,int variant)
{
    LbCodeEntry *entry;LbProcess *p;Eterm args[2]={NIL,NIL};size_t arity=variant?2:1;
    CHECK(lb_code_entry_acquire(w->space,m->name,atom(w,function),arity,&entry)==LB_CODE_OK);
    CHECK(lb_process_create(entry,args,arity,512,&p)==LB_PROCESS_READY);lb_code_entry_release(entry);
    Eterm value=sample(p,kind);CHECK(!is_non_value(value));p->reg[variant==1?1:0]=value;
    if(variant==1) p->reg[0]=atom(w,names[index]);
    if(variant==2) p->reg[1]=atom(w,"yes");
    return p;
}
static LbProcessStatus run(LbProcess *p)
{ LbProcessStatus s=LB_PROCESS_READY;for(unsigned i=0;i<1000 && (s==LB_PROCESS_READY || s==LB_PROCESS_YIELDED);++i)s=lb_process_run(p,1,100);return s; }
static void outcome(LbProcess *p,Eterm value)
{
    if(is_non_value(value)) CHECK(p->status==LB_PROCESS_EXCEPTION && p->exception_value==am_badarg);
    else CHECK(p->status==LB_PROCESS_DONE && lb_process_result(p)==value);
}
static void *deny(void *c,size_t n) { (void)c;(void)n;return NULL; }
static void primitive(LbWorld *w,LbCodeModule *m,unsigned index,Kind kind)
{
    LbAllocDomain *d=w->space->domain;
    for(unsigned credit=0;credit<3;++credit) {
        LbProcess *p=create(w,m,"identity",index,kind,0);LbWorkBudget *b=lb_work_budget_create(w);CHECK(b);
        b->limit=b->remaining=credit;lb_work_domain_borrow(d,b);
        p->heap_reserved=7;while(p->htop<p->stop-S_RESERVED-7)*p->htop++=NIL;
        Eterm input=p->reg[0],*top=p->htop;size_t live=d->current,limit=d->limit,atoms=lb_atoms_count(w->space->atoms);
        LbSystemAllocator allocator=d->system;d->system.allocate=deny;d->limit=live;
        Eterm value=primitives[index](p,p->reg,NULL);
        if(!credit) CHECK(is_non_value(value) && p->status==LB_PROCESS_WORK_LIMIT);
        else { CHECK(value==expected(index,kind,0) && b->remaining==credit-1);if(is_non_value(value))CHECK(p->freason==LB_EXC_BADARG); }
        CHECK(p->htop==top && p->reg[0]==input && p->heap_reserved==7 && !p->collections && d->current==live && lb_atoms_count(w->space->atoms)==atoms);
        d->limit=limit;d->system=allocator;lb_work_domain_borrow(d,NULL);lb_work_domain_clear_failure(d);lb_alloc_domain_begin_operation(d);
        lb_process_destroy(p);lb_work_budget_release(b);
    }
}
static void work_prefixes(LbWorld *w,LbCodeModule *m,unsigned index,Kind kind,int dynamic)
{
    LbAllocDomain *d=w->space->domain; uint64_t required=0;
    for(uint64_t pass=0;pass<=required+1;++pass) {
        LbProcess *p=create(w,m,dynamic?"dynamic":bodies[index],index,kind,dynamic);
        LbWorkBudget *budget=lb_work_budget_create(w); CHECK(budget);
        uint64_t credit=pass?pass-1:1000000; budget->limit=budget->remaining=credit;
        lb_work_domain_borrow(d,budget); LbProcessStatus status=run(p);
        if(!pass || credit==required) {
            outcome(p,expected(index,kind,0));
            if(!pass) { required=credit-budget->remaining; CHECK(required>1 && required<10000); }
            else CHECK(!budget->remaining);
        } else CHECK(status==LB_PROCESS_WORK_LIMIT);
        lb_work_domain_borrow(d,NULL); lb_work_domain_clear_failure(d); lb_alloc_domain_begin_operation(d);
        lb_process_destroy(p); lb_work_budget_release(budget);
    }
}
int main(int argc,char **argv)
{
    CHECK(argc==2 || argc==3);
    if(argc==3) {
        FILE *oracle=fopen(argv[2],"r"); CHECK(oracle);
        for(unsigned k=0;k<KINDS;++k) {
            unsigned row,mask; int size;
            CHECK(fscanf(oracle,"%u %u %d",&row,&mask,&size)==3 && row==k && mask<512);
            for(unsigned i=0;i<9;++i) CHECK(expected(i,(Kind)k,0)==((mask&(1u<<i))?am_true:am_false));
            CHECK(expected(9,(Kind)k,0)==(size<0?THE_NON_VALUE:make_small(size)));
        }
        unsigned extra; CHECK(fscanf(oracle,"%u",&extra)==EOF && !fclose(oracle));
    }
    FILE *f=fopen(argv[1],"rb");CHECK(f && !fseek(f,0,SEEK_END));long n=ftell(f);CHECK(n>0 && !fseek(f,0,SEEK_SET));
    unsigned char *bytes=malloc((size_t)n);CHECK(bytes && fread(bytes,1,(size_t)n,f)==(size_t)n && !fclose(f));
    LbEngine *engine;LbWorld *w;CHECK(lb_engine_create(NULL,&engine)==LB_ENGINE_OK && lb_world_create(engine,&w)==LB_WORLD_OK);
    lb_engine_enter(engine);LbCodeModule *m;LbBeamError error;LbCodeStatus status=lb_code_load(w->space,bytes,(size_t)n,&m,&error);
    if(status!=LB_CODE_OK)fprintf(stderr,"load=%d stage=%s offset=%zu\n",status,error.stage,error.offset);
    CHECK(status==LB_CODE_OK);free(bytes);
    for(unsigned i=0;i<10;++i)for(Kind k=0;k<KINDS;++k) {
        primitive(w,m,i,k);
        for(unsigned mode=0;mode<4;++mode) {
            char y_name[64]; CHECK(snprintf(y_name,sizeof(y_name),"%s_y",guards[i])>0);
            LbProcess *p=create(w,m,mode==3?y_name:mode==2?"dynamic":mode==1?guards[i]:bodies[i],i,k,mode==3?2:mode==2?1:0);
            Eterm want=expected(i,k,mode==1 || mode==3);
            if(mode==1 || mode==3) want=atom(w,want==am_true?"yes":"no");
            (void)run(p);outcome(p,want);lb_process_destroy(p);
        }
    }
    for(Kind k=0;k<KINDS;++k) {
        LbProcess *p=create(w,m,"size_guard_bool",9,k,0);
        (void)run(p); outcome(p,expected(9,k,1)); lb_process_destroy(p);
    }
    const Kind positives[]={BITS,TRUE,FLOAT,MAP,BIG,PID,PORT_TOKEN,REF,TUPLE,TUPLE};
    for(unsigned i=0;i<10;++i) for(int dynamic=0;dynamic<2;++dynamic) {
        work_prefixes(w,m,i,positives[i],dynamic); work_prefixes(w,m,i,NIL_VALUE,dynamic);
    }
    lb_engine_leave(engine);LbReclamation *r;CHECK(lb_world_stop(w,&r)==LB_WORLD_OK && lb_reclamation_poll(r)==LB_WORLD_OK);
    CHECK(lb_reclamation_consume(r)==LB_WORLD_OK);lb_reclamation_release(r);
    LbWorldResourceUsage usage;CHECK(lb_world_resource_usage(w,&usage)==LB_WORLD_OK && usage.closed && !usage.current);
    lb_world_release(w);CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK);lb_engine_release(engine);
    puts("CORE_TYPE_PREDICATES_OK ten_mfas=true native_tags=true direct_dynamic_guards=true primitive_work=true allocation_free=true reclaimed=true");
}
