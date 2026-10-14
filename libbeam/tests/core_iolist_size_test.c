/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#define _POSIX_C_SOURCE 200809L
#include "world_internal.h"
#include "process_internal.h"
#include "work_budget_internal.h"
#include "iolist_size.h"
#include "big.h"
#include "opcodes.h"
#include <stdio.h>
#include <time.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"CHECK %d: %s\n",__LINE__,#x); exit(1); } } while(0)
typedef struct { size_t calls,fail,live,bytes; } Memory;
typedef union { max_align_t alignment; size_t size; } Header;
static void *allocate(void *context,size_t bytes)
{
    Memory *m=context; if(++m->calls==m->fail) return NULL;
    Header *h=malloc(sizeof(*h)+bytes); if(!h) return NULL;
    h->size=bytes; ++m->live; m->bytes+=bytes; return h+1;
}
static void release(void *context,void *pointer)
{
    Memory *m=context; Header *h=(Header *)pointer-1;
    CHECK(m->live && m->bytes>=h->size); --m->live; m->bytes-=h->size;
    memset(pointer,0xa5,h->size); free(h);
}
static LbBytesView text(const char *s) { return (LbBytesView){(const unsigned char *)s,strlen(s)}; }
static void tick(void) { struct timespec delay={0,1000000}; nanosleep(&delay,NULL); }
static void fused_collection(LbWorld *world,Eterm module)
{
    Eterm function; LbCodeEntry *entry; LbProcess *p;
    CHECK(lb_atoms_find(world->space->atoms,"repeat",6,&function)==LB_ATOM_OK);
    CHECK(lb_code_entry_acquire(world->space,module,function,2,&entry)==LB_CODE_OK);
    unsigned char bytes[80]; memset(bytes,0x37,sizeof(bytes));
    Eterm args[2]={make_small(2),NIL};
    CHECK(lb_process_create(entry,args,2,128,&p)==LB_PROCESS_READY); lb_code_entry_release(entry);
    CHECK(lb_bitstring_build(&world->domain,&p->off_heap,&p->htop,bytes,sizeof(bytes)*8,0,&p->reg[1])==LB_ALLOC_OK);
    LbProcessStatus status=LB_PROCESS_READY; unsigned forced=0;
    for(unsigned step=0;step<1000 && status!=LB_PROCESS_DONE;++step) {
        int collect=p->i[0]==op_test_heap_1_put_list_Iy; size_t before=p->collections;
        if(collect) { while(p->htop<p->stop-S_RESERVED) *p->htop++=NIL; ++forced; }
        status=lb_process_run(p,1,100);
        CHECK(status==LB_PROCESS_YIELDED || status==LB_PROCESS_DONE);
        if(collect) CHECK(p->collections==before+1);
    }
    CHECK(status==LB_PROCESS_DONE && forced==2);
    Eterm list=lb_process_result(p);
    for(unsigned i=0;i<2;++i) {
        CHECK(is_list(list)); LbBitstringView view;
        CHECK(lb_bitstring_view(CAR(list_val(list)),&view) && view.bit_size==sizeof(bytes)*8 && !memcmp(view.data,bytes,sizeof(bytes)));
        list=CDR(list_val(list));
    }
    CHECK(is_nil(list)); lb_process_destroy(p);
}
static void components(LbWorld *world,Memory *m)
{
    lb_engine_enter(world->engine);
    Eterm module,function; LbCodeEntry *entry; LbProcess *p;
    CHECK(lb_atoms_find(world->space->atoms,"iolist_size_slice",17,&module)==LB_ATOM_OK);
    CHECK(lb_atoms_find(world->space->atoms,"identity",8,&function)==LB_ATOM_OK);
    CHECK(lb_code_entry_acquire(world->space,module,function,1,&entry)==LB_CODE_OK);
    unsigned char data[80]; memset(data,0x96,sizeof(data)); size_t baseline=m->live;
    for(unsigned offset=0;offset<8;++offset) for(unsigned tail=0;tail<8;++tail) {
        CHECK(lb_process_create_binary(entry,data,sizeof(data),128,&p)==LB_PROCESS_READY);
        LbSubBits *view=(LbSubBits *)boxed_val(p->reg[0]); CHECK(view->thing_word==LB_HEADER_SUB_BITS);
        view->start=offset; view->end=offset+65*8+tail;
        Eterm result=lb_bif_iolist_size_1(p,p->reg,NULL);
        if(tail) CHECK(is_non_value(result) && p->freason==LB_EXC_BADARG);
        else {
            CHECK(result==make_small(65)); Eterm binary=p->reg[0];
            Eterm list=CONS(p->htop,binary,binary); p->htop+=2;
            p->reg[0]=CONS(p->htop,make_small(255),list); p->htop+=2;
            CHECK(lb_bif_iolist_size_1(p,p->reg,NULL)==make_small(131));
        }
        lb_process_destroy(p); CHECK(m->live==baseline);
    }
    CHECK(lb_process_create_binary(entry,data,sizeof(data),2048,&p)==LB_PROCESS_READY); lb_code_entry_release(entry);
    Eterm binary=p->reg[0]; size_t calls=m->calls; m->fail=calls+1;
    CHECK(lb_bif_iolist_size_1(p,p->reg,NULL)==make_small(sizeof(data)));
    p->reg[0]=NIL; CHECK(lb_bif_iolist_size_1(p,p->reg,NULL)==make_small(0));
    p->reg[0]=make_small(3); CHECK(is_non_value(lb_bif_iolist_size_1(p,p->reg,NULL)) && p->freason==LB_EXC_BADARG);
    CHECK(m->calls==calls); m->fail=0; p->freason=LB_EXC_NULL;
    p->reg[0]=binary;
    for(unsigned i=0;i<512;++i) { p->reg[0]=CONS(p->htop,p->reg[0],NIL); p->htop+=2; }
    p->heap_reserved=7; Eterm root=p->reg[0]; Eterm *heap=p->heap; size_t live=m->live,bytes=m->bytes;
    LbAllocDomain *domain=&world->domain; size_t prefixes=0;
    for(size_t prefix=1;;++prefix) {
        CHECK(prefix<20); size_t before=m->calls; m->fail=before+prefix;
        p->status=LB_PROCESS_READY; lb_alloc_domain_begin_operation(domain);
        Eterm result=lb_bif_iolist_size_1(p,p->reg,NULL); m->fail=0;
        if(is_non_value(result)) CHECK(p->status==LB_PROCESS_NO_MEMORY && p->freason==LB_EXC_NULL);
        else CHECK(result==make_small(sizeof(data)));
        CHECK(m->live==live && m->bytes==bytes && p->heap==heap && p->reg[0]==root && !p->collections && p->heap_reserved==7);
        ++prefixes; if(!is_non_value(result) && prefix>m->calls-before) break;
    }
    lb_alloc_domain_begin_operation(domain); p->status=LB_PROCESS_READY;
    size_t limit=domain->limit; domain->limit=domain->current;
    CHECK(is_non_value(lb_bif_iolist_size_1(p,p->reg,NULL)) && p->status==LB_PROCESS_LIMIT && p->freason==LB_EXC_NULL);
    CHECK(m->live==live && m->bytes==bytes); domain->limit=limit; lb_alloc_domain_begin_operation(domain);
    uint64_t cost=0;
    for(unsigned mode=0;mode<4;++mode) {
        LbWorkBudget *budget=lb_work_budget_create(world); CHECK(budget);
        uint64_t credit=mode==0?1000000:mode==1?cost:mode==2?cost-1:0;
        budget->limit=budget->remaining=credit; p->status=LB_PROCESS_READY;
        lb_work_domain_borrow(domain,budget); Eterm result=lb_bif_iolist_size_1(p,p->reg,NULL);
        if(mode<2) {
            CHECK(result==make_small(sizeof(data)));
            if(!mode) { cost=credit-budget->remaining; CHECK(cost>1); } else CHECK(!budget->remaining);
        } else CHECK(is_non_value(result) && p->status==LB_PROCESS_WORK_LIMIT && p->freason==LB_EXC_NULL);
        lb_work_domain_borrow(domain,NULL); lb_work_domain_clear_failure(domain); lb_alloc_domain_begin_operation(domain);
        lb_work_budget_release(budget);
        CHECK(m->live==live && m->bytes==bytes && p->heap==heap && p->reg[0]==root && !p->collections);
    }
    /* Arithmetic boundary witnesses without pretending to allocate enormous
     * binaries or execute billions of guest list visits. */
    LbIolistSize size={UINTPTR_MAX,0}; CHECK(lb_iolist_size_add(&size,1) && !size.low && size.high==1);
    LbIolistSize overflow={UINTPTR_MAX,UINTPTR_MAX}; CHECK(!lb_iolist_size_add(&overflow,1) && overflow.low==UINTPTR_MAX && overflow.high==UINTPTR_MAX);
    CHECK(lb_iolist_size_result(p,(LbIolistSize){MAX_SMALL,0})==make_small(MAX_SMALL));
    p->status=LB_PROCESS_READY;
    while(p->htop<p->stop-S_RESERVED) *p->htop++=NIL;
    Eterm result=lb_iolist_size_result(p,(LbIolistSize){(Uint)MAX_SMALL+1,0});
    CHECK(lb_is_big(result) && lb_big_size(result)==1 && boxed_val(result)[1]==(Uint)MAX_SMALL+1 && p->collections && p->heap!=heap);
    CHECK(lb_bif_iolist_size_1(p,p->reg,NULL)==make_small(sizeof(data)) && p->heap_reserved==7);
    result=lb_iolist_size_result(p,size); CHECK(lb_is_big(result) && lb_big_size(result)==2 && !boxed_val(result)[1] && boxed_val(result)[2]==1);
    lb_process_destroy(p); fused_collection(world,module); lb_engine_leave(world->engine);
    printf("CORE_IOLIST_SIZE_OK offsets=8 tails=8 allocation_prefixes=%zu exact_credit=true one_below=true zero=true memory_limit=true borrowed_roots_stable=true moving_result=true wide_counter=true\n",prefixes);
}
int main(int argc,char **argv)
{
    FILE *file; CHECK(argc==2 && (file=fopen(argv[1],"rb")) && !fseek(file,0,SEEK_END));
    long length=ftell(file); CHECK(length>0 && !fseek(file,0,SEEK_SET)); unsigned char *bytes=malloc((size_t)length);
    CHECK(bytes && fread(bytes,1,(size_t)length,file)==(size_t)length && !fclose(file));
    Memory memory={0}; LbSystemAllocator allocator={&memory,allocate,release}; LbEngine *engine; LbWorld *world;
    CHECK(lb_engine_create(&allocator,&engine)==LB_ENGINE_OK && lb_world_create(engine,&world)==LB_WORLD_OK);
    LbWorldLoadReport report={0}; LbWorldStatus status=lb_world_load_report(world,(LbBytesView){bytes,(size_t)length},&report);
    if(status!=LB_WORLD_OK) fprintf(stderr,"load=%d stage=%s offset=%zu\n",status,report.stage?report.stage:"",report.offset);
    CHECK(status==LB_WORLD_OK); free(bytes); components(world,&memory);
    LbCall *call; LbBytesView result;
    CHECK(lb_world_start(world,text("iolist_size_slice"),text("cases"),text("size"),&call)==LB_WORLD_OK);
    for(size_t i=0;i<10000 && (status=lb_call_poll(call,&result))==LB_WORLD_PENDING;++i) tick();
    if(status!=LB_WORLD_OK) fprintf(stderr,"execution=%d\n",status);
    CHECK(status==LB_WORLD_OK && result.size==4 && !memcmp(result.data,"size",4));
    CHECK(lb_call_consume(call)==LB_WORLD_OK); lb_call_release(call);
    LbReclamation *reclamation; CHECK(lb_world_stop(world,&reclamation)==LB_WORLD_OK);
    for(size_t i=0;i<10000 && lb_reclamation_poll(reclamation)==LB_WORLD_PENDING;++i) tick();
    CHECK(lb_reclamation_consume(reclamation)==LB_WORLD_OK); lb_reclamation_release(reclamation); lb_world_release(world);
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine); CHECK(!memory.live && !memory.bytes);
}
