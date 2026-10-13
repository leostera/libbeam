/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#include "process_internal.h"
#include "world_internal.h"
#include "work_budget_internal.h"
#include "alloc_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"load-module:%d: %s\n",__LINE__,#x); abort(); } } while(0)
typedef enum { EMPTY, BYTES, UNALIGNED, ODD_BITS, NOT_BINARY } CodeKind;
typedef enum { PRESENT, ABSENT, BADARG } Expected;
static Eterm atom(LbWorld *w,const char *s)
{ Eterm a; CHECK(lb_atoms_intern(w->space->atoms,s,strlen(s),&a)==LB_ATOM_OK); return a; }
static LbCodeModule *load(LbWorld *w,const char *path)
{
    FILE *f=fopen(path,"rb"); CHECK(f && !fseek(f,0,SEEK_END)); long n=ftell(f); CHECK(n>0 && !fseek(f,0,SEEK_SET));
    unsigned char *b=malloc((size_t)n); CHECK(b && fread(b,1,(size_t)n,f)==(size_t)n && !fclose(f));
    LbCodeModule *m; LbBeamError e; LbCodeStatus status=lb_code_load(w->space,b,(size_t)n,&m,&e);
    if(status!=LB_CODE_OK) fprintf(stderr,"load=%d stage=%s offset=%zu\n",status,e.stage,e.offset);
    CHECK(status==LB_CODE_OK); free(b); return m;
}
static LbProcess *create(LbWorld *w,LbCodeModule *m,const char *function,Eterm name,CodeKind kind)
{
    LbCodeEntry *entry; LbProcess *p; Eterm args[2]={name,NIL};
    CHECK(lb_code_entry_acquire(w->space,m->name,atom(w,function),2,&entry)==LB_CODE_OK);
    CHECK(lb_process_create(entry,args,2,512,&p)==LB_PROCESS_READY); lb_code_entry_release(entry);
    unsigned char bytes[128]; memset(bytes,0xa5,sizeof(bytes));
    if(kind!=NOT_BINARY) {
        size_t bits=kind==EMPTY?0:kind==ODD_BITS?7:kind==UNALIGNED?1024:24;
        CHECK(lb_bitstring_build(w->space->domain,&p->off_heap,&p->htop,bytes,bits,0,&p->reg[1])==LB_ALLOC_OK);
        if(kind==UNALIGNED) { LbSubBits *s=(LbSubBits *)boxed_val(p->reg[1]); s->start=3; s->end=11; }
    }
    if(!strcmp(function,"dynamic")) { p->reg[0]=TUPLE2(p->htop,name,am_load_module); p->htop+=3; }
    return p;
}
static unsigned parallel_moves;
static LbProcessStatus run(LbProcess *p)
{
    LbProcessStatus s=LB_PROCESS_READY;
    for(unsigned i=0;i<1000 && (s==LB_PROCESS_READY || s==LB_PROCESS_YIELDED);++i) {
        if(p->i[0]==op_move2_par_yxxx) ++parallel_moves;
        s=lb_process_run(p,1,100);
    }
    return s;
}
static void result(LbProcess *p,Eterm value,Eterm name,Expected expected,int primitive)
{
    if(expected==PRESENT) {
        CHECK(is_tuple(value) && arityval(*tuple_val(value))==2);
        CHECK(tuple_val(value)[1]==am_module && tuple_val(value)[2]==name);
        CHECK(primitive || p->status==LB_PROCESS_DONE);
    } else if(expected==ABSENT) {
        CHECK(is_non_value(value) && p->status==LB_PROCESS_CAPABILITY_DENIED && p->capability_failure);
        CHECK(!strcmp(p->capability_failure->message,"erlang:load_module/2 denied: CODE_LOADING"));
    } else {
        CHECK(is_non_value(value));
        CHECK(primitive?p->freason==LB_EXC_BADARG:p->status==LB_PROCESS_EXCEPTION && p->exception_value==am_badarg);
        CHECK(!p->capability_failure);
    }
}
static void check(LbWorld *w,LbCodeModule *m,Eterm name,CodeKind kind,Expected expected)
{
    LbAllocDomain *d=w->space->domain; size_t baseline=d->current,blocks=d->block_count,atoms=lb_atoms_count(w->space->atoms);
    LbCodeModule *modules=w->space->modules; Export *exports=w->space->exports;
    unsigned char metadata[sizeof(*m)]; memcpy(metadata,m,sizeof(*m));
    size_t code_size=m->word_count*sizeof(BeamInstr),exports_size=m->export_count*sizeof(Export);
    void *code=malloc(code_size),*bindings=malloc(exports_size); CHECK(code && bindings);
    memcpy(code,m->words,code_size); memcpy(bindings,m->exports,exports_size);
    for(unsigned mode=0;mode<2;++mode) {
        uint64_t required=0;
        for(uint64_t pass=0;pass<=required+1;++pass) {
            LbProcess *p=create(w,m,mode?"dynamic":"direct",name,kind);
            LbWorkBudget *budget=lb_work_budget_create(w); CHECK(budget);
            uint64_t credit=pass?pass-1:1000000; budget->limit=budget->remaining=credit;
            lb_work_domain_borrow(d,budget); LbProcessStatus s=run(p);
            if(!pass || credit==required) {
                result(p,expected==PRESENT?lb_process_result(p):THE_NON_VALUE,name,expected,0);
                if(!pass) { required=credit-budget->remaining;
                    if(required>=10000) fprintf(stderr,"work=%llu mode=%u kind=%d\n",(unsigned long long)required,mode,kind);
                    CHECK(required>1 && required<10000); }
                else CHECK(!budget->remaining);
            } else CHECK(s==LB_PROCESS_WORK_LIMIT && !p->capability_failure);
            lb_work_domain_borrow(d,NULL); lb_work_domain_clear_failure(d); lb_alloc_domain_begin_operation(d);
            lb_process_destroy(p); lb_work_budget_release(budget);
            CHECK(d->current==baseline && d->block_count==blocks);
        }
    }
    /* Exercise the compiler's parallel Y/X move after a real decoding call as
     * well as the input-supplied dynamic name covered by every prefix above. */
    LbProcess *decoded=create(w,m,"decoded_dynamic",name,kind); (void)run(decoded);
    result(decoded,expected==PRESENT?lb_process_result(decoded):THE_NON_VALUE,name,expected,0);
    lb_process_destroy(decoded); CHECK(d->current==baseline && d->block_count==blocks);
    CHECK(w->space->modules==modules && w->space->exports==exports && lb_atoms_count(w->space->atoms)==atoms);
    CHECK(!memcmp(metadata,m,sizeof(*m)) && !memcmp(code,m->words,code_size) && !memcmp(bindings,m->exports,exports_size));
    free(bindings); free(code);
}
static void *deny(void *context,size_t size) { (void)context; (void)size; return NULL; }
static void primitive_gc(LbWorld *w,LbCodeModule *m,Eterm absent)
{
    LbAllocDomain *d=w->space->domain;
    for(unsigned mode=0;mode<3;++mode) {
        LbProcess *p=create(w,m,"direct",m->name,UNALIGNED); size_t users=m->users;
        while(p->htop<p->stop-S_RESERVED) *p->htop++=NIL;
        p->heap_reserved=9; size_t limit=d->limit; LbSystemAllocator allocator=d->system;
        if(mode==1) d->system.allocate=deny;
        if(mode==2) d->limit=d->current;
        Eterm output=lb_bif_load_module_2(p,p->reg,NULL);
        if(!mode) {
            result(p,output,m->name,PRESENT,1); CHECK(p->collections && p->heap_reserved==9);
            LbBitstringView v; CHECK(lb_bitstring_view(p->reg[1],&v) && v.bit_size==8);
        } else CHECK(is_non_value(output) && p->status==(mode==1?LB_PROCESS_NO_MEMORY:LB_PROCESS_LIMIT) && !p->capability_failure);
        CHECK(m->users==users); d->system=allocator; d->limit=limit;
        lb_alloc_domain_begin_operation(d); lb_process_destroy(p);
    }
    for(unsigned invalid=0;invalid<2;++invalid) {
        LbProcess *p=create(w,m,"direct",absent,invalid?ODD_BITS:UNALIGNED);
        while(p->htop<p->stop-S_RESERVED) *p->htop++=NIL;
        Eterm *top=p->htop; LbSystemAllocator allocator=d->system; size_t limit=d->limit;
        d->system.allocate=deny; d->limit=d->current;
        Eterm output=lb_bif_load_module_2(p,p->reg,NULL);
        result(p,output,absent,invalid?BADARG:ABSENT,1); CHECK(p->htop==top);
        d->system=allocator; d->limit=limit; lb_alloc_domain_begin_operation(d); lb_process_destroy(p);
    }
}
static void host_calls(LbWorld *w)
{
    const unsigned char bytes[]={0xff,0,0x80,0x01};
    LbBytesView module={(const unsigned char *)"load_module_slice",17},input={bytes,sizeof(bytes)},output;
    for(unsigned i=0;i<3;++i) {
        LbCall *call; LbBytesView function=i==1?(LbBytesView){(const unsigned char *)"absent",6}:
            (LbBytesView){(const unsigned char *)"present",7};
        CHECK((i?lb_world_call:lb_world_start)(w,module,function,input,&call)==LB_WORLD_OK);
        LbWorldStatus status=LB_WORLD_PENDING;
        for(unsigned wait=0;wait<5000 && status==LB_WORLD_PENDING;++wait) {
            status=lb_call_poll(call,&output);
            if(status==LB_WORLD_PENDING) { struct timespec tick={0,1000000}; nanosleep(&tick,NULL); }
        }
        if(i==1) {
            CHECK(status==LB_WORLD_CAPABILITY_DENIED);
            const LbCapabilityFailure *failure=lb_call_capability_failure(call);
            CHECK(failure && !strcmp(failure->message,"erlang:load_module/2 denied: CODE_LOADING"));
        } else CHECK(status==LB_WORLD_OK && output.size==sizeof(bytes) && !memcmp(output.data,bytes,sizeof(bytes)));
        CHECK(lb_call_consume(call)==LB_WORLD_OK); lb_call_release(call);
    }
}
static void close_world(LbWorld *w)
{
    LbReclamation *r; CHECK(lb_world_stop(w,&r)==LB_WORLD_OK);
    for(unsigned wait=0;wait<5000 && lb_reclamation_poll(r)==LB_WORLD_PENDING;++wait) {
        struct timespec tick={0,1000000}; nanosleep(&tick,NULL);
    }
    CHECK(lb_reclamation_consume(r)==LB_WORLD_OK); lb_reclamation_release(r);
    LbWorldResourceUsage usage; CHECK(lb_world_resource_usage(w,&usage)==LB_WORLD_OK && usage.closed && !usage.current);
    lb_world_release(w);
}
int main(int argc,char **argv)
{
    CHECK(argc==3); LbEngine *e; LbWorld *w,*peer;
    CHECK(lb_engine_create(NULL,&e)==LB_ENGINE_OK && lb_world_create(e,&w)==LB_WORLD_OK && lb_world_create(e,&peer)==LB_WORLD_OK);
    lb_engine_enter(e);
    LbCodeModule *m=load(w,argv[1]),*target=load(w,argv[2]),*other=load(peer,argv[1]);
    Eterm absent=atom(w,"load_module_absent"),target_name=target->name;
    for(unsigned kind=EMPTY;kind<=NOT_BINARY;++kind) {
        check(w,m,m->name,(CodeKind)kind,kind>=ODD_BITS?BADARG:PRESENT);
        check(w,m,absent,(CodeKind)kind,kind>=ODD_BITS?BADARG:ABSENT);
        check(w,m,NIL,(CodeKind)kind,BADARG);
    }
    check(w,m,am_erlang,EMPTY,PRESENT); check(w,m,target_name,BYTES,PRESENT);
    check(peer,other,atom(peer,"apply_target"),BYTES,ABSENT);
    primitive_gc(w,m,absent);
    CHECK(!target->users && lb_code_unload(target)==LB_CODE_OK); check(w,m,target_name,EMPTY,ABSENT);
    target=load(w,argv[2]); check(w,m,target_name,EMPTY,PRESENT); CHECK(!target->users);
    CHECK(parallel_moves>0);
    lb_engine_leave(e); host_calls(w); close_world(peer); close_world(w);
    CHECK(lb_engine_shutdown(e)==LB_ENGINE_OK); lb_engine_release(e);
    puts("CORE_LOAD_MODULE_OK existing_noop=true absent_denied=true precedence=true unaligned=true direct_dynamic=true work_prefixes=true moving_gc=true quota_oom=true peer_unload_reload=true");
}
