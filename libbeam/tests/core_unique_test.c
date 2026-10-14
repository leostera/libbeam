/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#include "process_internal.h"
#include "world_internal.h"
#include "work_budget_internal.h"
#include "alloc_internal.h"
#include "big.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while(0)
typedef struct { size_t calls,fail,live,bytes; } Memory;
typedef union { max_align_t alignment; size_t bytes; } Header;
static void *allocate(void *context,size_t size)
{
    Memory *m=context; if(++m->calls==m->fail || size>SIZE_MAX-sizeof(Header)) return NULL;
    Header *h=malloc(sizeof(*h)+size); if(!h) return NULL; h->bytes=size; ++m->live; m->bytes+=size; return h+1;
}
static void release(void *context,void *pointer)
{
    Memory *m=context; Header *h=(Header *)pointer-1; CHECK(m->live && m->bytes>=h->bytes);
    --m->live; m->bytes-=h->bytes; memset(pointer,0xa5,h->bytes); free(h);
}
static LbBytesView text(const char *s) { return (LbBytesView){(const unsigned char *)s,strlen(s)}; }
static int same(LbUniqueSequence a,LbUniqueSequence b) { return a.low==b.low && a.high==b.high && a.exhausted==b.exhausted; }
static LbProcess *create(LbWorld *w,size_t options,int positive,int monotonic,int collect)
{
    Eterm module,function,input=NIL; LbCodeEntry *entry; LbProcess *p;
    CHECK(lb_atoms_find(w->space->atoms,"unique_slice",12,&module)==LB_ATOM_OK);
    CHECK(lb_atoms_find(w->space->atoms,"identity",8,&function)==LB_ATOM_OK);
    CHECK(lb_code_entry_acquire(w->space,module,function,1,&entry)==LB_CODE_OK);
    CHECK(lb_process_create(entry,&input,1,2048,&p)==LB_PROCESS_READY); lb_code_entry_release(entry);
    Eterm list=NIL;
    for(size_t i=0;i<options;++i) { list=CONS(p->htop,am_monotonic,list); p->htop+=2; }
    if(positive) { list=CONS(p->htop,am_positive,list); p->htop+=2; }
    if(monotonic) { list=CONS(p->htop,am_monotonic,list); p->htop+=2; }
    p->reg[0]=list; p->reg[2]=TUPLE2(p->htop,list,make_small(42)); p->htop+=3; p->construction_root=p->reg[2];
    p->heap_reserved=7; if(collect) while(p->htop<p->stop-S_RESERVED) *p->htop++=NIL;
    return p;
}
static void expected(Eterm result,LbUniqueSequence raw,int positive)
{
    const Uint offset=(Uint)(-MIN_SMALL);
    if(!positive && !raw.high && raw.low<offset) { CHECK(result==make_small(MIN_SMALL+(Sint)raw.low)); return; }
    if(positive && raw.low==UINT64_MAX && raw.high==UINT64_MAX) {
        CHECK(lb_is_big(result) && boxed_val(result)[0]==_make_header(3,POS_BIG_SUBTAG) && !boxed_val(result)[1] && !boxed_val(result)[2] && boxed_val(result)[3]==1); return;
    }
    __uint128_t value=((__uint128_t)raw.high<<64)|raw.low;
    value=positive?value+1:value-offset;
    if(value<=MAX_SMALL) CHECK(result==make_small((Sint)value));
    else {
        size_t digits=(value>>64)?2:1; CHECK(lb_is_big(result) && boxed_val(result)[0]==_make_header(digits,POS_BIG_SUBTAG));
        CHECK(boxed_val(result)[1]==(Uint)value); if(digits==2) CHECK(boxed_val(result)[2]==(Uint)(value>>64));
    }
}
static void advanced(LbUniqueSequence before,LbUniqueSequence after)
{
    if(before.low==UINT64_MAX && before.high==UINT64_MAX) CHECK(after.exhausted && after.low==before.low && after.high==before.high);
    else CHECK(!after.exhausted && after.low==before.low+1 && after.high==before.high+(before.low==UINT64_MAX));
}
static void components(LbEngine *engine,LbWorld *w,Memory *m)
{
    LbAllocDomain *d=&w->domain; size_t baseline=d->current;
    const LbUniqueSequence seeds[]={ {0,0,0},{MAX_SMALL-1,0,0},{MAX_SMALL,0,0}, {(Uint)(-MIN_SMALL),0,0},
        {(Uint)(-MIN_SMALL)+(Uint)MAX_SMALL,0,0},{(Uint)(-MIN_SMALL)+(Uint)MAX_SMALL+1,0,0},
        {UINT64_MAX,0,0},{0,1,0},{UINT64_MAX,UINT64_MAX,0} };
    for(unsigned mono=0;mono<2;++mono) for(unsigned pos=0;pos<2;++pos) for(unsigned gc=0;gc<2;++gc)
    for(size_t n=0;n<sizeof(seeds)/sizeof(*seeds);++n) {
        LbProcess *p=create(w,0,pos,mono,gc); engine->unique_sequences[mono]=seeds[n];
        LbUniqueSequence other=engine->unique_sequences[!mono]; Eterm result=lb_bif_unique_integer_1(p,p->reg,NULL);
        expected(result,seeds[n],pos); advanced(seeds[n],engine->unique_sequences[mono]); CHECK(same(other,engine->unique_sequences[!mono]));
        CHECK(p->heap_reserved==7 && p->reg[2]==p->construction_root && tuple_val(p->reg[2])[1]==p->reg[0] && tuple_val(p->reg[2])[2]==make_small(42));
        if(gc && lb_is_big(result)) CHECK(p->collections);
        if(engine->unique_sequences[mono].exhausted) {
            LbUniqueSequence last=engine->unique_sequences[mono];
            CHECK(is_non_value(lb_bif_unique_integer_1(p,p->reg,NULL)) && p->freason==LB_EXC_SYSTEM_LIMIT && same(last,engine->unique_sequences[mono]));
        }
        lb_process_destroy(p); CHECK(d->current==baseline);
    }
    engine->unique_sequences[0]=(LbUniqueSequence){0}; engine->unique_sequences[1]=(LbUniqueSequence){0};
    for(unsigned arity=0;arity<2;++arity) for(unsigned gc=0;gc<2;++gc) {
        const LbUniqueSequence seed={(Uint)(-MIN_SMALL)+(Uint)MAX_SMALL+1,0,0};
        uint64_t cost=0;
        for(uint64_t pass=0;pass<=cost+1;++pass) {
            LbProcess *p=create(w,0,0,0,gc); engine->unique_sequences[0]=seed;
            LbWorkBudget *budget=lb_work_budget_create(w); CHECK(budget); uint64_t credit=pass?pass-1:1000000;
            budget->limit=budget->remaining=credit; lb_work_domain_borrow(d,budget);
            Eterm *heap=p->heap,*top=p->htop; size_t live=d->current;
            Eterm result=arity?lb_bif_unique_integer_1(p,p->reg,NULL):lb_bif_unique_integer_0(p,NULL,NULL);
            if(!pass) { cost=credit-budget->remaining; CHECK(cost>1 && cost<20000); }
            if(!pass || credit==cost) { expected(result,seed,0); advanced(seed,engine->unique_sequences[0]); if(pass) CHECK(!budget->remaining); }
            else CHECK(is_non_value(result) && p->status==LB_PROCESS_WORK_LIMIT && p->freason==LB_EXC_NULL && same(seed,engine->unique_sequences[0]) && p->heap==heap && p->htop==top && !p->collections && d->current==live);
            lb_work_domain_borrow(d,NULL); lb_work_domain_clear_failure(d); lb_alloc_domain_begin_operation(d);
            lb_work_budget_release(budget); lb_process_destroy(p); CHECK(d->current==baseline);
        }
        if(!gc) continue;
        int done=0;
        for(size_t prefix=0;prefix<20 && !done;++prefix) {
            LbProcess *p=create(w,0,0,0,1); engine->unique_sequences[0]=seed;
            size_t limit=d->limit,live=d->current; Eterm *heap=p->heap,*top=p->htop;
            if(!prefix) d->limit=live; else m->fail=m->calls+prefix;
            Eterm result=arity?lb_bif_unique_integer_1(p,p->reg,NULL):lb_bif_unique_integer_0(p,NULL,NULL);
            d->limit=limit; m->fail=0;
            if(is_non_value(result)) CHECK(p->status==(prefix?LB_PROCESS_NO_MEMORY:LB_PROCESS_LIMIT) && p->freason==LB_EXC_NULL && same(seed,engine->unique_sequences[0]) && p->heap==heap && p->htop==top && !p->collections && d->current==live);
            else { expected(result,seed,0); advanced(seed,engine->unique_sequences[0]); CHECK(p->collections); done=1; }
            lb_alloc_domain_begin_operation(d); lb_process_destroy(p); CHECK(d->current==baseline);
        }
        CHECK(done);
    }
    for(unsigned kind=0;kind<5;++kind) {
        LbProcess *p=create(w,0,0,0,0);
        if(kind==0) p->reg[0]=am_true;
        if(kind==1) p->reg[0]=make_small(0);
        if(kind==2) { p->reg[0]=TUPLE1(p->htop,am_positive); p->htop+=2; }
        if(kind==3) { p->reg[0]=CONS(p->htop,am_monotonic,am_true); p->htop+=2; }
        if(kind==4) { p->reg[0]=CONS(p->htop,am_true,NIL); p->htop+=2; }
        LbUniqueSequence before[2]={engine->unique_sequences[0],engine->unique_sequences[1]}; size_t calls=m->calls; m->fail=calls+1;
        CHECK(is_non_value(lb_bif_unique_integer_1(p,p->reg,NULL)) && p->freason==LB_EXC_BADARG && m->calls==calls);
        CHECK(same(before[0],engine->unique_sequences[0]) && same(before[1],engine->unique_sequences[1])); m->fail=0;
        lb_process_destroy(p); CHECK(d->current==baseline);
    }
    for(unsigned credit=0;credit<=513;++credit) {
        LbProcess *p=create(w,512,0,0,0); LbUniqueSequence seed={0}; engine->unique_sequences[1]=seed;
        LbWorkBudget *budget=lb_work_budget_create(w); CHECK(budget); budget->limit=budget->remaining=credit; lb_work_domain_borrow(d,budget);
        Eterm result=lb_bif_unique_integer_1(p,p->reg,NULL);
        if(credit==513) { expected(result,seed,0); advanced(seed,engine->unique_sequences[1]); CHECK(!budget->remaining); }
        else CHECK(is_non_value(result) && p->status==LB_PROCESS_WORK_LIMIT && same(seed,engine->unique_sequences[1]));
        lb_work_domain_borrow(d,NULL); lb_work_domain_clear_failure(d); lb_alloc_domain_begin_operation(d);
        lb_work_budget_release(budget); lb_process_destroy(p); CHECK(d->current==baseline);
    }
    puts("CORE_UNIQUE_OK canonical_boundaries=true two_word_carry=true terminal_no_wrap=true independent_streams=true exact_work_prefixes=true duplicate_modifier_work=true allocation_prefixes=true rollback=true poisoned_heap=true roots=true");
}
static LbProcess *order_process(LbWorld *w,const char *name,Eterm left,Eterm right,int depth)
{
    Eterm module,function,input=NIL; LbCodeEntry *entry; LbProcess *p;
    CHECK(lb_atoms_find(w->space->atoms,"order_slice",11,&module)==LB_ATOM_OK);
    CHECK(lb_atoms_find(w->space->atoms,name,strlen(name),&function)==LB_ATOM_OK);
    CHECK(lb_code_entry_acquire(w->space,module,function,1,&entry)==LB_CODE_OK);
    CHECK(lb_process_create(entry,&input,1,2048,&p)==LB_PROCESS_READY); lb_code_entry_release(entry);
    for(int i=0;i<depth;++i) { left=TUPLE2(p->htop,left,make_small(i)); p->htop+=3; right=TUPLE2(p->htop,right,make_small(i)); p->htop+=3; }
    p->reg[0]=TUPLE2(p->htop,left,right); p->htop+=3; return p;
}
static LbProcessStatus run(LbProcess *p)
{
    LbProcessStatus s=LB_PROCESS_READY;
    for(unsigned i=0;i<1000 && (s==LB_PROCESS_READY || s==LB_PROCESS_YIELDED);++i) s=lb_process_run(p,1,100);
    return s;
}
static void ordering(LbEngine *engine,LbWorld *w,Memory *m)
{
    CHECK(engine->bifs[BIF_seq_2].name==am_Eq && engine->bifs[BIF_sneq_2].name==am_Neq);
    CHECK(engine->bifs[BIF_seqeq_2].name==am_Eqeq && engine->bifs[BIF_sneqeq_2].name==am_Neqeq);
    LbProcess *p=create(w,0,0,0,0); Eterm *hp=p->htop; hp[0]=_make_header(1,FLOAT_SUBTAG); hp[1]=UINT64_C(0x3ff0000000000000); p->htop+=2;
    Eterm values[]={make_small(1),make_boxed(hp)};
    CHECK(engine->bifs[BIF_seq_2].f(p,values,NULL)==am_false && engine->bifs[BIF_seqeq_2].f(p,values,NULL)==am_true);
    CHECK(engine->bifs[BIF_sneq_2].f(p,values,NULL)==am_true && engine->bifs[BIF_sneqeq_2].f(p,values,NULL)==am_false); lb_process_destroy(p);
    const char *names[]={"ge_xx","ge_cx","ge_xc","ge_yy","lt_xx","lt_cx","lt_xc","lt_yy"};
    const BeamInstr ops[]={op_is_ge_fxx,op_is_ge_literal_fcx,op_is_ge_literal_fxc,op_is_ge_fss,op_is_lt_fxx,op_is_lt_literal_fcx,op_is_lt_literal_fxc,op_is_lt_fss};
    LbAllocDomain *d=&w->domain; size_t baseline=d->current;
    for(unsigned form=0;form<8;++form) for(Sint a=-2;a<=2;++a) for(Sint b=-2;b<=2;++b) {
        uint64_t cost=0; Sint left=form%4==1?0:a,right=form%4==1?a:form%4==2?0:b;
        Eterm expected_result=(form<4?left>=right:left<right)?am_true:am_false;
        for(unsigned mode=0;mode<4;++mode) {
            p=order_process(w,names[form],make_small(a),make_small(b),0);
            for(unsigned step=0;step<100 && *p->i!=ops[form];++step) CHECK(lb_process_run(p,1,100)==LB_PROCESS_YIELDED);
            CHECK(*p->i==ops[form]); LbWorkBudget *budget=lb_work_budget_create(w); CHECK(budget);
            uint64_t credit=!mode?1000000:mode==1?cost:mode==2?cost-1:0; budget->limit=budget->remaining=credit; lb_work_domain_borrow(d,budget);
            LbProcessStatus status=run(p);
            if(mode<2) { CHECK(status==LB_PROCESS_DONE && lb_process_result(p)==expected_result); if(!mode) { cost=credit-budget->remaining; CHECK(cost>1); } else CHECK(!budget->remaining); }
            else CHECK(status==LB_PROCESS_WORK_LIMIT && p->freason==LB_EXC_NULL);
            lb_work_domain_borrow(d,NULL); lb_work_domain_clear_failure(d); lb_alloc_domain_begin_operation(d);
            lb_work_budget_release(budget); lb_process_destroy(p); CHECK(d->current==baseline);
        }
    }
    const unsigned structural_forms[]={0,3,4,7};
    for(size_t index=0;index<sizeof(structural_forms)/sizeof(*structural_forms);++index) {
        unsigned form=structural_forms[index]; int done=0;
        for(size_t prefix=0;prefix<20 && !done;++prefix) {
            p=order_process(w,names[form],make_small(7),make_small(7),128);
            for(unsigned step=0;step<100 && *p->i!=ops[form];++step) CHECK(lb_process_run(p,1,100)==LB_PROCESS_YIELDED);
            CHECK(*p->i==ops[form]); size_t limit=d->limit,live=d->current; Eterm *heap=p->heap,*top=p->htop;
            if(!prefix) d->limit=live; else m->fail=m->calls+prefix;
            LbProcessStatus status=run(p); d->limit=limit; m->fail=0;
            if(status==LB_PROCESS_DONE) { CHECK(lb_process_result(p)==(form<4?am_true:am_false)); done=1; }
            else CHECK(status==(prefix?LB_PROCESS_NO_MEMORY:LB_PROCESS_LIMIT) && p->freason==LB_EXC_NULL);
            CHECK(p->heap==heap && p->htop==top && !p->collections && d->current==live);
            lb_alloc_domain_begin_operation(d); lb_process_destroy(p); CHECK(d->current==baseline);
        }
        CHECK(done);
        p=order_process(w,names[form],make_small(7),make_small(8),1);
        CHECK(run(p)==LB_PROCESS_REFUSED); lb_process_destroy(p); CHECK(d->current==baseline);
    }
    puts("ORDERED_GUARDS_OK variants=8 both_branches=true exact_zero_work=true allocation_prefixes=true memory_limit=true refusal_not_false=true pinned_equality_identities=true");
}
static void retire(LbWorld *w)
{
    LbReclamation *r; CHECK(lb_world_stop(w,&r)==LB_WORLD_OK);
    for(unsigned i=0;i<10000 && lb_reclamation_poll(r)==LB_WORLD_PENDING;++i) { struct timespec t={0,1000000}; nanosleep(&t,NULL); }
    CHECK(lb_reclamation_consume(r)==LB_WORLD_OK); lb_reclamation_release(r); lb_world_release(w);
}
static void invoke(LbWorld *w,const char *function,char *output,size_t capacity,int start)
{
    LbCall *call; LbBytesView result; CHECK((start?lb_world_start:lb_world_call)(w,text("unique_slice"),text(function),text("unique"),&call)==LB_WORLD_OK);
    LbWorldStatus status=LB_WORLD_PENDING;
    for(unsigned i=0;i<10000 && (status=lb_call_poll(call,&result))==LB_WORLD_PENDING;++i) { struct timespec t={0,1000000}; nanosleep(&t,NULL); }
    CHECK(status==LB_WORLD_OK && result.size<capacity); memcpy(output,result.data,result.size); output[result.size]=0;
    CHECK(lb_call_consume(call)==LB_WORLD_OK); lb_call_release(call);
}
int main(int argc,char **argv)
{
    CHECK(argc==3); FILE *f=fopen(argv[1],"rb"); CHECK(f && !fseek(f,0,SEEK_END)); long size=ftell(f); CHECK(size>0 && !fseek(f,0,SEEK_SET));
    unsigned char *bytes=malloc((size_t)size); CHECK(bytes && fread(bytes,1,(size_t)size,f)==(size_t)size && !fclose(f));
    f=fopen(argv[2],"rb"); CHECK(f && !fseek(f,0,SEEK_END)); long order_size=ftell(f); CHECK(order_size>0 && !fseek(f,0,SEEK_SET));
    unsigned char *order_bytes=malloc((size_t)order_size); CHECK(order_bytes && fread(order_bytes,1,(size_t)order_size,f)==(size_t)order_size && !fclose(f));
    Memory memory={0}; LbSystemAllocator allocator={&memory,allocate,release};
    for(unsigned phase=0;phase<2;++phase) {
        LbEngine *engine; LbWorld *worlds[2]; CHECK(lb_engine_create(&allocator,&engine)==LB_ENGINE_OK);
        for(unsigned i=0;i<2;++i) { CHECK(lb_world_create(engine,&worlds[i])==LB_WORLD_OK); LbWorldLoadReport report={0};
            LbWorldStatus loaded=lb_world_load_report(worlds[i],(LbBytesView){bytes,(size_t)size},&report);
            if(loaded!=LB_WORLD_OK) fprintf(stderr,"load=%d stage=%s\n",loaded,report.stage); CHECK(loaded==LB_WORLD_OK);
            loaded=lb_world_load_report(worlds[i],(LbBytesView){order_bytes,(size_t)order_size},&report);
            if(loaded!=LB_WORLD_OK) fprintf(stderr,"order load=%d stage=%s\n",loaded,report.stage); CHECK(loaded==LB_WORLD_OK); }
        if(!phase) { lb_engine_enter(engine); components(engine,worlds[0],&memory); ordering(engine,worlds[0],&memory); lb_engine_leave(engine); }
        else {
            long long previous=0; char output[64];
            for(unsigned i=0;i<100;++i) {
                invoke(worlds[i%2],"next",output,sizeof(output),i<2); char *end; long long value=strtoll(output,&end,10); CHECK(!*end && (i==0 || value>previous)); previous=value;
                invoke(worlds[i%2],"copy",output,sizeof(output),0); CHECK(!strcmp(output,"unique"));
            }
            invoke(worlds[0],"family",output,sizeof(output),0); CHECK(!strcmp(output,"unique"));
            invoke(worlds[1],"family",output,sizeof(output),0); CHECK(!strcmp(output,"unique"));
        }
        retire(worlds[0]);
        if(phase) { char output[64]; invoke(worlds[1],"copy",output,sizeof(output),0); CHECK(!strcmp(output,"unique")); }
        retire(worlds[1]); CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine); CHECK(!memory.live && !memory.bytes);
    }
    free(order_bytes); free(bytes); puts("UNIQUE_WORLD_OK shared_engine_order=true repeated_calls=true dynamic=true children=true peer_survival=true reclaimed=true"); return 0;
}
