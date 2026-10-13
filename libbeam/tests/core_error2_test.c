/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#include "process_internal.h"
#include "world_internal.h"
#include "work_budget_internal.h"
#include "alloc_internal.h"
#include "stacktrace.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while(0)
static void binary(Eterm value)
{ LbBitstringView v; CHECK(lb_bitstring_view(value,&v) && v.bit_size==800 && v.data[0]==0x6d && v.data[99]==0x6d); }
static LbProcess *create(LbWorld *w,LbCodeModule *m,unsigned kind,int gc)
{
    Eterm function; LbCodeEntry *entry; LbProcess *p; unsigned char bytes[100]; memset(bytes,0x6d,sizeof(bytes));
    CHECK(lb_atoms_find(w->space->atoms,"crash",5,&function)==LB_ATOM_OK);
    CHECK(lb_code_entry_acquire(w->space,m->name,function,1,&entry)==LB_CODE_OK);
    CHECK(lb_process_create_binary(entry,bytes,sizeof(bytes),1024,&p)==LB_PROCESS_READY); lb_code_entry_release(entry);
    Eterm reason=p->reg[0],args=NIL;
    if(kind==1 || kind==2) { args=CONS(p->htop,reason,kind==1?NIL:am_ok); p->htop+=2; }
    if(kind==3) args=am_ok;
    if(kind==4) args=make_small(42);
    if(kind==5) { args=TUPLE1(p->htop,reason); p->htop+=2; }
    p->reg[0]=TUPLE2(p->htop,reason,args); p->htop+=3;
    for(unsigned i=0;i<100 && p->i[0]!=op_call_bif_W;++i) CHECK(lb_process_run(p,1,100)==LB_PROCESS_YIELDED);
    CHECK(p->i[0]==op_call_bif_W); p->heap_reserved=13;
    if(gc) while(p->htop<p->stop-S_RESERVED-p->heap_reserved) *p->htop++=NIL;
    return p;
}
static void wrapper(LbProcess *p,size_t credit)
{
    CHECK(p->freason==LB_EXC_ERROR_2 && is_tuple(p->fvalue) && arityval(*tuple_val(p->fvalue))==2 && p->heap_reserved==credit);
    binary(tuple_val(p->fvalue)[1]); CHECK(tuple_val(p->fvalue)[1]==p->reg[0] && tuple_val(p->fvalue)[2]==p->reg[1]);
}
static LbProcessStatus run(LbProcess *p)
{ LbProcessStatus s=LB_PROCESS_YIELDED; for(unsigned i=0;i<100 && s==LB_PROCESS_YIELDED;++i) s=lb_process_run(p,1,100); return s; }
static void semantics(LbWorld *w,LbCodeModule *m)
{
    for(unsigned kind=0;kind<6;++kind) for(int gc=0;gc<2;++gc) {
        LbProcess *p=create(w,m,kind,gc);
        /* A compiled call invalidates caller heap credit. Force its GC after
         * accounting for that boundary; primitive tests below preserve credit. */
        if(gc) while(p->htop<p->stop-S_RESERVED) *p->htop++=NIL;
        CHECK(run(p)==LB_PROCESS_EXCEPTION); wrapper(p,0); binary(p->exception_value); CHECK(!gc || p->collections);
        Eterm down; CHECK(lb_error_down_reason(p,&down)==LB_PROCESS_READY);
        Eterm *reason=tuple_val(down); binary(reason[1]); CHECK(is_list(reason[2]));
        Eterm *frame=tuple_val(CAR(list_val(reason[2])));
        CHECK(arityval(frame[0])==4 && frame[1]==m->name);
        Eterm crash; CHECK(lb_atoms_find(w->space->atoms,"crash",5,&crash)==LB_ATOM_OK && frame[2]==crash);
        CHECK(frame[3]==(kind<=2?tuple_val(p->fvalue)[2]:make_small(1)));
        p->construction_root=down; CHECK(lb_process_collect(p,0)==LB_PROCESS_EXCEPTION);
        binary(tuple_val(p->construction_root)[1]); binary(p->exception_value); lb_process_destroy(p);
    }
}
static void *deny(void *ctx,size_t n) { (void)ctx; (void)n; return NULL; }
static void prefixes(LbWorld *w,LbCodeModule *m)
{
    LbAllocDomain *d=w->space->domain; size_t baseline=d->current,blocks=d->block_count;
    for(unsigned mode=0;mode<3;++mode) {
        LbProcess *p=create(w,m,1,1); size_t limit=d->limit; LbSystemAllocator system=d->system;
        if(mode==0) d->limit=d->current; if(mode==1) d->system.allocate=deny;
        CHECK(is_non_value(lb_bif_error_2(p,p->reg,NULL)));
        if(mode<2) {
            CHECK(p->status==(mode?LB_PROCESS_NO_MEMORY:LB_PROCESS_LIMIT) && p->freason==LB_EXC_NULL && p->fvalue==NIL);
            binary(p->reg[0]); binary(CAR(list_val(p->reg[1])));
        } else { wrapper(p,13); CHECK(p->collections); }
        d->system=system; d->limit=limit; lb_alloc_domain_begin_operation(d); lb_process_destroy(p);
        CHECK(d->current==baseline && d->block_count==blocks);
    }
    for(int gc=0;gc<2;++gc) {
        uint64_t required=0;
        for(uint64_t pass=0;pass<=required+1;++pass) {
            LbProcess *p=create(w,m,2,gc); LbWorkBudget *budget=lb_work_budget_create(w); CHECK(budget);
            uint64_t credit=pass?pass-1:1000000; budget->remaining=budget->limit=credit; lb_work_domain_borrow(d,budget);
            CHECK(is_non_value(lb_bif_error_2(p,p->reg,NULL)));
            if(!pass) { wrapper(p,13); required=credit-budget->remaining; CHECK(required>1 && required<100000); }
            else if(credit<required) CHECK(p->status==LB_PROCESS_WORK_LIMIT && p->freason==LB_EXC_NULL && p->fvalue==NIL);
            else { wrapper(p,13); CHECK(!budget->remaining); }
            binary(p->reg[0]); binary(CAR(list_val(p->reg[1])));
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
    size_t baseline=w->space->domain->current; semantics(w,m); prefixes(w,m); CHECK(w->space->domain->current==baseline); lb_engine_leave(engine);
    LbReclamation *r; CHECK(lb_world_stop(w,&r)==LB_WORLD_OK && lb_reclamation_poll(r)==LB_WORLD_OK && lb_reclamation_consume(r)==LB_WORLD_OK); lb_reclamation_release(r); lb_world_release(w);
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine);
    puts("CORE_ERROR2_OK actual_error=true wrapper=true arbitrary_args=true terminal_reason=true down_trace=true moving_gc=true heap_credit=true allocation_work_prefixes=true balanced=true unwind=false"); return 0;
}
