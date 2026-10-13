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
typedef enum { FRESH, COLLIDE, REPLACE, IMMEDIATE, ERASE, GET, MISSING } Operation;
static LbProcess *create(LbWorld *w,LbCodeModule *m)
{
    Eterm function; LbCodeEntry *entry; LbProcess *p; unsigned char input[100]; memset(input,0x7b,sizeof(input));
    CHECK(lb_atoms_find(w->space->atoms,"identity",8,&function)==LB_ATOM_OK);
    CHECK(lb_code_entry_acquire(w->space,m->name,function,1,&entry)==LB_CODE_OK);
    CHECK(lb_process_create_binary(entry,input,sizeof(input),1024,&p)==LB_PROCESS_READY); lb_code_entry_release(entry); CHECK(!p->dictionary); return p;
}
static Eterm get(LbProcess *p,Sint key)
{ p->reg[0]=make_small(key); return lb_bif_get_1(p,p->reg,NULL); }
static Eterm put(LbProcess *p,Sint key,Eterm value)
{ p->reg[0]=make_small(key); p->reg[1]=value; return lb_bif_put_2(p,p->reg,NULL); }
static Eterm erase(LbProcess *p,Sint key)
{ p->reg[0]=make_small(key); return lb_bif_erase_1(p,p->reg,NULL); }
static void binary(Eterm term)
{ LbBitstringView view; CHECK(lb_bitstring_view(term,&view) && view.bit_size==800 && view.data[0]==0x7b && view.data[99]==0x7b); }
static void prepare(LbProcess *p,Operation op)
{
    Eterm input=p->reg[0];
    if(op!=FRESH) { CHECK(put(p,1,input)==am_undefined); CHECK(put(p,9,make_small(9))==am_undefined); }
    p->reg[0]=make_small(op==FRESH?1:op==COLLIDE?17:op==MISSING?99:9);
    p->reg[1]=op==IMMEDIATE?am_ok:input;
    p->heap_reserved=13;
    while(p->htop<p->stop-S_RESERVED-p->heap_reserved) *p->htop++=NIL;
}
static Eterm operation(LbProcess *p,Operation op)
{
    if(op==ERASE) return lb_bif_erase_1(p,p->reg,NULL);
    if(op==GET || op==MISSING) return lb_bif_get_1(p,p->reg,NULL);
    return lb_bif_put_2(p,p->reg,NULL);
}
static void state(LbProcess *p,Operation op,int success)
{
    CHECK(p->heap_reserved==13);
    if(op==FRESH && !success) { CHECK(!p->dictionary); return; }
    CHECK(p->dictionary && p->dictionary->numElements==(op==FRESH?1:success && op==COLLIDE?3:success && op==ERASE?1:2));
    binary(get(p,1));
    if(op==FRESH) return;
    Eterm nine=get(p,9);
    if(success && op==REPLACE) binary(nine);
    else CHECK(nine==(success && op==IMMEDIATE?am_ok:success && op==ERASE?am_undefined:make_small(9)));
    if(success && op==COLLIDE) binary(get(p,17));
}
typedef struct { LbSystemAllocator backing; size_t remaining; } Prefix;
static void *allocate(void *ctx,size_t size)
{ Prefix *p=ctx; if(!p->remaining) return NULL; --p->remaining; return p->backing.allocate(p->backing.context,size); }
static void release(void *ctx,void *ptr) { Prefix *p=ctx; p->backing.release(p->backing.context,ptr); }
static void prefixes(LbWorld *w,LbCodeModule *m,Operation op)
{
    LbAllocDomain *d=w->space->domain; size_t baseline=d->current,blocks=d->block_count; int complete=0;
    for(size_t prefix=0;prefix<20 && !complete;++prefix) {
        LbProcess *p=create(w,m); prepare(p,op); Prefix fault={d->system,prefix?prefix-1:0}; size_t limit=d->limit;
        if(prefix) d->system=(LbSystemAllocator){&fault,allocate,release}; else d->limit=d->current;
        Eterm result=operation(p,op); complete=!is_non_value(result);
        d->system=fault.backing; d->limit=limit;
        if(!complete) CHECK(p->status==(prefix?LB_PROCESS_NO_MEMORY:LB_PROCESS_LIMIT));
        else CHECK(result==(op==FRESH || op==COLLIDE || op==MISSING?am_undefined:make_small(9)));
        lb_alloc_domain_begin_operation(d); p->status=LB_PROCESS_READY; state(p,op,complete); lb_process_destroy(p);
        CHECK(d->current==baseline && d->block_count==blocks);
    }
    CHECK(complete);
    uint64_t required=0;
    for(uint64_t pass=0;pass<=required+1;++pass) {
        LbProcess *p=create(w,m); prepare(p,op); LbWorkBudget *budget=lb_work_budget_create(w); CHECK(budget);
        uint64_t credit=pass?pass-1:1000000; budget->remaining=budget->limit=credit; lb_work_domain_borrow(d,budget);
        Eterm result=operation(p,op); int success=!is_non_value(result);
        if(!pass) { CHECK(success); required=credit-budget->remaining; CHECK(required>0 && required<100000); }
        else if(credit<required) CHECK(!success && p->status==LB_PROCESS_WORK_LIMIT);
        else CHECK(success && !budget->remaining);
        lb_work_domain_borrow(d,NULL); lb_work_domain_clear_failure(d); lb_alloc_domain_begin_operation(d); p->status=LB_PROCESS_READY;
        state(p,op,success); lb_process_destroy(p); lb_work_budget_release(budget);
        CHECK(d->current==baseline && d->block_count==blocks);
    }
}
static void roots(LbWorld *w,LbCodeModule *m)
{
    LbProcess *p=create(w,m),*peer=create(w,m); Eterm input=p->reg[0];
    CHECK(put(p,1,input)==am_undefined && get(peer,1)==am_undefined);
    CHECK(put(p,9,make_small(9))==am_undefined && put(p,17,am_ok)==am_undefined);
    for(unsigned i=0;i<MAX_REG;++i) p->reg[i]=NIL;
    CHECK(lb_process_collect(p,0)==LB_PROCESS_READY); binary(get(p,1));
    p->reg[1]=get(p,1); CHECK(lb_process_has_other_reference(p,p->reg[1],&p->reg[1]));
    CHECK(erase(p,9)==make_small(9)); binary(erase(p,1));
    CHECK(is_tuple(p->dictionary->data[1]) && erase(p,17)==am_ok && !p->dictionary->numElements && is_nil(p->dictionary->data[1]));
    CHECK(put(p,-1,am_undefined)==am_undefined && p->dictionary->numElements==1 && erase(p,-1)==am_undefined && !p->dictionary->numElements);
    p->reg[0]=am_ok; p->reg[1]=am_true; CHECK(lb_bif_put_2(p,p->reg,NULL)==am_undefined && lb_bif_get_1(p,p->reg,NULL)==am_true);
    Eterm *top=p->htop; size_t count=p->dictionary->numElements;
    for(unsigned op=0;op<3;++op) {
        p->reg[0]=NIL; p->status=LB_PROCESS_READY;
        Eterm result=op==0?lb_bif_get_1(p,p->reg,NULL):op==1?lb_bif_put_2(p,p->reg,NULL):lb_bif_erase_1(p,p->reg,NULL);
        CHECK(is_non_value(result) && p->status==LB_PROCESS_REFUSED && p->dictionary->numElements==count && p->htop==top);
    }
    lb_process_destroy(peer); lb_process_destroy(p);
}
static LbCodeModule *load(LbWorld *w,const char *path)
{
    FILE *f=fopen(path,"rb"); CHECK(f && !fseek(f,0,SEEK_END)); long size=ftell(f); CHECK(size>0 && !fseek(f,0,SEEK_SET));
    unsigned char *bytes=malloc((size_t)size); CHECK(bytes && fread(bytes,1,(size_t)size,f)==(size_t)size && !fclose(f));
    LbCodeModule *m; LbBeamError error; CHECK(lb_code_load(w->space,bytes,(size_t)size,&m,&error)==LB_CODE_OK); free(bytes); return m;
}
static void locations(LbWorld *w,LbCodeModule *m)
{
    const char *names[]={"query","query_y"};
    for(unsigned i=0;i<2;++i) {
        Eterm name,key=make_small(9); LbCodeEntry *entry; LbProcess *p;
        CHECK(lb_atoms_find(w->space->atoms,names[i],strlen(names[i]),&name)==LB_ATOM_OK);
        CHECK(lb_code_entry_acquire(w->space,m->name,name,1,&entry)==LB_CODE_OK);
        CHECK(lb_process_create(entry,&key,1,512,&p)==LB_PROCESS_READY); lb_code_entry_release(entry);
        CHECK(put(p,9,make_small(42))==am_undefined);
        LbProcessStatus status=LB_PROCESS_READY;
        for(unsigned j=0;j<100 && (status==LB_PROCESS_READY || status==LB_PROCESS_YIELDED);++j) status=lb_process_run(p,1,100);
        CHECK(status==LB_PROCESS_DONE && lb_process_result(p)==make_small(42)); lb_process_destroy(p);
    }
}
static void code_roots(LbWorld *w,LbCodeModule *host,LbCodeModule *target)
{
    LbProcess *p=create(w,host); Eterm name; Uint cost=0;
    CHECK(lb_atoms_find(w->space->atoms,"one",3,&name)==LB_ATOM_OK);
    Export *entry=lb_export_find(w->space,target->name,name,1); CHECK(entry);
    CHECK(lb_fun_retain_export(w->space,&p->fun_leases,entry,&cost)==LB_ALLOC_OK);
    LbFun *fun=(LbFun *)p->htop; fun->thing_word=LB_FUN_HEADER(1,0)|((Uint)1<<LB_FUN_KIND_SHIFT); fun->entry.exp=entry; p->htop+=LB_FUN_WORDS;
    CHECK(put(p,1,make_boxed((Eterm *)fun))==am_undefined);
    for(unsigned i=0;i<MAX_REG;++i) p->reg[i]=NIL;
    CHECK(lb_process_collect(p,0)==LB_PROCESS_READY && lb_code_unload(target)==LB_CODE_BUSY);
    CHECK(lb_fun(get(p,1))->entry.exp==entry);
    CHECK(lb_fun(erase(p,1))!=NULL); /* Discard returned fun before the next GC. */
    CHECK(lb_process_collect(p,0)==LB_PROCESS_READY && !p->fun_leases && lb_code_unload(target)==LB_CODE_OK);
    lb_process_destroy(p);
}
int main(int argc,char **argv)
{
    CHECK(argc==4); FILE *f=fopen(argv[1],"rb"); CHECK(f && !fseek(f,0,SEEK_END)); long size=ftell(f); CHECK(size>0 && !fseek(f,0,SEEK_SET));
    unsigned char *bytes=malloc((size_t)size); CHECK(bytes && fread(bytes,1,(size_t)size,f)==(size_t)size && !fclose(f));
    LbEngine *engine; LbWorld *w; CHECK(lb_engine_create(NULL,&engine)==LB_ENGINE_OK && lb_world_create(engine,&w)==LB_WORLD_OK);
    lb_engine_enter(engine); LbCodeModule *m; LbBeamError error; LbCodeStatus loaded=lb_code_load(w->space,bytes,(size_t)size,&m,&error);
    if(loaded!=LB_CODE_OK) fprintf(stderr,"dict load=%d stage=%s\n",loaded,error.stage?error.stage:"unknown"); CHECK(loaded==LB_CODE_OK); free(bytes);
    LbCodeModule *location_module=load(w,argv[2]),*target=load(w,argv[3]);
    locations(w,location_module); code_roots(w,m,target);
    size_t baseline=w->space->domain->current; roots(w,m);
    for(Operation op=FRESH;op<=MISSING;++op) prefixes(w,m,op);
    CHECK(w->space->domain->current==baseline); lb_engine_leave(engine);
    LbReclamation *r; CHECK(lb_world_stop(w,&r)==LB_WORLD_OK && lb_reclamation_poll(r)==LB_WORLD_OK && lb_reclamation_consume(r)==LB_WORLD_OK); lb_reclamation_release(r); lb_world_release(w);
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine);
    puts("CORE_DICT_OK native_buckets=true collisions=true put_get_erase=true gc_roots=true peer_empty=true undefined_value=true key_refusal=true allocation_work_prefixes=true mutation_atomic=true balanced=true"); return 0;
}
