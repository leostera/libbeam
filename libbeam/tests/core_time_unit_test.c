/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#include "process_internal.h"
#include "world_internal.h"
#include "work_budget_internal.h"
#include "alloc_internal.h"
#include "big.h"
#include "binary.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while(0)
typedef struct { size_t calls,fail,live,bytes; } Memory;
typedef union { max_align_t alignment; size_t size; } Header;
static void *allocate(void *context,size_t size) { Memory *m=context; if(++m->calls==m->fail || size>SIZE_MAX-sizeof(Header)) return NULL; Header *h=malloc(sizeof(*h)+size); if(!h) return NULL; h->size=size; ++m->live; m->bytes+=size; return h+1; }
static void release(void *context,void *pointer) { Memory *m=context; Header *h=(Header *)pointer-1; CHECK(m->live && m->bytes>=h->size); --m->live; m->bytes-=h->size; memset(pointer,0xa5,h->size); free(h); }
static LbBytesView text(const char *s) { return (LbBytesView){(const unsigned char *)s,strlen(s)}; }
static LbProcess *create(LbCodeEntry *entry,int collect)
{
    LbProcess *p; Eterm nil=NIL; CHECK(lb_process_create(entry,&nil,1,128,&p)==LB_PROCESS_READY);
    p->reg[0]=make_boxed(p->htop); *p->htop++=_make_header(2,NEG_BIG_SUBTAG); *p->htop++=0; *p->htop++=1;
    p->reg[1]=make_small(3); p->reg[2]=make_small(1000000000);
    p->reg[3]=TUPLE3(p->htop,p->reg[0],p->reg[1],p->reg[2]); p->htop+=4; p->construction_root=p->reg[3]; p->heap_reserved=7;
    if(collect) while(p->htop<p->stop-S_RESERVED) *p->htop++=NIL;
    return p;
}
static __int128 converted(__int128 t,Sint from,Sint to) { __int128 n=t*to,q=n/from; return q-(n<0 && n%from!=0); }
static void integer(Eterm result,__int128 expected)
{
    if(expected>=MIN_SMALL && expected<=MAX_SMALL) { CHECK(result==make_small((Sint)expected)); return; }
    __uint128_t magnitude=expected<0?-expected:expected; size_t digits=magnitude>>64?2:1;
    CHECK(lb_is_big(result) && boxed_val(result)[0]==_make_header(digits,expected<0?NEG_BIG_SUBTAG:POS_BIG_SUBTAG));
    CHECK(boxed_val(result)[1]==(Uint)magnitude); if(digits==2) CHECK(boxed_val(result)[2]==(Uint)(magnitude>>64));
}
static void roots(LbProcess *p)
{
    CHECK(p->heap_reserved==7 && p->reg[3]==p->construction_root && tuple_val(p->reg[3])[1]==p->reg[0]);
    CHECK(boxed_val(p->reg[0])[0]==_make_header(2,NEG_BIG_SUBTAG) && !boxed_val(p->reg[0])[1] && boxed_val(p->reg[0])[2]==1);
    CHECK(p->reg[1]==make_small(3) && p->reg[2]==make_small(1000000000));
}
static void components(LbWorld *w,Memory *m)
{
    LbAllocDomain *d=&w->domain; Eterm module,function; LbCodeEntry *entry;
    CHECK(lb_atoms_find(w->space->atoms,"time_unit_slice",15,&module)==LB_ATOM_OK && lb_atoms_find(w->space->atoms,"identity",8,&function)==LB_ATOM_OK);
    CHECK(lb_code_entry_acquire(w->space,module,function,1,&entry)==LB_CODE_OK); size_t baseline=d->current;
    LbProcess *clock=create(entry,0); LbWorkBudget *clock_budget=lb_work_budget_create(w); CHECK(clock_budget);
    clock_budget->limit=clock_budget->remaining=100000; lb_work_domain_borrow(d,clock_budget);
    Eterm first=lb_bif_monotonic_time_0(clock,NULL,NULL),second=lb_bif_monotonic_time_0(clock,NULL,NULL);
    CHECK(is_small(first) && is_small(second) && signed_val(first)>=0 && signed_val(second)>=signed_val(first));
    Eterm nanosecond,native,microsecond,unit;
    CHECK(lb_atoms_find(w->space->atoms,"nanosecond",10,&nanosecond)==LB_ATOM_OK);
    CHECK(lb_atoms_find(w->space->atoms,"native",6,&native)==LB_ATOM_OK);
    CHECK(lb_atoms_find(w->space->atoms,"microsecond",11,&microsecond)==LB_ATOM_OK);
    Eterm second_unit; CHECK(lb_atoms_find(w->space->atoms,"second",6,&second_unit)==LB_ATOM_OK);
    Eterm wall_ns=lb_bif_system_time_0(clock,NULL,NULL);
    CHECK(lb_is_big(wall_ns) && (*boxed_val(wall_ns)&_TAG_HEADER_MASK)!=NEG_BIG_SUBTAG);
    clock->reg[0]=nanosecond; Eterm wall_nanos=lb_bif_system_time_1(clock,clock->reg,NULL);
    clock->reg[0]=second_unit; Eterm wall_seconds=lb_bif_system_time_1(clock,clock->reg,NULL);
    CHECK(lb_is_big(wall_nanos) && is_small(wall_seconds) && signed_val(wall_seconds)>0 && clock->native_time_root==NIL);
    clock->reg[0]=nanosecond; Eterm nanos=lb_bif_monotonic_time_1(clock,clock->reg,NULL);
    clock->reg[0]=native; Eterm native_time=lb_bif_monotonic_time_1(clock,clock->reg,NULL);
    CHECK(is_small(nanos) && is_small(native_time) && signed_val(nanos)>0 && signed_val(native_time)>=signed_val(nanos));
    clock->reg[0]=microsecond; Eterm micros=lb_bif_monotonic_time_1(clock,clock->reg,NULL);
    CHECK(is_small(micros) && signed_val(micros)>=signed_val(nanos)/1000);
    unit=am_true; clock->reg[0]=unit; clock->freason=LB_EXC_NULL;
    CHECK(is_non_value(lb_bif_monotonic_time_1(clock,clock->reg,NULL)) && clock->freason==LB_EXC_BADARG);
    lb_work_domain_borrow(d,NULL); lb_work_budget_release(clock_budget); lb_process_destroy(clock); CHECK(d->current==baseline);
    const Sint times[]={MIN_SMALL,MIN_SMALL+1,-1001,-1,0,1,1001,MAX_SMALL},rates[]={1,3,7,1000000000,MAX_SMALL};
    for(size_t t=0;t<8;++t) for(size_t f=0;f<5;++f) for(size_t u=0;u<5;++u) {
        LbProcess *p=create(entry,0); p->reg[0]=make_small(times[t]); p->reg[1]=make_small(rates[f]); p->reg[2]=make_small(rates[u]);
        integer(lb_bif_convert_time_unit_3(p,p->reg,NULL),converted(times[t],rates[f],rates[u])); lb_process_destroy(p); CHECK(d->current==baseline);
    }
    const __int128 expected=converted(-((__int128)1<<64),3,1000000000);
    for(unsigned collect=0;collect<2;++collect) {
        uint64_t cost=0;
        for(uint64_t pass=0;pass<=cost+1;++pass) {
            LbProcess *p=create(entry,collect); LbWorkBudget *budget=lb_work_budget_create(w); CHECK(budget); uint64_t credit=pass?pass-1:1000000;
            budget->limit=budget->remaining=credit; Eterm *heap=p->heap,*top=p->htop; size_t live=d->current; lb_work_domain_borrow(d,budget);
            Eterm result=lb_bif_convert_time_unit_3(p,p->reg,NULL);
            if(!pass) { cost=credit-budget->remaining; CHECK(cost>1 && cost<20000); }
            if(!pass || credit==cost) { integer(result,expected); roots(p); CHECK(!collect || p->collections); if(pass) CHECK(!budget->remaining); }
            else {
                if(!is_non_value(result) || p->status!=LB_PROCESS_WORK_LIMIT || p->freason!=LB_EXC_NULL || p->heap!=heap || p->htop!=top || p->collections || d->current!=live)
                    fprintf(stderr,"work collect=%u credit=%llu cost=%llu nonvalue=%d status=%d reason=%lu moved=%d top=%d collections=%zu live=%zu/%zu\n",collect,(unsigned long long)credit,(unsigned long long)cost,is_non_value(result),p->status,(unsigned long)p->freason,p->heap!=heap,p->htop!=top,p->collections,d->current,live);
                CHECK(is_non_value(result) && p->status==LB_PROCESS_WORK_LIMIT && p->freason==LB_EXC_NULL && p->heap==heap && p->htop==top && !p->collections && d->current==live);
            }
            lb_work_domain_borrow(d,NULL); lb_work_domain_clear_failure(d); lb_alloc_domain_begin_operation(d); lb_work_budget_release(budget); lb_process_destroy(p); CHECK(d->current==baseline);
        }
        int done=0;
        for(size_t prefix=1;prefix<20 && !done;++prefix) {
            LbProcess *p=create(entry,collect); Eterm *heap=p->heap,*top=p->htop; size_t live=d->current; m->fail=m->calls+prefix;
            Eterm result=lb_bif_convert_time_unit_3(p,p->reg,NULL); m->fail=0;
            if(is_non_value(result)) CHECK(p->status==LB_PROCESS_NO_MEMORY && p->freason==LB_EXC_NULL && p->heap==heap && p->htop==top && !p->collections && d->current==live);
            else { integer(result,expected); roots(p); done=1; }
            lb_alloc_domain_begin_operation(d); lb_process_destroy(p); CHECK(d->current==baseline);
        }
        CHECK(done);
    }
    for(unsigned after_scratch=0;after_scratch<2;++after_scratch) {
        LbProcess *p=create(entry,1); size_t limit=d->limit,live=d->current; d->limit=live+(after_scratch?512:0);
        Eterm *heap=p->heap,*top=p->htop; CHECK(is_non_value(lb_bif_convert_time_unit_3(p,p->reg,NULL)) && p->status==LB_PROCESS_LIMIT && p->freason==LB_EXC_NULL);
        CHECK(p->heap==heap && p->htop==top && !p->collections && d->current==live); roots(p);
        d->limit=limit; lb_alloc_domain_begin_operation(d); lb_process_destroy(p); CHECK(d->current==baseline);
    }
    for(unsigned kind=0;kind<7;++kind) {
        LbProcess *p=create(entry,0); Eterm unit;
        if(kind==0) unit=am_true;
        else if(kind==1) unit=NIL;
        else if(kind==2) { unit=TUPLE1(p->htop,am_true); p->htop+=2; }
        else if(kind==3) { p->htop[0]=_make_header(1,FLOAT_SUBTAG); p->htop[1]=UINT64_C(0x3ff0000000000000); unit=make_boxed(p->htop); p->htop+=2; }
        else if(kind==4) { CHECK(lb_bitstring_build(d,&p->off_heap,&p->htop,"unit",32,0,&unit)==LB_ALLOC_OK); }
        else unit=make_small(kind==5?0:-1);
        p->reg[1]=p->reg[2]=unit; Eterm original=p->reg[0]; size_t calls=m->calls; m->fail=calls+1;
        Eterm result=lb_bif_convert_time_unit_3(p,p->reg,NULL);
        if(kind<5) CHECK(result==original); else CHECK(is_non_value(result) && p->freason==LB_EXC_BADARG);
        CHECK(m->calls==calls); m->fail=0; p->freason=LB_EXC_NULL; p->reg[2]=make_small(1);
        CHECK(is_non_value(lb_bif_convert_time_unit_3(p,p->reg,NULL)) && p->freason==LB_EXC_BADARG);
        lb_process_destroy(p); CHECK(d->current==baseline);
    }
    for(unsigned from_big=0;from_big<2;++from_big) {
        LbProcess *p=create(entry,0); __int128 answer;
        if(from_big) {
            p->reg[0]=make_boxed(p->htop); *p->htop++=_make_header(3,NEG_BIG_SUBTAG); *p->htop++=0; *p->htop++=0; *p->htop++=1;
            p->reg[1]=make_boxed(p->htop); *p->htop++=_make_header(2,POS_BIG_SUBTAG); *p->htop++=1; *p->htop++=1;
            p->reg[2]=make_small(3); answer=-3*((__int128)1<<64)+2;
        } else {
            p->reg[0]=make_small(1); p->reg[2]=make_boxed(p->htop); *p->htop++=_make_header(2,POS_BIG_SUBTAG); *p->htop++=0; *p->htop++=1;
            answer=((__int128)1<<64)/3;
        }
        for(unsigned i=0;i<3;++i) tuple_val(p->reg[3])[i+1]=p->reg[i];
        Eterm borrowed=p->reg[from_big?1:2]; Eterm *heap=p->heap;
        while(p->htop<p->stop-S_RESERVED) *p->htop++=NIL;
        integer(lb_bif_convert_time_unit_3(p,p->reg,NULL),answer);
        CHECK(p->collections && p->heap!=heap && p->reg[from_big?1:2]!=borrowed && p->reg[3]==p->construction_root && p->heap_reserved==7);
        for(unsigned i=0;i<3;++i) CHECK(tuple_val(p->reg[3])[i+1]==p->reg[i]);
        Eterm *unit=boxed_val(p->reg[from_big?1:2]); CHECK(unit[0]==_make_header(2,POS_BIG_SUBTAG) && unit[1]==from_big && unit[2]==1);
        lb_process_destroy(p); CHECK(d->current==baseline);
    }
    int equal_done=0;
    for(size_t prefix=0;prefix<20 && !equal_done;++prefix) {
        LbProcess *p=create(entry,0); CHECK(lb_heap_reserve(p,1024,MAX_REG));
        Eterm left=am_true,right=am_true;
        for(unsigned i=0;i<128;++i) { left=TUPLE2(p->htop,left,make_small(i)); p->htop+=3; right=TUPLE2(p->htop,right,make_small(i)); p->htop+=3; }
        p->reg[1]=left; p->reg[2]=right; Eterm original=p->reg[0]; Eterm *heap=p->heap,*top=p->htop;
        size_t live=d->current,limit=d->limit; if(!prefix) d->limit=live; else m->fail=m->calls+prefix;
        Eterm result=lb_bif_convert_time_unit_3(p,p->reg,NULL); d->limit=limit; m->fail=0;
        if(is_non_value(result)) CHECK(p->status==(prefix?LB_PROCESS_NO_MEMORY:LB_PROCESS_LIMIT) && p->freason==LB_EXC_NULL);
        else { CHECK(result==original); equal_done=1; }
        CHECK(p->heap==heap && p->htop==top && d->current==live);
        lb_alloc_domain_begin_operation(d); lb_process_destroy(p); CHECK(d->current==baseline);
    }
    CHECK(equal_done);
    for(unsigned variant=0;variant<4;++variant) {
        LbProcess *p=create(entry,0); CHECK(lb_heap_reserve(p,LB_BIG_ARITY_MAX+16,MAX_REG));
        Eterm *digits=p->htop; p->reg[0]=make_boxed(digits); p->htop+=LB_BIG_ARITY_MAX+1;
        int negative=variant==1 || variant==3;
        digits[0]=_make_header(LB_BIG_ARITY_MAX,negative?NEG_BIG_SUBTAG:POS_BIG_SUBTAG);
        for(size_t i=1;i<=LB_BIG_ARITY_MAX;++i) digits[i]=variant>=2?UINT64_MAX:0;
        if(variant<2) digits[LB_BIG_ARITY_MAX]=2;
        p->reg[1]=make_small(2); p->reg[2]=make_small(variant==2?MAX_SMALL:1);
        LbWorkBudget *budget=lb_work_budget_create(w); CHECK(budget); lb_work_domain_borrow(d,budget);
        Eterm result=lb_bif_convert_time_unit_3(p,p->reg,NULL);
        CHECK(budget->remaining && !budget->exhausted);
        if(variant>=2) CHECK(is_non_value(result) && p->freason==LB_EXC_BADARG);
        else {
            CHECK(lb_is_big(result) && boxed_val(result)[0]==_make_header(LB_BIG_ARITY_MAX,negative?NEG_BIG_SUBTAG:POS_BIG_SUBTAG));
            for(size_t i=1;i<LB_BIG_ARITY_MAX;++i) CHECK(!boxed_val(result)[i]);
            CHECK(boxed_val(result)[LB_BIG_ARITY_MAX]==1);
        }
        CHECK(boxed_val(p->reg[0])[0]==_make_header(LB_BIG_ARITY_MAX,negative?NEG_BIG_SUBTAG:POS_BIG_SUBTAG));
        for(size_t i=1;i<LB_BIG_ARITY_MAX;++i) CHECK(boxed_val(p->reg[0])[i]==(variant>=2?UINT64_MAX:0));
        CHECK(boxed_val(p->reg[0])[LB_BIG_ARITY_MAX]==(variant>=2?UINT64_MAX:2));
        lb_work_domain_borrow(d,NULL); lb_work_budget_release(budget); lb_process_destroy(p); CHECK(d->current==baseline);
    }
    lb_code_entry_release(entry); puts("CORE_TIME_UNIT_OK floor=true canonical_boundaries=true exact_work_prefixes=true allocation_prefixes=true memory_limit=true moving_roots=true poisoned_release=true pinned_equal_unit_quirk=true reclaimed=true");
}
int main(int argc,char **argv)
{
    int reference=argc>1 && !strcmp(argv[1],"--reference"); if(reference) { --argc; ++argv; } CHECK(argc==2);
    FILE *f=fopen(argv[1],"rb"); CHECK(f && !fseek(f,0,SEEK_END)); long size=ftell(f); CHECK(size>0 && !fseek(f,0,SEEK_SET));
    unsigned char *bytes=malloc((size_t)size); CHECK(bytes && fread(bytes,1,(size_t)size,f)==(size_t)size && !fclose(f));
    Memory memory={0}; LbSystemAllocator allocator={&memory,allocate,release}; LbEngine *engine; LbWorld *w;
    CHECK(lb_engine_create(&allocator,&engine)==LB_ENGINE_OK && lb_world_create(engine,&w)==LB_WORLD_OK); LbWorldLoadReport report={0};
    LbWorldStatus loaded=lb_world_load_report(w,(LbBytesView){bytes,(size_t)size},&report); free(bytes);
    if(loaded!=LB_WORLD_OK) fprintf(stderr,"load=%d stage=%s\n",loaded,report.stage); CHECK(loaded==LB_WORLD_OK);
    if(!reference) { lb_engine_enter(engine); components(w,&memory); lb_engine_leave(engine); }
    size_t rows=0;
    for(unsigned index=1;index<=13;++index) {
        char input[8]; snprintf(input,sizeof(input),"%u",index); LbCall *call;
        CHECK((index==1?lb_world_start:lb_world_call)(w,text("time_unit_slice"),text("all"),text(input),&call)==LB_WORLD_OK);
        LbWorldStatus status=LB_WORLD_PENDING; LbBytesView result;
        for(unsigned i=0;i<10000 && (status=lb_call_poll(call,&result))==LB_WORLD_PENDING;++i) { struct timespec delay={0,1000000}; nanosleep(&delay,NULL); }
        if(status!=LB_WORLD_OK) fprintf(stderr,"batch=%u status=%d\n",index,status);
        CHECK(status==LB_WORLD_OK); if(reference) CHECK(fwrite(result.data,1,result.size,stdout)==result.size);
        for(size_t i=0;i<result.size;++i) rows+=result.data[i]=='\n'; CHECK(lb_call_consume(call)==LB_WORLD_OK); lb_call_release(call);
    }
    CHECK(rows==2535); LbReclamation *r; CHECK(lb_world_stop(w,&r)==LB_WORLD_OK);
    for(unsigned i=0;i<10000 && lb_reclamation_poll(r)==LB_WORLD_PENDING;++i) { struct timespec delay={0,1000000}; nanosleep(&delay,NULL); }
    CHECK(lb_reclamation_consume(r)==LB_WORLD_OK); lb_reclamation_release(r); lb_world_release(w); CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine);
    CHECK(!memory.live && !memory.bytes); if(!reference) puts("TIME_UNIT_EXECUTION_OK rows=2535 direct_dynamic=true big_units=true negative_floor=true real_worker=true reclaimed=true"); return 0;
}
