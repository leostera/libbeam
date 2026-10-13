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
static LbProcess *create(LbWorld *w,LbCodeModule *m,Sint count,size_t bytes,int gc)
{
    Eterm function; LbCodeEntry *entry; LbProcess *p; unsigned char input[100]; memset(input,0x7f,sizeof(input));
    CHECK(bytes<=sizeof(input) && lb_atoms_find(w->space->atoms,"build",5,&function)==LB_ATOM_OK);
    CHECK(lb_code_entry_acquire(w->space,m->name,function,1,&entry)==LB_CODE_OK);
    CHECK(lb_process_create_binary(entry,input,bytes,1024,&p)==LB_PROCESS_READY); lb_code_entry_release(entry);
    p->reg[0]=TUPLE2(p->htop,make_small(count),p->reg[0]); p->htop+=3;
    for(unsigned i=0;i<100 && p->i[0]!=op_call_bif_W;++i) CHECK(lb_process_run(p,1,100)==LB_PROCESS_YIELDED);
    CHECK(p->i[0]==op_call_bif_W); p->heap_reserved=13;
    if(gc) while(p->htop<p->stop-S_RESERVED-p->heap_reserved) *p->htop++=NIL;
    return p;
}
static void binary(Eterm value,size_t bytes)
{ LbBitstringView view; CHECK(lb_bitstring_view(value,&view) && view.bit_size==8*bytes && view.data[0]==0x7f && view.data[bytes-1]==0x7f); }
static void tuple(Eterm value,size_t count,size_t bytes)
{
    CHECK(is_tuple(value) && arityval(*tuple_val(value))==count);
    if(!count) { CHECK(is_literal_ptr(value) && tuple_val(value)[1]==THE_NON_VALUE); return; }
    CHECK(!is_literal_ptr(value)); Eterm *t=tuple_val(value); binary(t[1],bytes);
    for(size_t i=1;i<=count;++i) CHECK(t[i]==t[1]);
}
static LbProcessStatus run(LbProcess *p)
{ LbProcessStatus s=LB_PROCESS_YIELDED; for(unsigned i=0;i<100 && s==LB_PROCESS_YIELDED;++i) s=lb_process_run(p,1,100); return s; }
static void semantics(LbWorld *w,LbCodeModule *m)
{
    const size_t counts[]={0,1,4,64,129};
    for(size_t n=0;n<5;++n) for(size_t bytes=16;bytes<=100;bytes+=84) for(int gc=0;gc<2;++gc) {
        LbProcess *p=create(w,m,(Sint)counts[n],bytes,gc);
        if(gc) while(p->htop<p->stop-S_RESERVED) *p->htop++=NIL;
        CHECK(run(p)==LB_PROCESS_DONE); tuple(lb_process_result(p),counts[n],bytes);
        if(gc && counts[n]) CHECK(p->collections);
        CHECK(lb_process_collect(p,0)==LB_PROCESS_DONE); tuple(lb_process_result(p),counts[n],bytes); lb_process_destroy(p);
    }
    for(unsigned kind=0;kind<6;++kind) {
        LbProcess *p=create(w,m,1,16,0); Eterm *hp=p->htop;
        if(kind==0) p->reg[0]=make_small(-1);
        if(kind==1) p->reg[0]=make_small((Sint)MAX_ARITYVAL+1);
        if(kind==2) p->reg[0]=NIL;
        if(kind==3) p->reg[0]=am_false;
        if(kind==4) { hp[0]=_make_header(1,POS_BIG_SUBTAG); hp[1]=(Uint)1<<60; p->reg[0]=make_boxed(hp); p->htop+=2; }
        if(kind==5) { double v=1.0; hp[0]=_make_header(1,FLOAT_SUBTAG); memcpy(hp+1,&v,sizeof(v)); p->reg[0]=make_boxed(hp); p->htop+=2; }
        CHECK(run(p)==LB_PROCESS_EXCEPTION && p->exception_value==am_badarg); lb_process_destroy(p);
    }
    LbProcess *p=create(w,m,MAX_ARITYVAL,16,0);
    CHECK(is_non_value(lb_bif_make_tuple_2(p,p->reg,NULL)) && p->status==LB_PROCESS_LIMIT && p->freason==LB_EXC_NULL); lb_process_destroy(p);
}
static void *deny(void *ctx,size_t bytes) { (void)ctx; (void)bytes; return NULL; }
static void prefixes(LbWorld *w,LbCodeModule *m,size_t count)
{
    LbAllocDomain *d=w->space->domain; size_t baseline=d->current,blocks=d->block_count;
    for(unsigned mode=0;mode<3;++mode) {
        LbProcess *p=create(w,m,(Sint)count,100,1); LbSystemAllocator system=d->system; size_t limit=d->limit;
        Eterm *top=p->htop;
        if(mode==0) d->limit=d->current; if(mode==1) d->system.allocate=deny;
        Eterm result=lb_bif_make_tuple_2(p,p->reg,NULL);
        if(count && mode<2) CHECK(is_non_value(result) && p->status==(mode?LB_PROCESS_NO_MEMORY:LB_PROCESS_LIMIT) && p->htop==top);
        else { tuple(result,count,100); CHECK(count?p->collections>0:p->htop==top && !p->collections); }
        CHECK(p->heap_reserved==13); binary(p->reg[1],100);
        d->system=system; d->limit=limit; lb_alloc_domain_begin_operation(d); lb_process_destroy(p);
        CHECK(d->current==baseline && d->block_count==blocks);
    }
    for(int gc=0;gc<2;++gc) {
        uint64_t required=0;
        for(uint64_t pass=0;pass<=required+1;++pass) {
            LbProcess *p=create(w,m,(Sint)count,16,gc); LbWorkBudget *budget=lb_work_budget_create(w); CHECK(budget);
            uint64_t credit=pass?pass-1:1000000; budget->limit=budget->remaining=credit; lb_work_domain_borrow(d,budget);
            Eterm result=lb_bif_make_tuple_2(p,p->reg,NULL);
            if(!pass) { tuple(result,count,16); required=credit-budget->remaining; CHECK(required>=1 && required<100000); }
            else if(credit<required) CHECK(is_non_value(result) && p->status==LB_PROCESS_WORK_LIMIT);
            else { tuple(result,count,16); CHECK(!budget->remaining); }
            CHECK(p->heap_reserved==13); binary(p->reg[1],16);
            lb_work_domain_borrow(d,NULL); lb_work_domain_clear_failure(d); lb_alloc_domain_begin_operation(d);
            lb_process_destroy(p); lb_work_budget_release(budget); CHECK(d->current==baseline && d->block_count==blocks);
        }
    }
}
static void close_world(LbWorld *w)
{ LbReclamation *r; CHECK(lb_world_stop(w,&r)==LB_WORLD_OK && lb_reclamation_poll(r)==LB_WORLD_OK && lb_reclamation_consume(r)==LB_WORLD_OK); lb_reclamation_release(r); lb_world_release(w); }
int main(int argc,char **argv)
{
    CHECK(argc==2); FILE *f=fopen(argv[1],"rb"); CHECK(f && !fseek(f,0,SEEK_END)); long size=ftell(f); CHECK(size>0 && !fseek(f,0,SEEK_SET));
    unsigned char *bytes=malloc((size_t)size); CHECK(bytes && fread(bytes,1,(size_t)size,f)==(size_t)size && !fclose(f));
    LbEngine *engine; LbWorld *w,*peer; CHECK(lb_engine_create(NULL,&engine)==LB_ENGINE_OK && lb_world_create(engine,&w)==LB_WORLD_OK && lb_world_create(engine,&peer)==LB_WORLD_OK);
    lb_engine_enter(engine); LbCodeModule *m,*pm; LbBeamError error;
    CHECK(lb_code_load(w->space,bytes,(size_t)size,&m,&error)==LB_CODE_OK && lb_code_load(peer->space,bytes,(size_t)size,&pm,&error)==LB_CODE_OK); free(bytes);
    size_t baseline=w->space->domain->current; semantics(w,m); prefixes(w,m,0); prefixes(w,m,65); CHECK(w->space->domain->current==baseline);
    LbProcess *survivor=create(peer,pm,0,16,1); survivor->construction_root=lb_bif_make_tuple_2(survivor,survivor->reg,NULL);
    CHECK(survivor->construction_root==(make_tuple(engine->empty_tuple)|TAG_LITERAL_PTR));
    lb_engine_leave(engine); close_world(w); lb_engine_enter(engine);
    CHECK(lb_process_collect(survivor,0)==LB_PROCESS_YIELDED); tuple(survivor->construction_root,0,16); lb_process_destroy(survivor);
    lb_engine_leave(engine); close_world(peer);
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine);
    puts("CORE_MAKE_TUPLE_OK actual_bif=true empty_singleton=true alias_fill=true heap_ref_binaries=true moving_gc=true badarg=true native_arity_bound=true heap_credit=true allocation_work_prefixes=true balanced=true"); return 0;
}
