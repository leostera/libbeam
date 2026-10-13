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
typedef enum { PROPER, IMPROPER, TUPLE_HEAD, TUPLE_TAIL, EMPTY, NONLIST, KIND_COUNT } Kind;
typedef enum { HEAD, TAIL, GUARD_HEAD, GUARD_TAIL, GUARD_HEAD_Y, GUARD_TAIL_Y, OP_COUNT } Operation;
static const char *names[]={"head","tail","guarded_head","guarded_tail","guarded_head_y","guarded_tail_y"};
static Eterm atom(LbWorld *w,const char *name)
{ Eterm value; CHECK(lb_atoms_find(w->space->atoms,name,strlen(name),&value)==LB_ATOM_OK); return value; }
static LbProcess *create(LbWorld *w,LbCodeModule *module,Operation operation,Kind kind)
{
    LbCodeEntry *entry; LbProcess *p; Eterm args[]={make_small(9),atom(w,"yes")};
    size_t arity=operation>=GUARD_HEAD_Y?2:1;
    CHECK(lb_code_entry_acquire(w->space,module->name,atom(w,names[operation]),arity,&entry)==LB_CODE_OK);
    CHECK(lb_process_create(entry,args,arity,512,&p)==LB_PROCESS_READY); lb_code_entry_release(entry);
    if(kind==EMPTY) p->reg[0]=NIL;
    else if(kind!=NONLIST) {
        Eterm head=make_small(7),tail=kind==PROPER?NIL:atom(w,"done");
        if(kind==TUPLE_HEAD || kind==TUPLE_TAIL) {
            Eterm tuple=TUPLE1(p->htop,make_small(42)); p->htop+=2;
            if(kind==TUPLE_HEAD) head=tuple; else tail=tuple;
        }
        p->htop[0]=head; p->htop[1]=tail; p->reg[0]=make_list(p->htop); p->htop+=2;
    }
    return p;
}
static LbProcessStatus run(LbProcess *p)
{
    LbProcessStatus status=LB_PROCESS_READY;
    for(size_t i=0;i<1000 && (status==LB_PROCESS_READY || status==LB_PROCESS_YIELDED);++i) status=lb_process_run(p,1,100);
    return status;
}
static void outcome(LbWorld *w,LbProcess *p,Operation op,Kind kind)
{
    if(op<=TAIL && kind>=EMPTY) { CHECK(p->status==LB_PROCESS_EXCEPTION && p->exception_value==am_badarg); return; }
    CHECK(p->status==LB_PROCESS_DONE); Eterm value=lb_process_result(p);
    if(op>=GUARD_HEAD) {
        int head=op==GUARD_HEAD || op==GUARD_HEAD_Y;
        int yes=head?(kind<EMPTY && kind!=TUPLE_HEAD):(kind==IMPROPER || kind==TUPLE_HEAD);
        CHECK(value==atom(w,yes?"yes":"no"));
    } else if((op==HEAD && kind==TUPLE_HEAD) || (op==TAIL && kind==TUPLE_TAIL)) {
        CHECK(is_tuple(value) && arityval(*tuple_val(value))==1 && tuple_val(value)[1]==make_small(42));
    } else CHECK(value==(op==HEAD?make_small(7):kind==PROPER?NIL:atom(w,"done")));
}
static void *deny(void *context,size_t bytes) { (void)context; (void)bytes; return NULL; }
static void vectors(LbWorld *w,LbCodeModule *module)
{
    LbAllocDomain *domain=w->space->domain; size_t baseline=domain->current;
    for(Operation op=HEAD;op<OP_COUNT;++op) for(Kind kind=PROPER;kind<KIND_COUNT;++kind) {
        LbProcess *p=create(w,module,op,kind); Eterm source=p->reg[0],head=NIL,tail=NIL;
        if(is_list(source)) { head=list_val(source)[0]; tail=list_val(source)[1]; }
        CHECK(run(p)==(op<=TAIL && kind>=EMPTY?LB_PROCESS_EXCEPTION:LB_PROCESS_DONE)); outcome(w,p,op,kind);
        if(is_list(source)) {
            CHECK(!p->collections && list_val(source)[0]==head && list_val(source)[1]==tail);
            if(op<=TAIL) CHECK(lb_process_result(p)==(op==HEAD?head:tail));
        }
        if(op<=TAIL && (kind==TUPLE_HEAD || kind==TUPLE_TAIL)) {
            CHECK(lb_process_collect(p,0)==LB_PROCESS_DONE && p->collections>0); outcome(w,p,op,kind);
        }
        lb_process_destroy(p); CHECK(domain->current==baseline);
        if(op<=TAIL && kind>=EMPTY) continue;
        /* Successful projection and failed guards need no spare allocation
         * credit or allocator. Keep the VM's mandatory return reserve. */
        for(unsigned mode=0;mode<2;++mode) {
            p=create(w,module,op,kind);
            if(op>=GUARD_HEAD_Y) {
                /* Call setup may allocate; inject only at the actual generic
                 * Y-source guard BIF, not at its caller's frame allocation. */
                for(unsigned step=0;step<100 && p->i[0]!=op_i_bif1_sfbd;++step)
                    CHECK(lb_process_run(p,1,100)==LB_PROCESS_YIELDED);
                CHECK(p->i[0]==op_i_bif1_sfbd);
            }
            while(p->htop<p->stop-S_RESERVED) *p->htop++=NIL;
            size_t live=domain->current,limit=domain->limit; LbSystemAllocator system=domain->system;
            if(mode) domain->system.allocate=deny; else domain->limit=live;
            LbProcessStatus status=run(p);
            if(status!=LB_PROCESS_DONE || p->collections || domain->current!=live)
                fprintf(stderr,"operation=%u kind=%u mode=%u status=%d collections=%zu live=%zu current=%zu\n",op,kind,mode,status,p->collections,live,domain->current);
            CHECK(status==LB_PROCESS_DONE && !p->collections && domain->current==live);
            domain->limit=limit; domain->system=system; outcome(w,p,op,kind);
            lb_process_destroy(p); CHECK(domain->current==baseline);
        }
    }
}
static void work_boundaries(LbWorld *w,LbCodeModule *module)
{
    LbAllocDomain *domain=w->space->domain; size_t baseline=domain->current;
    for(Operation op=HEAD;op<OP_COUNT;++op) for(Kind kind=PROPER;kind<KIND_COUNT;++kind) {
        uint64_t required=0;
        LbProcessStatus expected=op<=TAIL && kind>=EMPTY?LB_PROCESS_EXCEPTION:LB_PROCESS_DONE;
        for(uint64_t pass=0;pass<=required+1;++pass) {
            LbProcess *p=create(w,module,op,kind); LbWorkBudget *budget=lb_work_budget_create(w); CHECK(budget);
            uint64_t credit=pass?pass-1:1000000; budget->limit=budget->remaining=credit; lb_work_domain_borrow(domain,budget);
            LbProcessStatus status=run(p);
            if(!pass) { CHECK(status==expected); required=credit-budget->remaining; CHECK(required>1 && required<10000); }
            else if(credit==required) CHECK(status==expected && budget->remaining==0);
            else CHECK(status==LB_PROCESS_WORK_LIMIT);
            if(status==expected) outcome(w,p,op,kind);
            lb_work_domain_borrow(domain,NULL); lb_work_domain_clear_failure(domain); lb_alloc_domain_begin_operation(domain);
            lb_process_destroy(p); lb_work_budget_release(budget); CHECK(domain->current==baseline);
        }
    }
}
int main(int argc,char **argv)
{
    FILE *file; long length; CHECK(argc==2 && (file=fopen(argv[1],"rb")) && !fseek(file,0,SEEK_END));
    length=ftell(file); CHECK(length>0 && !fseek(file,0,SEEK_SET)); unsigned char *bytes=malloc((size_t)length);
    CHECK(bytes && fread(bytes,1,(size_t)length,file)==(size_t)length && !fclose(file));
    LbEngine *engine; LbWorld *world; LbCodeModule *module; LbBeamError error;
    CHECK(lb_engine_create(NULL,&engine)==LB_ENGINE_OK && lb_world_create(engine,&world)==LB_WORLD_OK); lb_engine_enter(engine);
    LbCodeStatus loaded=lb_code_load(world->space,bytes,(size_t)length,&module,&error);
    if(loaded!=LB_CODE_OK) fprintf(stderr,"load=%d stage=%s offset=%zu\n",loaded,error.stage,error.offset);
    CHECK(loaded==LB_CODE_OK); free(bytes); vectors(world,module); work_boundaries(world,module);
    lb_engine_leave(engine); LbReclamation *reclamation;
    CHECK(lb_world_stop(world,&reclamation)==LB_WORLD_OK && lb_reclamation_poll(reclamation)==LB_WORLD_OK);
    CHECK(lb_reclamation_consume(reclamation)==LB_WORLD_OK); lb_reclamation_release(reclamation); lb_world_release(world);
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine);
    puts("CORE_LIST_PARTS_OK head_tail=true proper_improper=true badarg=true guards=true source_unchanged=true result_gc=true allocation_free=true work_prefixes=true balanced=true");
    return 0;
}
