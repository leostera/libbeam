/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#include "process_internal.h"
#include "world_internal.h"
#include "work_budget_internal.h"
#include "alloc_internal.h"
#include "binary.h"
#include "writable.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while(0)
typedef struct { size_t calls,live,bytes; int deny; } Memory;
typedef union { max_align_t alignment; size_t size; } Header;
static void *allocate(void *context,size_t bytes)
{
    Memory *m=context; ++m->calls; if(m->deny || bytes>SIZE_MAX-sizeof(Header)) return NULL;
    Header *h=malloc(sizeof(*h)+bytes); if(!h) return NULL; h->size=bytes; ++m->live; m->bytes+=bytes; return h+1;
}
static void release(void *context,void *pointer)
{
    Memory *m=context; Header *h=(Header *)pointer-1; CHECK(m->live && m->bytes>=h->size);
    --m->live; m->bytes-=h->size; memset(pointer,0xa5,h->size); free(h);
}
static LbCodeModule *load(LbWorld *w,const char *path)
{
    FILE *f=fopen(path,"rb"); CHECK(f && !fseek(f,0,SEEK_END)); long size=ftell(f); CHECK(size>0 && !fseek(f,0,SEEK_SET));
    unsigned char *bytes=malloc((size_t)size); CHECK(bytes && fread(bytes,1,(size_t)size,f)==(size_t)size && !fclose(f));
    LbCodeModule *module; LbBeamError error; LbCodeStatus status=lb_code_load(w->space,bytes,(size_t)size,&module,&error);
    if(status!=LB_CODE_OK) fprintf(stderr,"load=%d stage=%s offset=%zu\n",status,error.stage,error.offset);
    CHECK(status==LB_CODE_OK); free(bytes); return module;
}
static LbProcess *create(LbWorld *w,LbCodeModule *module,const char *name)
{
    Eterm function,input=NIL; LbCodeEntry *entry; LbProcess *p;
    CHECK(lb_atoms_find(w->space->atoms,name,strlen(name),&function)==LB_ATOM_OK);
    CHECK(lb_code_entry_acquire(w->space,module->name,function,1,&entry)==LB_CODE_OK);
    CHECK(lb_process_create(entry,&input,1,256,&p)==LB_PROCESS_READY); lb_code_entry_release(entry); return p;
}
static LbProcessStatus run(LbProcess *p)
{
    LbProcessStatus status=LB_PROCESS_READY;
    for(size_t i=0;i<10000 && (status==LB_PROCESS_READY || status==LB_PROCESS_YIELDED);++i) status=lb_process_run(p,1,100);
    return status;
}
static void query(LbWorld *w,LbProcess *p,Memory *m,size_t bits)
{
    LbAllocDomain *d=&w->domain; Eterm value=p->reg[0],*top=p->htop; size_t limit=d->limit,used=d->current;
    p->heap_reserved=7;
    for(unsigned credit=0;credit<2;++credit) {
        LbWorkBudget *budget=lb_work_budget_create(w); CHECK(budget); d->limit=d->current;
        size_t calls=m->calls; m->deny=1;
        p->status=LB_PROCESS_READY; p->freason=LB_EXC_NULL; budget->limit=budget->remaining=credit; lb_work_domain_borrow(d,budget);
        Eterm result=lb_bif_bit_size_1(p,p->reg,NULL);
        if(!credit) CHECK(is_non_value(result) && p->status==LB_PROCESS_WORK_LIMIT && p->freason==LB_EXC_NULL && !p->last_native_cost);
        else CHECK(result==make_small((Sint)bits) && p->last_native_cost==1 && !budget->remaining);
        CHECK(p->reg[0]==value && p->htop==top && p->heap_reserved==7 && m->calls==calls);
        lb_work_domain_borrow(d,NULL); lb_work_domain_clear_failure(d); lb_alloc_domain_begin_operation(d);
        m->deny=0; d->limit=limit; lb_work_budget_release(budget); CHECK(d->current==used);
    }
    p->status=LB_PROCESS_READY; p->heap_reserved=0;
}
int main(int argc,char **argv)
{
    CHECK(argc==3); Memory memory={0}; LbSystemAllocator allocator={&memory,allocate,release}; LbEngine *engine; LbWorld *w;
    CHECK(lb_engine_create(&allocator,&engine)==LB_ENGINE_OK && lb_world_create(engine,&w)==LB_WORLD_OK); lb_engine_enter(engine);
    LbCodeModule *body=load(w,argv[1]),*guard=load(w,argv[2]); size_t baseline=w->domain.current;
    unsigned char bytes[1025]; memset(bytes,0x95,sizeof(bytes));
    const size_t lengths[]={0,1,7,8,9,15,16,17,503,504,505,511,512,513,519,520,521,8192};
    const char *names[]={"direct","dynamic","guarded","direct","kept"};
    for(unsigned form=0;form<5;++form) for(unsigned offset=0;offset<8;++offset)
    for(size_t n=0;n<sizeof(lengths)/sizeof(*lengths);++n) for(unsigned storage=0;storage<2;++storage) {
        if(!storage && offset) continue; /* heap bits have no physical offset */
        size_t bits=lengths[n]; LbProcess *p=create(w,form==3?guard:body,names[form]);
        CHECK(lb_bitstring_build(&w->domain,&p->off_heap,&p->htop,bytes,storage?sizeof(bytes)*8:bits,0,&p->reg[0])==LB_ALLOC_OK);
        if(storage) { LbSubBits *sub=(LbSubBits *)boxed_val(p->reg[0]); CHECK(sub->thing_word==LB_HEADER_SUB_BITS); sub->start=offset; sub->end=offset+bits; }
        p->construction_root=p->reg[0]; query(w,p,&memory,bits);
        CHECK(run(p)==LB_PROCESS_DONE); Eterm result=lb_process_result(p);
        if(form==4) { CHECK(is_tuple(result) && tuple_val(result)[2]==p->construction_root && p->collections); result=tuple_val(result)[1]; }
        CHECK(result==make_small((Sint)bits));
        CHECK(lb_process_collect(p,0)==LB_PROCESS_DONE); LbBitstringView view;
        CHECK(lb_bitstring_view(p->construction_root,&view) && view.bit_size==bits && view.bit_offset==(storage?offset:0));
        lb_process_destroy(p); CHECK(w->domain.current==baseline);
    }
    for(unsigned form=0;form<4;++form) for(unsigned kind=0;kind<7;++kind) {
        LbProcess *p=create(w,form==3?guard:body,names[form]); Eterm *hp=p->htop;
        if(kind==0) p->reg[0]=NIL;
        if(kind==1) p->reg[0]=am_true;
        if(kind==2) p->reg[0]=make_small(0);
        if(kind==3) { hp[0]=_make_header(1,FLOAT_SUBTAG); hp[1]=UINT64_C(0x3ff0000000000000); p->reg[0]=make_boxed(hp); p->htop+=2; }
        if(kind==4) { p->reg[0]=TUPLE1(hp,make_small(0)); p->htop+=2; }
        if(kind==5) { p->reg[0]=CONS(hp,make_small(0),NIL); p->htop+=2; }
        if(kind==6) { hp[0]=_make_header(2,POS_BIG_SUBTAG); hp[1]=0; hp[2]=1; p->reg[0]=make_boxed(hp); p->htop+=3; }
        size_t calls=memory.calls; memory.deny=1;
        CHECK(is_non_value(lb_bif_bit_size_1(p,p->reg,NULL)) && p->freason==LB_EXC_BADARG && memory.calls==calls);
        memory.deny=0; p->freason=LB_EXC_NULL;
        CHECK(run(p)==(form>=2?LB_PROCESS_DONE:LB_PROCESS_EXCEPTION));
        if(form>=2) CHECK(lb_process_result(p)==am_no); else CHECK(p->exception_value==am_badarg);
        lb_process_destroy(p); CHECK(w->domain.current==baseline);
    }
    for(unsigned invalid=0;invalid<2;++invalid) {
        LbProcess *p=create(w,guard,"direct");
        if(invalid) p->reg[0]=am_true;
        else CHECK(lb_bitstring_build(&w->domain,&p->off_heap,&p->htop,bytes,8,0,&p->reg[0])==LB_ALLOC_OK);
        for(unsigned step=0;step<100 && *p->i!=op_i_bif1_sfbd;++step) CHECK(lb_process_run(p,1,100)==LB_PROCESS_YIELDED);
        CHECK(*p->i==op_i_bif1_sfbd);
        LbWorkBudget *budget=lb_work_budget_create(w); CHECK(budget); budget->limit=budget->remaining=1;
        lb_work_domain_borrow(&w->domain,budget);
        CHECK(run(p)==LB_PROCESS_WORK_LIMIT && p->freason==LB_EXC_NULL && !p->last_native_cost);
        lb_work_domain_borrow(&w->domain,NULL); lb_work_domain_clear_failure(&w->domain); lb_alloc_domain_begin_operation(&w->domain);
        lb_work_budget_release(budget); lb_process_destroy(p); CHECK(w->domain.current==baseline);
    }
    const size_t capacities[]={0,64,1024};
    for(size_t n=0;n<sizeof(capacities)/sizeof(*capacities);++n) {
        LbProcess *p=create(w,body,"direct"); Eterm value; LbBitWriter writer;
        CHECK(lb_writable_init(p,capacities[n],&value)==LB_WRITABLE_OK); p->reg[0]=value;
        LbSubBits before=*(LbSubBits *)boxed_val(p->reg[0]); query(w,p,&memory,0);
        CHECK(!memcmp(&before,boxed_val(p->reg[0]),sizeof(before)));
        CHECK(lb_writable_append(p,p->reg[0],5,0,1,&writer,&value)==LB_WRITABLE_OK);
        CHECK(lb_bits_put_integer(&writer,make_small(21),5,LB_BITS_BIG_ENDIAN)==LB_BITS_OK); p->reg[0]=value;
        before=*(LbSubBits *)boxed_val(p->reg[0]); query(w,p,&memory,5);
        CHECK(!memcmp(&before,boxed_val(p->reg[0]),sizeof(before)));
        CHECK(lb_process_collect(p,0)==LB_PROCESS_READY); query(w,p,&memory,5);
        lb_process_destroy(p); CHECK(w->domain.current==baseline);
    }
    /* Real backing at the owned binary ceiling, not a forged oversized view. */
    unsigned char *large=calloc(1,LB_MAX_BINARY_BYTES); CHECK(large); LbProcess *p=create(w,body,"direct");
    CHECK(lb_bitstring_build(&w->domain,&p->off_heap,&p->htop,large,(size_t)LB_MAX_BINARY_BYTES*8,0,&p->reg[0])==LB_ALLOC_OK); free(large);
    query(w,p,&memory,(size_t)LB_MAX_BINARY_BYTES*8); CHECK(run(p)==LB_PROCESS_DONE && lb_process_result(p)==make_small((Sint)LB_MAX_BINARY_BYTES*8)); lb_process_destroy(p);
    CHECK(w->domain.current==baseline); lb_engine_leave(engine); LbReclamation *reclamation;
    CHECK(lb_world_stop(w,&reclamation)==LB_WORLD_OK && lb_reclamation_poll(reclamation)==LB_WORLD_OK && lb_reclamation_consume(reclamation)==LB_WORLD_OK);
    lb_reclamation_release(reclamation); lb_world_release(w); CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine);
    CHECK(!memory.live && !memory.bytes);
    puts("CORE_BIT_SIZE_OK body_dynamic_guards=true physical_offsets=8 partial_bits=true empty=true heap_refc=true moving_aliases=true badarg=true exact_zero_work=true allocation_free=true real_ceiling=true reclaimed=true"); return 0;
}
