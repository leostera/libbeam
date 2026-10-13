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
static Eterm atom(LbWorld *w,const char *name)
{ Eterm value; CHECK(lb_atoms_find(w->space->atoms,name,strlen(name),&value)==LB_ATOM_OK); return value; }
static LbProcess *create(LbWorld *w,LbCodeModule *m,const char *name)
{
    LbCodeEntry *entry; LbProcess *p; Eterm arg=NIL;
    CHECK(lb_code_entry_acquire(w->space,m->name,atom(w,name),1,&entry)==LB_CODE_OK);
    CHECK(lb_process_create(entry,&arg,1,2048,&p)==LB_PROCESS_READY); lb_code_entry_release(entry); return p;
}
static Eterm selectors(LbProcess *p,size_t count,Eterm tail)
{ while(count--) { tail=CONS(p->htop,am_error_handler,tail); p->htop+=2; } return tail; }
static LbProcess *query(LbWorld *w,LbCodeModule *m,int count)
{
    LbProcess *p=create(w,m,"query"); p->reg[0]=count<0?am_error_handler:selectors(p,(size_t)count,NIL); return p;
}
static int at_bif(LbProcess *p)
{
    if(p->i[0]!=op_call_bif_W) return 0;
    LbBifFn fn; memcpy(&fn,&p->i[1],sizeof(fn)); return fn==lb_bif_process_info_2;
}
static void enter(LbProcess *p)
{ for(unsigned i=0;i<100 && !at_bif(p);++i) CHECK(lb_process_run(p,1,100)==LB_PROCESS_YIELDED); CHECK(at_bif(p)); }
static void fill(LbProcess *p) { while(p->htop<p->stop-S_RESERVED-p->heap_reserved) *p->htop++=NIL; }
static LbProcessStatus run(LbProcess *p)
{
    LbProcessStatus s=LB_PROCESS_READY;
    for(size_t i=0;i<1000 && (s==LB_PROCESS_READY || s==LB_PROCESS_YIELDED);++i) s=lb_process_run(p,1,100);
    return s;
}
static void tuple(Eterm value,Eterm handler)
{ CHECK(is_tuple(value) && arityval(*tuple_val(value))==2 && tuple_val(value)[1]==am_error_handler && tuple_val(value)[2]==handler); }
static void result(Eterm value,int count,Eterm handler)
{
    if(count<0) { tuple(value,handler); return; }
    while(count--) { CHECK(is_list(value)); tuple(list_val(value)[0],handler); value=list_val(value)[1]; }
    CHECK(value==NIL);
}
typedef struct { LbSystemAllocator backing; size_t left; } Prefix;
static void *allocate(void *context,size_t size)
{ Prefix *p=context; if(!p->left) return NULL; --p->left; return p->backing.allocate(p->backing.context,size); }
static void release(void *context,void *ptr)
{ Prefix *p=context; p->backing.release(p->backing.context,ptr); }
static void boundaries(LbWorld *w,LbCodeModule *m,int count)
{
    LbAllocDomain *d=w->space->domain; size_t baseline=d->current;
    for(unsigned gc=0;gc<2;++gc) {
        LbProcess *p=query(w,m,count); enter(p); if(gc) fill(p);
        CHECK(run(p)==LB_PROCESS_DONE); result(lb_process_result(p),count,am_error_handler);
        if(gc && count) CHECK(p->collections>0);
        CHECK(lb_process_collect(p,0)==LB_PROCESS_DONE); result(lb_process_result(p),count,am_error_handler); lb_process_destroy(p);
        uint64_t required=0;
        for(uint64_t pass=0;pass<=required+1;++pass) {
            p=query(w,m,count); enter(p); if(gc) fill(p);
            LbWorkBudget *budget=lb_work_budget_create(w); CHECK(budget); uint64_t credit=pass?pass-1:1000000;
            budget->limit=budget->remaining=credit; lb_work_domain_borrow(d,budget); LbProcessStatus s=run(p);
            if(!pass) { CHECK(s==LB_PROCESS_DONE); required=credit-budget->remaining; CHECK(required>1 && required<20000); }
            else if(credit<required) CHECK(s==LB_PROCESS_WORK_LIMIT);
            else CHECK(s==LB_PROCESS_DONE && !budget->remaining);
            if(s==LB_PROCESS_DONE) result(lb_process_result(p),count,am_error_handler);
            lb_work_domain_borrow(d,NULL); lb_work_domain_clear_failure(d); lb_alloc_domain_begin_operation(d);
            lb_process_destroy(p); lb_work_budget_release(budget); CHECK(d->current==baseline);
        }
    }
    for(unsigned direct=0;direct<2;++direct) {
        int done=0;
        for(size_t prefix=0;prefix<20 && !done;++prefix) {
            LbProcess *p=query(w,m,count); enter(p); if(direct) p->heap_reserved=11; fill(p);
            size_t credit=p->heap_reserved,limit=d->limit,live=d->current; Eterm *heap=p->htop;
            Prefix state={d->system,prefix?prefix-1:0};
            if(!prefix) d->limit=live; else d->system=(LbSystemAllocator){&state,allocate,release};
            Eterm output;
            if(direct) output=lb_bif_process_info_2(p,p->reg,NULL);
            else { (void)run(p); output=lb_process_result(p); }
            d->limit=limit; d->system=state.backing;
            CHECK(p->heap_reserved==credit);
            if(is_non_value(output)) {
                CHECK(count && p->status==(prefix?LB_PROCESS_NO_MEMORY:LB_PROCESS_LIMIT));
                CHECK(p->htop==heap && !p->collections && d->current==live);
            } else { result(output,count,am_error_handler); done=count?1:prefix==1; }
            lb_alloc_domain_begin_operation(d); lb_process_destroy(p); CHECK(d->current==baseline);
        }
        CHECK(done);
    }
}
static void semantics(LbWorld *w,LbCodeModule *m,LbProcess *target,Eterm pid)
{
    /* Private state setup proves the query reads its target. This is not
     * admission of process_flag or custom error-handler execution. */
    target->error_handler=am_true;
    for(int count=-1;count<=2;++count) {
        LbProcess *p=create(w,m,"other"); Eterm selector=count<0?am_error_handler:selectors(p,(size_t)count,NIL);
        p->reg[0]=TUPLE2(p->htop,pid,selector); p->htop+=3;
        CHECK(run(p)==LB_PROCESS_DONE); result(lb_process_result(p),count,am_true); lb_process_destroy(p);
    }
    for(unsigned kind=0;kind<4;++kind) {
        LbProcess *p=create(w,m,"query");
        p->reg[0]=kind==0?make_small(1):kind==1?selectors(p,1,am_true):kind==2?am_true:CONS(p->htop,make_small(1),NIL);
        if(kind==3) p->htop+=2;
        CHECK(run(p)==(kind==2?LB_PROCESS_REFUSED:LB_PROCESS_EXCEPTION));
        if(kind!=2) CHECK(p->exception_value==am_badarg); lb_process_destroy(p);
    }
    LbProcess *p=create(w,m,"other"); p->reg[0]=TUPLE2(p->htop,am_true,am_error_handler); p->htop+=3;
    CHECK(run(p)==LB_PROCESS_EXCEPTION && p->exception_value==am_badarg); lb_process_destroy(p);
    lb_process_destroy(target);
    for(int count=-1;count<=1;++count) {
        p=create(w,m,"other"); Eterm selector=count<0?am_error_handler:selectors(p,(size_t)count,NIL);
        p->reg[0]=TUPLE2(p->htop,pid,selector); p->htop+=3; CHECK(run(p)==LB_PROCESS_DONE && lb_process_result(p)==am_undefined); lb_process_destroy(p);
    }
    p=create(w,m,"other"); Eterm improper=selectors(p,1,am_true);
    p->reg[0]=TUPLE2(p->htop,pid,improper); p->htop+=3;
    CHECK(run(p)==LB_PROCESS_EXCEPTION && p->exception_value==am_badarg); lb_process_destroy(p);
}
int main(int argc,char **argv)
{
    FILE *f; CHECK(argc==2 && (f=fopen(argv[1],"rb")) && !fseek(f,0,SEEK_END)); long size=ftell(f); CHECK(size>0 && !fseek(f,0,SEEK_SET));
    unsigned char *bytes=malloc((size_t)size); CHECK(bytes && fread(bytes,1,(size_t)size,f)==(size_t)size && !fclose(f));
    LbEngine *engine; LbWorld *w; LbCodeModule *m; LbBeamError error;
    CHECK(lb_engine_create(NULL,&engine)==LB_ENGINE_OK && lb_world_create(engine,&w)==LB_WORLD_OK); lb_engine_enter(engine);
    CHECK(lb_code_load(w->space,bytes,(size_t)size,&m,&error)==LB_CODE_OK); free(bytes);
    LbProcess *target=create(w,m,"identity"); Eterm pid; CHECK(lb_process_identity(target,&pid)==LB_PROCESS_READY && target->error_handler==am_error_handler);
    boundaries(w,m,-1); boundaries(w,m,0); boundaries(w,m,2); boundaries(w,m,128); semantics(w,m,target,pid);
    lb_engine_leave(engine); LbReclamation *r;
    CHECK(lb_world_stop(w,&r)==LB_WORLD_OK && lb_reclamation_poll(r)==LB_WORLD_OK && lb_reclamation_consume(r)==LB_WORLD_OK);
    lb_reclamation_release(r); lb_world_release(w); CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine);
    puts("CORE_PROCESS_INFO_OK actual_error_handler=true self_other_dead=true selector_lists=true badarg_refusal=true moving_gc=true heap_credit=true allocation_work_prefixes=true balanced=true"); return 0;
}
