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
#ifndef LB_TEST_LIST_INPUT
#define LB_TEST_LIST_INPUT 0
#endif
static const int list_input=LB_TEST_LIST_INPUT;
static Eterm convert(LbProcess *p)
{ return list_input?lb_bif_list_to_tuple_1(p,p->reg,NULL):lb_bif_tuple_to_list_1(p,p->reg,NULL); }
static LbProcess *create(LbWorld *w,LbCodeModule *m,size_t count,size_t bytes,int gc)
{
    Eterm function; LbCodeEntry *entry; LbProcess *p; unsigned char input[100]; memset(input,0x71,sizeof(input));
    CHECK(count<=129 && bytes<=sizeof(input) && lb_atoms_find(w->space->atoms,"convert",7,&function)==LB_ATOM_OK);
    CHECK(lb_code_entry_acquire(w->space,m->name,function,1,&entry)==LB_CODE_OK);
    CHECK(lb_process_create_binary(entry,input,bytes,1024,&p)==LB_PROCESS_READY); lb_code_entry_release(entry);
    if(count) {
        Eterm value=p->reg[0],*hp=p->htop; hp[0]=make_arityval(count);
        for(size_t i=1;i<=count;++i) hp[i]=i%2?make_small(-(Sint)i):value;
        p->reg[0]=make_tuple(hp); p->htop+=count+1;
    } else p->reg[0]=make_tuple(w->space->engine->empty_tuple)|TAG_LITERAL_PTR;
    Eterm expected=p->reg[0];
    if(list_input) {
        Eterm result=NIL,*t=tuple_val(expected);
        for(size_t i=count;i>0;--i) { result=CONS(p->htop,t[i],result); p->htop+=2; }
        p->reg[0]=result;
    }
    p->construction_root=TUPLE2(p->htop,p->reg[0],expected); p->htop+=3;
    for(unsigned i=0;i<100 && p->i[0]!=op_call_bif_W;++i) CHECK(lb_process_run(p,1,100)==LB_PROCESS_YIELDED);
    CHECK(p->i[0]==op_call_bif_W); p->heap_reserved=13;
    if(gc) while(p->htop<p->stop-S_RESERVED-p->heap_reserved) *p->htop++=NIL;
    return p;
}
static void source(LbProcess *p,size_t count,size_t bytes)
{
    Eterm *roots=tuple_val(p->construction_root),*t=tuple_val(roots[2]); CHECK(arityval(t[0])==count);
    if(list_input) {
        Eterm tail=roots[1];
        for(size_t i=1;i<=count;++i) { CHECK(is_list(tail) && CAR(list_val(tail))==t[i]); tail=CDR(list_val(tail)); }
        CHECK(is_nil(tail));
    } else CHECK(roots[1]==roots[2]);
    for(size_t i=1;i<=count;++i) {
        if(i%2) CHECK(t[i]==make_small(-(Sint)i));
        else { LbBitstringView view; CHECK(lb_bitstring_view(t[i],&view) && view.bit_size==8*bytes && view.data[0]==0x71 && view.data[bytes-1]==0x71); CHECK(t[i]==t[2]); }
    }
}
static void check_result(LbProcess *p,Eterm result,size_t count,size_t bytes)
{
    source(p,count,bytes); Eterm *t=tuple_val(tuple_val(p->construction_root)[2]);
    if(list_input) {
        CHECK(is_tuple(result) && arityval(*tuple_val(result))==count);
        CHECK(count?!is_literal_ptr(result):is_literal_ptr(result));
        for(size_t i=1;i<=count;++i) CHECK(tuple_val(result)[i]==t[i]);
    } else {
        for(size_t i=1;i<=count;++i) { CHECK(is_list(result) && !is_literal_ptr(result)); Eterm *cell=list_val(result); CHECK(CAR(cell)==t[i]); result=CDR(cell); }
        CHECK(is_nil(result));
    }
}
static LbProcessStatus run(LbProcess *p)
{ LbProcessStatus s=LB_PROCESS_YIELDED; for(unsigned i=0;i<100 && s==LB_PROCESS_YIELDED;++i) s=lb_process_run(p,1,100); return s; }
static void semantics(LbWorld *w,LbCodeModule *m)
{
    const size_t counts[]={0,1,4,64,129};
    for(size_t i=0;i<5;++i) for(size_t bytes=16;bytes<=100;bytes+=84) for(int gc=0;gc<2;++gc) {
        LbProcess *p=create(w,m,counts[i],bytes,gc);
        if(gc) while(p->htop<p->stop-S_RESERVED) *p->htop++=NIL;
        CHECK(run(p)==LB_PROCESS_DONE); check_result(p,lb_process_result(p),counts[i],bytes);
        if(gc && counts[i]) CHECK(p->collections);
        CHECK(lb_process_collect(p,0)==LB_PROCESS_DONE); check_result(p,lb_process_result(p),counts[i],bytes); lb_process_destroy(p);
    }
    for(unsigned kind=0;kind<6;++kind) {
        LbProcess *p=create(w,m,2,100,0);
        if(kind==0) p->reg[0]=list_input?tuple_val(p->construction_root)[2]:NIL;
        if(kind==1) p->reg[0]=am_ok;
        if(kind==2) p->reg[0]=make_small(42);
        if(kind==3) p->reg[0]=tuple_val(tuple_val(p->construction_root)[2])[2];
        if(kind==4) { p->reg[0]=CONS(p->htop,am_ok,list_input?am_true:NIL); p->htop+=2; }
        if(kind==5) { double v=1.0; p->htop[0]=_make_header(1,FLOAT_SUBTAG); memcpy(p->htop+1,&v,sizeof(v)); p->reg[0]=make_boxed(p->htop); p->htop+=2; }
        CHECK(run(p)==LB_PROCESS_EXCEPTION && p->exception_value==am_badarg); source(p,2,100); lb_process_destroy(p);
    }
}
static void *deny(void *ctx,size_t bytes) { (void)ctx; (void)bytes; return NULL; }
static void prefixes(LbWorld *w,LbCodeModule *m,size_t count)
{
    LbAllocDomain *d=w->space->domain; size_t baseline=d->current,blocks=d->block_count;
    for(unsigned mode=0;mode<3;++mode) {
        LbProcess *p=create(w,m,count,100,1); LbSystemAllocator system=d->system; size_t limit=d->limit; Eterm *top=p->htop;
        if(mode==0) d->limit=d->current; if(mode==1) d->system.allocate=deny;
        Eterm result=convert(p);
        if(count && mode<2) CHECK(is_non_value(result) && p->status==(mode?LB_PROCESS_NO_MEMORY:LB_PROCESS_LIMIT) && p->htop==top);
        else { check_result(p,result,count,100); CHECK(count?p->collections>0:p->htop==top && !p->collections); }
        CHECK(p->heap_reserved==13); source(p,count,100);
        d->system=system; d->limit=limit; lb_alloc_domain_begin_operation(d); lb_process_destroy(p); CHECK(d->current==baseline && d->block_count==blocks);
    }
    for(int gc=0;gc<2;++gc) {
        uint64_t required=0;
        for(uint64_t pass=0;pass<=required+1;++pass) {
            LbProcess *p=create(w,m,count,16,gc); LbWorkBudget *budget=lb_work_budget_create(w); CHECK(budget);
            uint64_t credit=pass?pass-1:1000000; budget->limit=budget->remaining=credit; lb_work_domain_borrow(d,budget);
            Eterm result=convert(p);
            if(!pass) { check_result(p,result,count,16); required=credit-budget->remaining; CHECK(required>=1 && required<100000); }
            else if(credit<required) CHECK(is_non_value(result) && p->status==LB_PROCESS_WORK_LIMIT);
            else { check_result(p,result,count,16); CHECK(!budget->remaining); }
            CHECK(p->heap_reserved==13); source(p,count,16);
            lb_work_domain_borrow(d,NULL); lb_work_domain_clear_failure(d); lb_alloc_domain_begin_operation(d);
            lb_process_destroy(p); lb_work_budget_release(budget); CHECK(d->current==baseline && d->block_count==blocks);
        }
    }
}
static void bad_tail_prefixes(LbWorld *w,LbCodeModule *m)
{
    if(!list_input) return;
    LbAllocDomain *d=w->space->domain; size_t baseline=d->current;
    for(uint64_t credit=0;credit<=66;++credit) {
        LbProcess *p=create(w,m,65,16,0); Eterm tail=p->reg[0];
        for(unsigned i=1;i<65;++i) tail=CDR(list_val(tail));
        CDR(list_val(tail))=am_ok; Eterm *top=p->htop;
        LbWorkBudget *budget=lb_work_budget_create(w); CHECK(budget); budget->remaining=budget->limit=credit;
        lb_work_domain_borrow(d,budget); CHECK(is_non_value(convert(p)) && !budget->remaining);
        if(credit<66) CHECK(p->status==LB_PROCESS_WORK_LIMIT && p->freason==LB_EXC_NULL);
        else CHECK(p->freason==LB_EXC_BADARG && !budget->exhausted);
        Eterm *roots=tuple_val(p->construction_root),*expected=tuple_val(roots[2]); tail=roots[1];
        for(size_t i=1;i<=65;++i) { CHECK(is_list(tail) && CAR(list_val(tail))==expected[i]); tail=CDR(list_val(tail)); }
        CHECK(tail==am_ok && p->htop==top && !p->collections);
        lb_work_domain_borrow(d,NULL); lb_work_domain_clear_failure(d); lb_alloc_domain_begin_operation(d);
        lb_process_destroy(p); lb_work_budget_release(budget); CHECK(d->current==baseline);
    }
}
int main(int argc,char **argv)
{
    CHECK(argc==2); FILE *f=fopen(argv[1],"rb"); CHECK(f && !fseek(f,0,SEEK_END)); long size=ftell(f); CHECK(size>0 && !fseek(f,0,SEEK_SET));
    unsigned char *bytes=malloc((size_t)size); CHECK(bytes && fread(bytes,1,(size_t)size,f)==(size_t)size && !fclose(f));
    LbEngine *engine; LbWorld *w; CHECK(lb_engine_create(NULL,&engine)==LB_ENGINE_OK && lb_world_create(engine,&w)==LB_WORLD_OK);
    lb_engine_enter(engine); LbCodeModule *m; LbBeamError error; LbCodeStatus loaded=lb_code_load(w->space,bytes,(size_t)size,&m,&error);
    if(loaded!=LB_CODE_OK) fprintf(stderr,"tuple/list load=%d stage=%s\n",loaded,error.stage?error.stage:"unknown"); CHECK(loaded==LB_CODE_OK); free(bytes);
    size_t baseline=w->space->domain->current; semantics(w,m); prefixes(w,m,0); prefixes(w,m,65); bad_tail_prefixes(w,m); CHECK(w->space->domain->current==baseline); lb_engine_leave(engine);
    LbReclamation *r; CHECK(lb_world_stop(w,&r)==LB_WORLD_OK && lb_reclamation_poll(r)==LB_WORLD_OK && lb_reclamation_consume(r)==LB_WORLD_OK); lb_reclamation_release(r); lb_world_release(w);
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine);
    printf("CORE_TUPLE_LIST_DIRECTION list_input=%d\n",list_input);
    puts("CORE_TUPLE_LIST_OK actual_bif=true empty=true order=true shared_elements=true source_immutable=true moving_gc=true badarg=true heap_credit=true allocation_work_prefixes=true balanced=true"); return 0;
}
