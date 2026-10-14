/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#define _POSIX_C_SOURCE 200809L
#include "world_internal.h"
#include "process_internal.h"
#include "work_budget_internal.h"
#include "binary.h"
#include "bit_copy.h"
#include "reference.h"
#include <stdio.h>
#include <time.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"CHECK %d: %s\n",__LINE__,#x); exit(1); } } while(0)
typedef struct { size_t calls,fail,live; } Memory;
static void *allocate(void *context,size_t bytes)
{ Memory *m=context; if(++m->calls==m->fail) return NULL; void *p=malloc(bytes); if(p) ++m->live; return p; }
static void release(void *context,void *p)
{ Memory *m=context; CHECK(m->live); --m->live; free(p); }
static LbBytesView text(const char *s) { return (LbBytesView){(const unsigned char *)s,strlen(s)}; }
static void tick(void) { struct timespec delay={0,1000000}; nanosleep(&delay,NULL); }
static void unaligned(LbCodeEntry *entry,Memory *memory)
{
    const size_t sizes[]={0,1,3,4,7,8,11,12,13,23,24,25,63,64,65,257};
    size_t live=memory->live;
    for(size_t size=0;size<sizeof(sizes)/sizeof(sizes[0]);++size) for(unsigned tail=0;tail<8;++tail) {
        size_t n=sizes[size],length=n*8+tail; unsigned char source[320]={0}; Eterm expected=THE_NON_VALUE;
        for(size_t i=0;i<n;++i) source[i]=(unsigned char)(n-i);
        if(tail) source[n]=(unsigned char)((3u&((1u<<tail)-1))<<(8-tail));
        for(unsigned offset=0;offset<8;++offset) {
            unsigned char shifted[320]={0}; LbProcess *p;
            CHECK(lb_bits_copy(source,sizeof(source),0,shifted,sizeof(shifted),offset,length));
            CHECK(lb_process_create_binary(entry,shifted,sizeof(shifted),64,&p)==LB_PROCESS_READY);
            LbSubBits *view=(LbSubBits *)boxed_val(p->reg[0]); CHECK(view->thing_word==LB_HEADER_SUB_BITS);
            view->start=offset; view->end=offset+length;
            p->reg[1]=make_small(UINT64_C(1)<<32);
            Eterm actual=lb_bif_phash2_2(p,p->reg,NULL); CHECK(is_small(actual));
            if(!offset) expected=actual; else CHECK(actual==expected);
            lb_process_destroy(p); CHECK(memory->live==live);
        }
    }
}
static void identity_vectors(LbWorld *world,int print)
{
    lb_engine_enter(world->engine);
    LbCodeEntry *entry; Eterm module,function,input=NIL; LbProcess *p;
    CHECK(lb_atoms_find(world->space->atoms,"hash_slice",10,&module)==LB_ATOM_OK);
    CHECK(lb_atoms_find(world->space->atoms,"identity",8,&function)==LB_ATOM_OK);
    CHECK(lb_code_entry_acquire(world->space,module,function,1,&entry)==LB_CODE_OK);
    CHECK(lb_process_create(entry,&input,1,64,&p)==LB_PROCESS_READY); lb_code_entry_release(entry);
    const uint64_t values[]={0,1,32767,32768,268435455,268435459,0,1,262143,262144,UINT64_C(1)<<32};
    const uint64_t ranges[]={UINT64_C(1)<<32,UINT64_C(1)<<27,1,2,3,16777216,4294967295};
    for(size_t i=0;i<sizeof(values)/sizeof(values[0]);++i) {
        /* Representation vectors, not live/fabricated actor services. */
        p->reg[0]=i<6?(values[i]<<_TAG_IMMED1_SIZE)|_TAG_IMMED1_PID:lb_reference_write(p->htop,values[i]);
        if(i>=6) p->htop+=LB_REF_WORDS;
        for(size_t r=0;r<sizeof(ranges)/sizeof(ranges[0]);++r) {
            p->reg[1]=make_small((Sint)ranges[r]);
            Eterm result=r==1?lb_bif_phash2_1(p,p->reg,NULL):lb_bif_phash2_2(p,p->reg,NULL);
            CHECK(is_small(result)); if(print) printf("%s%llu",r?":":"",(unsigned long long)signed_val(result));
        }
        if(print) putchar('\n');
    }
    lb_process_destroy(p); lb_engine_leave(world->engine);
}
static void components(LbWorld *world,Memory *m)
{
    lb_engine_enter(world->engine);
    LbCodeEntry *entry; Eterm module,function,input=NIL; LbProcess *p;
    CHECK(lb_atoms_find(world->space->atoms,"hash_slice",10,&module)==LB_ATOM_OK);
    CHECK(lb_atoms_find(world->space->atoms,"identity",8,&function)==LB_ATOM_OK);
    CHECK(lb_code_entry_acquire(world->space,module,function,1,&entry)==LB_CODE_OK);
    unaligned(entry,m);
    CHECK(lb_process_create(entry,&input,1,2048,&p)==LB_PROCESS_READY); lb_code_entry_release(entry);
    Eterm *tuple=p->htop; tuple[0]=make_arityval(512);
    for(size_t i=1;i<=512;++i) tuple[i]=make_small((Sint)i);
    p->htop+=513; p->reg[0]=make_boxed(tuple); p->reg[1]=make_small(UINT64_C(1)<<32); p->heap_reserved=7;
    size_t live=m->live; Eterm root=p->reg[0]; Eterm *heap=p->heap;
    Eterm expected=lb_bif_phash2_2(p,p->reg,NULL); CHECK(is_small(expected));
    const Eterm invalid_ranges[]={make_small(0),make_small(-1),make_small(INT64_C(4294967297)),NIL,am_true};
    for(size_t i=0;i<sizeof(invalid_ranges)/sizeof(invalid_ranges[0]);++i) {
        size_t calls=m->calls; p->reg[1]=invalid_ranges[i]; p->freason=LB_EXC_NULL;
        CHECK(is_non_value(lb_bif_phash2_2(p,p->reg,NULL)) && p->freason==LB_EXC_BADARG);
        CHECK(m->calls==calls && p->heap==heap && p->reg[0]==root);
    }
    p->reg[1]=make_small(UINT64_C(1)<<32); p->freason=LB_EXC_NULL;
    for(size_t prefix=1;prefix<=4;++prefix) {
        lb_alloc_domain_begin_operation(&world->domain); p->status=LB_PROCESS_READY; m->fail=m->calls+prefix;
        Eterm result=lb_bif_phash2_2(p,p->reg,NULL); m->fail=0;
        if(prefix<=3) CHECK(is_non_value(result) && p->status==LB_PROCESS_NO_MEMORY);
        else CHECK(result==expected);
        CHECK(m->live==live && p->heap==heap && p->reg[0]==root && !p->collections && p->heap_reserved==7);
    }
    lb_alloc_domain_begin_operation(&world->domain); p->status=LB_PROCESS_READY;
    size_t saved_limit=world->domain.limit,calls=m->calls;
    CHECK(world->domain.current+8192<saved_limit); world->domain.limit=world->domain.current+8192;
    CHECK(is_non_value(lb_bif_phash2_2(p,p->reg,NULL)) && lb_alloc_domain_failure(&world->domain)==LB_ALLOC_LIMIT);
    CHECK(m->calls==calls+1 && m->live==live && p->heap==heap && p->reg[0]==root);
    world->domain.limit=saved_limit;
    uint64_t cost=0;
    for(unsigned mode=0;mode<4;++mode) {
        lb_alloc_domain_begin_operation(&world->domain); p->status=LB_PROCESS_READY; p->freason=LB_EXC_NULL;
        LbWorkBudget *budget=lb_work_budget_create(world); CHECK(budget);
        uint64_t credit=mode==0?1000000:mode==1?cost:mode==2?cost-1:0;
        budget->limit=budget->remaining=credit; lb_work_domain_borrow(&world->domain,budget);
        Eterm result=lb_bif_phash2_2(p,p->reg,NULL);
        if(!mode) { CHECK(result==expected); cost=credit-budget->remaining; CHECK(cost>512); }
        else if(mode==1) CHECK(result==expected && !budget->remaining);
        else CHECK(is_non_value(result) && p->status==LB_PROCESS_WORK_LIMIT && p->freason==LB_EXC_NULL);
        CHECK(p->heap==heap && p->reg[0]==root && !p->collections && p->heap_reserved==7);
        lb_work_domain_borrow(&world->domain,NULL); lb_work_domain_clear_failure(&world->domain);
        lb_work_budget_release(budget); CHECK(m->live==live);
    }
    lb_alloc_domain_begin_operation(&world->domain); lb_process_destroy(p); lb_engine_leave(world->engine);
    puts("CORE_HASH_RESOURCE_OK scratch_prefix_rollback=true exact_work=true one_below=true no_gc=true reservation_preserved=true");
}
int main(int argc,char **argv)
{
    int reference=argc==3 && !strcmp(argv[1],"--reference"); CHECK(argc==2 || reference);
    FILE *f=fopen(argv[reference?2:1],"rb"); CHECK(f && !fseek(f,0,SEEK_END)); long size=ftell(f);
    CHECK(size>0 && !fseek(f,0,SEEK_SET)); unsigned char *bytes=malloc((size_t)size);
    CHECK(bytes && fread(bytes,1,(size_t)size,f)==(size_t)size && !fclose(f));
    Memory memory={0}; LbSystemAllocator allocator={&memory,allocate,release}; LbEngine *engine; LbWorld *world;
    CHECK(lb_engine_create(&allocator,&engine)==LB_ENGINE_OK && lb_world_create(engine,&world)==LB_WORLD_OK);
    LbWorldLoadReport report={0}; LbWorldStatus status=lb_world_load_report(world,(LbBytesView){bytes,(size_t)size},&report);
    if(status!=LB_WORLD_OK) fprintf(stderr,"load status=%d stage=%s offset=%zu\n",status,report.stage?report.stage:"unknown",report.offset);
    CHECK(status==LB_WORLD_OK); memset(bytes,0xa5,(size_t)size); free(bytes);
    if(!reference) components(world,&memory);
    identity_vectors(world,reference);
    const char *sizes[]={"0","1","3","4","7","8","11","12","13","23","24","25","63","64","65","257"};
    size_t rows=0;
    /* Each real invocation retains ordinary default limits. Partition the
     * oracle workload instead of increasing the budget for a giant batch. */
    for(size_t batch=0;batch<=sizeof(sizes)/sizeof(sizes[0]);++batch) {
        LbCall *call; LbBytesView result;
        CHECK((batch?lb_world_call:lb_world_start)(world,text("hash_slice"),text(batch?"all":"base"),text(batch?sizes[batch-1]:""),&call)==LB_WORLD_OK);
        for(size_t i=0;i<10000;++i) { status=lb_call_poll(call,&result); if(status!=LB_WORLD_PENDING) break; tick(); }
        if(status!=LB_WORLD_OK) fprintf(stderr,"execution batch=%zu status=%d\n",batch,status);
        CHECK(status==LB_WORLD_OK); size_t count=0;
        for(size_t i=0;i<result.size;++i) if(result.data[i]=='\n') ++count;
        CHECK(count==(batch?64:36)); rows+=count;
        if(reference) CHECK(fwrite(result.data,1,result.size,stdout)==result.size);
        CHECK(lb_call_consume(call)==LB_WORLD_OK); lb_call_release(call);
    }
    CHECK(rows==1060);
    LbReclamation *r; CHECK(lb_world_stop(world,&r)==LB_WORLD_OK);
    for(size_t i=0;i<10000 && lb_reclamation_poll(r)==LB_WORLD_PENDING;++i) tick();
    CHECK(lb_reclamation_consume(r)==LB_WORLD_OK); lb_reclamation_release(r); lb_world_release(world);
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine); CHECK(!memory.live);
    if(!reference) puts("CORE_HASH_EXECUTION_OK rows=1071 ranges=7 real_beam=true owned_input=true reclaimed=true");
}
