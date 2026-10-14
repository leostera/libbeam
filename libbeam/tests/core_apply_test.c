/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#include "apply.h"
#include "fun.h"
#include "world_internal.h"
#include "work_budget_internal.h"
#include "alloc_internal.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while(0)
typedef struct { size_t live,bytes; } Memory;
typedef union { max_align_t alignment; size_t size; } MemoryHeader;
static void *owned_allocate(void *context,size_t size)
{
    Memory *m=context; if(size>SIZE_MAX-sizeof(MemoryHeader)) return NULL;
    MemoryHeader *h=malloc(sizeof(*h)+size); if(!h) return NULL;
    h->size=size; ++m->live; m->bytes+=size; return h+1;
}
static void owned_release(void *context,void *pointer)
{
    Memory *m=context; MemoryHeader *h=(MemoryHeader *)pointer-1;
    CHECK(m->live && m->bytes>=h->size); --m->live; m->bytes-=h->size;
    memset(pointer,0xa5,h->size); free(h);
}
static Eterm atom(LbWorld *w,const char *name)
{
    Eterm value; if(lb_atoms_find(w->space->atoms,name,strlen(name),&value)==LB_ATOM_OK) return value;
    CHECK(lb_atoms_intern(w->space->atoms,name,strlen(name),&value)==LB_ATOM_OK); return value;
}
static LbCodeModule *load(LbWorld *w,const char *path)
{
    FILE *f=fopen(path,"rb"); CHECK(f && !fseek(f,0,SEEK_END)); long size=ftell(f); CHECK(size>0 && !fseek(f,0,SEEK_SET));
    unsigned char *bytes=malloc((size_t)size); CHECK(bytes && fread(bytes,1,(size_t)size,f)==(size_t)size && !fclose(f));
    LbCodeModule *module; LbBeamError error; LbCodeStatus status=lb_code_load(w->space,bytes,(size_t)size,&module,&error);
    if(status!=LB_CODE_OK) fprintf(stderr,"load=%d stage=%s offset=%zu\n",status,error.stage,error.offset);
    CHECK(status==LB_CODE_OK); free(bytes); return module;
}
static LbProcess *create_at(LbWorld *w,LbCodeModule *host,const char *function)
{
    LbCodeEntry *entry; LbProcess *p; Eterm input=NIL;
    CHECK(lb_code_entry_acquire(w->space,host->name,atom(w,function),1,&entry)==LB_CODE_OK);
    CHECK(lb_process_create(entry,&input,1,4096,&p)==LB_PROCESS_READY); lb_code_entry_release(entry); return p;
}
static LbProcess *create(LbWorld *w,LbCodeModule *host) { return create_at(w,host,"identity"); }
static Eterm list(LbProcess *p,const Eterm *values,size_t count,Eterm tail)
{
    CHECK((size_t)(p->stop-p->htop)>2*count+S_RESERVED);
    while(count) { tail=CONS(p->htop,values[--count],tail); p->htop+=2; } return tail;
}
static void setup(LbProcess *p,Eterm module,Eterm function,const Eterm *values,size_t count)
{ p->reg[0]=module; p->reg[1]=function; p->reg[2]=list(p,values,count,NIL); }
static void fill(LbProcess *p)
{ while(p->htop<p->stop-S_RESERVED-p->heap_reserved) *p->htop++=NIL; }
static void semantics(LbWorld *w,LbCodeModule *host,LbCodeModule *target)
{
    Eterm one=atom(w,"one"),pair=atom(w,"pair"),zero=atom(w,"zero"),values[]={make_small(41),make_small(42)};
    for(unsigned nested=0;nested<4;++nested) {
        LbProcess *p=create(w,host); setup(p,target->name,pair,values,2);
        for(unsigned i=0;i<nested;++i) { Eterm triple[]={p->reg[0],p->reg[1],p->reg[2]}; setup(p,am_erlang,am_apply,triple,3); }
        const Export *entry=lb_apply(p); CHECK(entry && entry->owner==target && p->reg[0]==values[0] && p->reg[1]==values[1]);
        CHECK(target->users==1 && p->fun_leases); p->i=entry->dispatch.addresses[0];
        CHECK(lb_process_collect(p,0)==LB_PROCESS_READY && target->users==1);
        CHECK(lb_process_run(p,100,1000)==LB_PROCESS_DONE);
        Eterm result=lb_process_result(p); CHECK(is_tuple(result) && tuple_val(result)[1]==values[0] && tuple_val(result)[2]==values[1]);
        lb_process_destroy(p); CHECK(!target->users);
    }
    LbProcess *p=create(w,host); setup(p,target->name,zero,NULL,0);
    CHECK(lb_apply(p)); lb_process_destroy(p);
    p=create(w,host); p->reg[0]=values[0]; p->reg[1]=target->name; p->reg[2]=one;
    CHECK(lb_fixed_apply(p,1) && p->reg[0]==values[0]); lb_process_destroy(p);
    p=create(w,host); setup(p,target->name,one,values,1); p->reg[3]=am_erlang; p->reg[4]=am_apply;
    CHECK(lb_fixed_apply(p,3) && p->reg[0]==values[0]); lb_process_destroy(p);
    /* Malformed arguments and native MAX_ARG precedence; no argument registers
     * are partially overwritten during list parsing. */
    for(unsigned kind=0;kind<6;++kind) {
        p=create(w,host); setup(p,target->name,one,values,1);
        if(kind==0) p->reg[0]=NIL;
        if(kind==1) p->reg[1]=make_small(0);
        if(kind==2) p->reg[2]=make_small(0);
        if(kind==3) p->reg[2]=list(p,values,1,am_true);
        if(kind>=4) { Eterm many[256]; for(size_t i=0;i<256;++i) many[i]=make_small(i); p->reg[2]=list(p,many,kind==4?255:256,am_true); }
        Eterm before[]={p->reg[0],p->reg[1],p->reg[2]};
        CHECK(!lb_apply(p) && p->freason==(kind==5?LB_EXC_SYSTEM_LIMIT:LB_EXC_BADARG));
        CHECK(!memcmp(before,p->reg,sizeof(before)) && !p->fun_leases); lb_process_destroy(p);
    }
    p=create(w,host); Eterm bad[]={NIL,one,NIL}; setup(p,am_erlang,am_apply,bad,3);
    CHECK(!lb_apply(p) && p->freason==LB_EXC_BADARG && p->reg[0]==NIL && p->reg[1]==one && p->reg[2]==NIL); lb_process_destroy(p);
    p=create(w,host); Eterm many[255]; for(size_t i=0;i<255;++i) many[i]=NIL;
    setup(p,target->name,one,many,255); CHECK(!lb_apply(p) && p->status==LB_PROCESS_REFUSED); lb_process_destroy(p);
    p=create(w,host); setup(p,am_erlang,am_abs,values,1);
    const Export *native=lb_apply(p); CHECK(native && !native->owner && !p->fun_leases); lb_process_destroy(p);
}
static void fixed_error(LbWorld *w,LbCodeModule *host)
{
    LbProcess *p=create(w,host); Eterm *hp=p->htop;
    p->reg[0]=TUPLE1(hp,make_small(17)); p->htop+=2;
    hp=p->htop; p->reg[1]=TUPLE1(hp,make_small(18)); p->htop+=2;
    p->reg[2]=NIL; p->heap_reserved=11; fill(p);
    CHECK(!lb_fixed_apply(p,1) && p->freason==LB_EXC_BADARG && p->collections==1 && p->heap_reserved==11);
    CHECK(is_tuple(p->reg[0]) && tuple_val(p->reg[0])[1]==make_small(18) && p->reg[1]==NIL);
    CHECK(is_list(p->reg[2]) && is_tuple(list_val(p->reg[2])[0]) && tuple_val(list_val(p->reg[2])[0])[1]==make_small(17));
    CHECK(list_val(p->reg[2])[1]==NIL); lb_process_destroy(p);
}
static void lifetime(LbWorld *w,LbCodeModule *host,LbCodeModule *target)
{
    LbProcess *p=create(w,host); Eterm value=make_small(1); const BeamInstr *initial=p->i; Eterm cp=*p->stop;
    setup(p,target->name,atom(w,"one"),&value,1); const Export *entry=lb_apply(p); CHECK(entry);
    p->i=entry->dispatch.addresses[0]; CHECK(lb_process_collect(p,0)==LB_PROCESS_READY && target->users==1);
    p->i=initial; *p->stop=(Eterm)entry->dispatch.addresses[0];
    CHECK(lb_process_collect(p,0)==LB_PROCESS_READY && target->users==1);
    *p->stop=cp; CHECK(lb_process_collect(p,0)==LB_PROCESS_READY && !target->users && !p->fun_leases);
    lb_process_destroy(p);
}
typedef struct { LbSystemAllocator backing; size_t left; } Prefix;
static void *allocate(void *context,size_t size)
{ Prefix *p=context; if(!p->left) return NULL; --p->left; return p->backing.allocate(p->backing.context,size); }
static void release(void *context,void *ptr)
{ Prefix *p=context; p->backing.release(p->backing.context,ptr); }
static void boundaries(LbWorld *w,LbCodeModule *host,LbCodeModule *target)
{
    LbAllocDomain *d=w->space->domain; Eterm function=atom(w,"one"),value=make_small(1); size_t baseline=d->current;
    for(unsigned fixed=0;fixed<2;++fixed) {
        int done=0;
        for(size_t prefix=0;prefix<20 && !done;++prefix) {
            LbProcess *p=create(w,host);
            if(fixed) { p->reg[0]=value; p->reg[1]=NIL; p->reg[2]=function; fill(p); }
            else setup(p,target->name,function,&value,1);
            Eterm before[]={p->reg[0],p->reg[1],p->reg[2]}; Eterm *heap=p->htop;
            size_t limit=d->limit,live=d->current; Prefix state={d->system,prefix?prefix-1:0};
            if(!prefix) d->limit=live; else d->system=(LbSystemAllocator){&state,allocate,release};
            const Export *result=fixed?lb_fixed_apply(p,1):lb_apply(p);
            d->limit=limit; d->system=state.backing;
            if(p->status!=LB_PROCESS_READY) {
                CHECK(!result && p->status==(prefix?LB_PROCESS_NO_MEMORY:LB_PROCESS_LIMIT));
                CHECK(!memcmp(before,p->reg,sizeof(before)) && p->htop==heap && !p->collections && !p->fun_leases && d->current==live);
            } else { CHECK(prefix>1 && (fixed?p->freason==LB_EXC_BADARG:result!=NULL)); done=1; }
            lb_alloc_domain_begin_operation(d); lb_process_destroy(p); CHECK(d->current==baseline && !target->users);
        }
        CHECK(done);
        uint64_t required=0;
        for(uint64_t pass=0;pass<=required+1;++pass) {
            LbProcess *p=create(w,host);
            if(fixed) { p->reg[0]=value; p->reg[1]=NIL; p->reg[2]=function; fill(p); }
            else { setup(p,target->name,function,&value,1); Eterm triple[]={p->reg[0],p->reg[1],p->reg[2]}; setup(p,am_erlang,am_apply,triple,3); }
            Eterm before[]={p->reg[0],p->reg[1],p->reg[2]}; LbWorkBudget *budget=lb_work_budget_create(w); CHECK(budget);
            uint64_t credit=pass?pass-1:1000000; budget->remaining=budget->limit=credit; lb_work_domain_borrow(d,budget);
            const Export *result=fixed?lb_fixed_apply(p,1):lb_apply(p);
            if(!pass) { CHECK(p->status==LB_PROCESS_READY); required=credit-budget->remaining; CHECK(required>1 && required<20000); }
            else if(credit<required) CHECK(!result && p->status==LB_PROCESS_WORK_LIMIT && !memcmp(before,p->reg,sizeof(before)) && !p->fun_leases);
            else CHECK(p->status==LB_PROCESS_READY && !budget->remaining);
            lb_work_domain_borrow(d,NULL); lb_work_domain_clear_failure(d); lb_alloc_domain_begin_operation(d);
            lb_process_destroy(p); lb_work_budget_release(budget); CHECK(d->current==baseline && !target->users);
        }
    }
}
static LbProcessStatus run(LbProcess *p)
{
    LbProcessStatus status=LB_PROCESS_READY;
    for(size_t i=0;i<1000 && (status==LB_PROCESS_READY || status==LB_PROCESS_YIELDED);++i) status=lb_process_run(p,1,100);
    return status;
}
static LbProcess *invocation(LbWorld *w,LbCodeModule *host,LbCodeModule *target,const char *name,int invalid)
{
    LbProcess *p=create_at(w,host,name); Eterm value=TUPLE1(p->htop,make_small(42)); p->htop+=2;
    Eterm args=!strncmp(name,"fixed",5)?value:list(p,&value,1,NIL);
    p->reg[0]=TUPLE3(p->htop,invalid?NIL:target->name,atom(w,"one"),args); p->htop+=4;
    return p;
}
static void outcome(LbWorld *w,LbProcess *p,const char *name)
{
    Eterm result=lb_process_result(p);
    if(strstr(name,"call")) { CHECK(is_tuple(result) && arityval(*tuple_val(result))==2 && tuple_val(result)[1]==atom(w,"applied")); result=tuple_val(result)[2]; }
    CHECK(is_tuple(result) && arityval(*tuple_val(result))==1 && tuple_val(result)[1]==make_small(42));
}
static void execution(LbWorld *w,LbCodeModule *host,LbCodeModule *target)
{
    const char *names[]={"list","list_call","list_last","fixed","fixed_call","external"};
    const BeamInstr ops[]={op_i_apply_only,op_i_apply,op_i_apply_last_Q,op_apply_last_tQ,op_apply_t,op_i_apply_only};
    LbAllocDomain *d=w->space->domain;
    /* Intern fixture-only comparison atoms before taking the balance snapshot. */
    (void)atom(w,"applied"); size_t baseline=d->current;
    for(size_t n=0;n<6;++n) {
        LbProcess *p=invocation(w,host,target,names[n],0);
        for(unsigned i=0;i<100 && p->i[0]!=ops[n];++i) CHECK(lb_process_run(p,1,100)==LB_PROCESS_YIELDED);
        CHECK(p->i[0]==ops[n]);
        if(n==5) CHECK(p->i==w->space->apply_export.dispatch.addresses[0]);
        CHECK(run(p)==LB_PROCESS_DONE); outcome(w,p,names[n]);
        CHECK(lb_process_collect(p,0)==LB_PROCESS_DONE); outcome(w,p,names[n]); lb_process_destroy(p);
        for(unsigned gc=0;gc<2;++gc) {
            p=invocation(w,host,target,names[n],1);
            if(gc) {
                for(unsigned i=0;i<100 && p->i[0]!=ops[n];++i) CHECK(lb_process_run(p,1,100)==LB_PROCESS_YIELDED);
                CHECK(p->i[0]==ops[n]); fill(p);
            }
            CHECK(run(p)==LB_PROCESS_EXCEPTION && p->exception_value==am_badarg);
            if(gc && !strncmp(names[n],"fixed",5)) CHECK(p->collections>0);
            lb_process_destroy(p);
        }
        int allocated=0;
        for(size_t prefix=0;prefix<20 && !allocated;++prefix) {
            p=invocation(w,host,target,names[n],0);
            for(unsigned i=0;i<100 && p->i[0]!=ops[n];++i) CHECK(lb_process_run(p,1,100)==LB_PROCESS_YIELDED);
            CHECK(p->i[0]==ops[n]); size_t limit=d->limit,live=d->current;
            Prefix state={d->system,prefix?prefix-1:0};
            if(!prefix) d->limit=live; else d->system=(LbSystemAllocator){&state,allocate,release};
            LbProcessStatus status=run(p); d->limit=limit; d->system=state.backing;
            if(status==LB_PROCESS_DONE) { CHECK(prefix>1); outcome(w,p,names[n]); allocated=1; }
            else CHECK(status==(prefix?LB_PROCESS_NO_MEMORY:LB_PROCESS_LIMIT) && !target->users && !p->fun_leases && d->current==live);
            lb_alloc_domain_begin_operation(d); lb_process_destroy(p); CHECK(d->current==baseline);
        }
        CHECK(allocated);
        uint64_t required=0;
        for(uint64_t pass=0;pass<=required+1;++pass) {
            p=invocation(w,host,target,names[n],0); LbWorkBudget *budget=lb_work_budget_create(w); CHECK(budget);
            uint64_t credit=pass?pass-1:1000000; budget->remaining=budget->limit=credit; lb_work_domain_borrow(d,budget);
            LbProcessStatus status=run(p);
            if(!pass) { CHECK(status==LB_PROCESS_DONE); required=credit-budget->remaining; CHECK(required>1 && required<20000); }
            else if(credit<required) CHECK(status==LB_PROCESS_WORK_LIMIT);
            else CHECK(status==LB_PROCESS_DONE && !budget->remaining);
            if(status==LB_PROCESS_DONE) outcome(w,p,names[n]);
            lb_work_domain_borrow(d,NULL); lb_work_domain_clear_failure(d); lb_alloc_domain_begin_operation(d);
            lb_process_destroy(p); lb_work_budget_release(budget);
        }
        CHECK(d->current==baseline && !target->users);
    }
}
static Eterm external_fun(LbWorld *w,LbProcess *p,LbCodeModule *target)
{
    Export *entry=lb_export_find(w->space,target->name,atom(w,"one"),1); CHECK(entry);
    Uint cost=0; CHECK(lb_fun_retain_export(w->space,&p->fun_leases,entry,&cost)==LB_ALLOC_OK);
    LbFun *fun=(LbFun *)p->htop; fun->thing_word=LB_FUN_HEADER(1,0)|((Uint)1<<LB_FUN_KIND_SHIFT);
    fun->entry.exp=entry; p->htop+=LB_FUN_WORDS; return make_boxed((Eterm *)fun);
}
static void fun_error(LbProcess *p,size_t count)
{
    CHECK(p->freason==LB_EXC_BADARITY && is_tuple(p->fvalue));
    Eterm *payload=tuple_val(p->fvalue); CHECK(arityval(*payload)==2 && lb_fun(payload[1]));
    Eterm args=payload[2]; for(size_t i=0;i<count;++i) { CHECK(is_list(args) && CAR(list_val(args))==make_small(i)); args=CDR(list_val(args)); }
    CHECK(is_nil(args));
}
static void fun_components(LbWorld *w,LbCodeModule *host,LbCodeModule *target)
{
    LbAllocDomain *d=w->space->domain; size_t baseline=d->current;
    const size_t counts[]={0,1,2,255,256,1022,1023,1024}; Eterm values[1024];
    for(size_t i=0;i<1024;++i) values[i]=make_small(i);
    for(unsigned invalid=0;invalid<2;++invalid) for(unsigned improper=0;improper<2;++improper)
    for(size_t n=0;n<sizeof(counts)/sizeof(*counts);++n) {
        LbProcess *p=create(w,host); p->reg[0]=invalid?am_true:external_fun(w,p,target);
        p->reg[1]=list(p,values,counts[n],improper?am_true:NIL); Eterm before[]={p->reg[0],p->reg[1]};
        p->heap_reserved=7; fill(p); const BeamInstr *next=lb_apply_fun(p);
        if(counts[n]==1024 || improper) {
            CHECK(!next && p->freason==(counts[n]==1024?LB_EXC_SYSTEM_LIMIT:LB_EXC_BADARG));
            CHECK(!memcmp(before,p->reg,sizeof(before)) && !p->collections);
        } else if(invalid) CHECK(!next && p->freason==LB_EXC_BADFUN && p->fvalue==am_true);
        else if(counts[n]==1) CHECK(next && p->reg[0]==make_small(0) && !p->collections);
        else { CHECK(!next && p->collections && p->heap_reserved==7); fun_error(p,counts[n]); }
        lb_process_destroy(p); CHECK(d->current==baseline && !target->users);
    }
    {
        LbProcess *p=create(w,host); p->reg[0]=external_fun(w,p,target);
        Eterm value=TUPLE2(p->htop,p->reg[0],make_small(42)); p->htop+=3; p->construction_root=value;
        Eterm args[]={value,value}; p->reg[1]=list(p,args,2,NIL); fill(p);
        CHECK(!lb_apply_fun(p) && p->freason==LB_EXC_BADARITY && p->collections);
        for(unsigned pass=0;pass<2;++pass) {
            Eterm *payload=tuple_val(p->fvalue); Eterm tail=payload[2];
            CHECK(lb_fun(payload[1]) && target->users==1);
            for(unsigned i=0;i<2;++i) {
                CHECK(is_list(tail) && CAR(list_val(tail))==p->construction_root);
                Eterm *item=tuple_val(CAR(list_val(tail))); CHECK(item[1]==payload[1] && item[2]==make_small(42)); tail=CDR(list_val(tail));
            }
            CHECK(is_nil(tail)); if(!pass) CHECK(lb_process_collect(p,0)==LB_PROCESS_READY);
        }
        lb_process_destroy(p); CHECK(d->current==baseline && !target->users);
    }
    for(unsigned kind=0;kind<2;++kind) {
        uint64_t cost=0;
        for(uint64_t pass=0;pass<=cost+1;++pass) {
            LbProcess *p=create(w,host); p->reg[0]=external_fun(w,p,target); p->reg[1]=list(p,values,kind?2:1,NIL);
            if(kind) fill(p);
            LbWorkBudget *budget=lb_work_budget_create(w); CHECK(budget); uint64_t credit=pass?pass-1:1000000;
            budget->limit=budget->remaining=credit; lb_work_domain_borrow(d,budget);
            const BeamInstr *next=lb_apply_fun(p);
            if(!pass) { cost=credit-budget->remaining; CHECK(cost>1 && cost<20000); }
            if(!pass || credit==cost) { CHECK(p->status==LB_PROCESS_READY); if(kind) { CHECK(!next); fun_error(p,2); } else CHECK(next); if(pass) CHECK(!budget->remaining); }
            else CHECK(!next && p->status==LB_PROCESS_WORK_LIMIT && p->freason==LB_EXC_NULL);
            lb_work_domain_borrow(d,NULL); lb_work_domain_clear_failure(d); lb_alloc_domain_begin_operation(d);
            lb_process_destroy(p); lb_work_budget_release(budget); CHECK(d->current==baseline && !target->users);
        }
    }
    int done=0;
    for(size_t prefix=0;prefix<20 && !done;++prefix) {
        LbProcess *p=create(w,host); p->reg[0]=external_fun(w,p,target); p->reg[1]=list(p,values,2,NIL); fill(p);
        Eterm *heap=p->heap,*top=p->htop; size_t limit=d->limit,live=d->current;
        Prefix state={d->system,prefix?prefix-1:0}; if(!prefix) d->limit=live; else d->system=(LbSystemAllocator){&state,allocate,release};
        CHECK(!lb_apply_fun(p)); d->limit=limit; d->system=state.backing;
        if(p->status==LB_PROCESS_READY) { fun_error(p,2); CHECK(p->collections); done=1; }
        else CHECK(p->status==(prefix?LB_PROCESS_NO_MEMORY:LB_PROCESS_LIMIT) && p->freason==LB_EXC_NULL && p->heap==heap && p->htop==top && !p->collections && d->current==live);
        lb_alloc_domain_begin_operation(d); lb_process_destroy(p); CHECK(d->current==baseline && !target->users);
    }
    CHECK(done);
    const char *names[]={"fun_list","fun_call","fun_last","fun_external","fun_dynamic"};
    const BeamInstr ops[]={op_i_apply_fun_only,op_i_apply_fun,op_i_apply_fun_last_Q,op_i_apply_fun_only,op_i_apply_fun_only};
    for(size_t n=0;n<5;++n) {
        uint64_t cost=0;
        for(unsigned mode=0;mode<4;++mode) {
            LbProcess *p=create_at(w,host,names[n]); Eterm fun=external_fun(w,p,target),value=TUPLE1(p->htop,make_small(42)); p->htop+=2;
            Eterm args=list(p,&value,1,NIL); p->reg[0]=TUPLE2(p->htop,fun,args); p->htop+=3;
            LbWorkBudget *budget=lb_work_budget_create(w); CHECK(budget); uint64_t credit=!mode?1000000:mode==1?cost:mode==2?cost-1:0;
            budget->limit=budget->remaining=credit; lb_work_domain_borrow(d,budget);
            if(!mode) {
                for(unsigned step=0;step<100 && *p->i!=ops[n];++step) CHECK(lb_process_run(p,1,100)==LB_PROCESS_YIELDED);
                CHECK(*p->i==ops[n]); if(n>=3) CHECK(p->i==w->space->apply_fun_export.dispatch.addresses[0]);
            }
            LbProcessStatus status=run(p);
            if(mode<2) { CHECK(status==LB_PROCESS_DONE); outcome(w,p,names[n]); if(!mode) { cost=credit-budget->remaining; CHECK(cost>1); } else CHECK(!budget->remaining); }
            else CHECK(status==LB_PROCESS_WORK_LIMIT);
            lb_work_domain_borrow(d,NULL); lb_work_domain_clear_failure(d); lb_alloc_domain_begin_operation(d);
            if(mode<2) { CHECK(lb_process_collect(p,0)==LB_PROCESS_DONE); outcome(w,p,names[n]); }
            lb_process_destroy(p); lb_work_budget_release(budget); CHECK(d->current==baseline && !target->users);
        }
    }
    LbProcess *denied=create_at(w,host,"fun_denied"); CHECK(run(denied)==LB_PROCESS_CAPABILITY_DENIED);
    CHECK(denied->capability_failure && !strcmp(denied->capability_failure->function,"display") && denied->capability_failure->arity==1);
    lb_process_destroy(denied); CHECK(d->current==baseline);
    puts("CORE_APPLY_FUN_OK argument_limits=true badfun=true moving_badarity=true precedence=true opcode_variants=true native_export=true work_prefixes=true allocation_prefixes=true memory_limit=true code_leases=true reclaimed=true");
}
int main(int argc,char **argv)
{
    CHECK(argc==3); LbEngine *engine; LbWorld *world;
    Memory memory={0}; LbSystemAllocator allocator={&memory,owned_allocate,owned_release};
    CHECK(lb_engine_create(&allocator,&engine)==LB_ENGINE_OK && lb_world_create(engine,&world)==LB_WORLD_OK); lb_engine_enter(engine);
    LbCodeModule *host=load(world,argv[1]),*target=load(world,argv[2]);
    semantics(world,host,target); fixed_error(world,host); lifetime(world,host,target); boundaries(world,host,target); execution(world,host,target); fun_components(world,host,target);
    lb_engine_leave(engine); LbReclamation *reclamation;
    CHECK(lb_world_stop(world,&reclamation)==LB_WORLD_OK && lb_reclamation_poll(reclamation)==LB_WORLD_OK);
    CHECK(lb_reclamation_consume(reclamation)==LB_WORLD_OK); lb_reclamation_release(reclamation); lb_world_release(world);
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine); CHECK(!memory.live && !memory.bytes);
    puts("CORE_APPLY_COMPONENTS_OK nested=true fixed=true argument_limits=true moving_badarg=true code_pc_stack_leases=true allocation_work_prefixes=true balanced=true instruction_dispatch=true external_apply_export=true"); return 0;
}
