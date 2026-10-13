/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#include "process_internal.h"
#include "world_internal.h"
#include "work_budget_internal.h"
#include "alloc_internal.h"
#include "map_hamt.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while(0)
static LbCodeModule *load(LbWorld *w,const void *bytes,size_t size)
{ LbCodeModule *m; LbBeamError error; CHECK(lb_code_load(w->space,bytes,size,&m,&error)==LB_CODE_OK); return m; }
static LbProcess *create(LbWorld *w,LbCodeModule *m,const void *bytes,size_t size)
{
    Eterm function; LbCodeEntry *entry; LbProcess *p;
    CHECK(lb_atoms_find(w->space->atoms,"decode",6,&function)==LB_ATOM_OK);
    CHECK(lb_code_entry_acquire(w->space,m->name,function,1,&entry)==LB_CODE_OK);
    CHECK(lb_process_create_binary(entry,bytes,size,2048,&p)==LB_PROCESS_READY); lb_code_entry_release(entry); return p;
}
static void enter(LbProcess *p)
{ for(unsigned i=0;i<100 && p->i[0]!=op_call_bif_W;++i) CHECK(lb_process_run(p,1,100)==LB_PROCESS_YIELDED); CHECK(p->i[0]==op_call_bif_W); }
static void fill(LbProcess *p) { while(p->htop<p->stop-S_RESERVED-p->heap_reserved) *p->htop++=NIL; }
static LbProcessStatus run(LbProcess *p)
{
    LbProcessStatus status=LB_PROCESS_READY;
    for(unsigned i=0;i<1000 && (status==LB_PROCESS_READY || status==LB_PROCESS_YIELDED);++i) status=lb_process_run(p,1,100);
    return status;
}
static const unsigned char data[]={131,104,2,119,2,'o','k',109,0,0,0,3,1,2,3,255};
static const unsigned char fresh[]={131,104,2,119,17,'d','e','c','o','d','e','_','f','r','e','s','h','_','a','t','o','m',97,42};
static void value(LbWorld *w,Eterm result,int is_fresh)
{
    CHECK(is_tuple(result) && !is_literal_ptr(result) && arityval(*tuple_val(result))==2);
    Eterm *t=tuple_val(result); LbBeamBytes name;
    CHECK(lb_atoms_name(w->space->atoms,t[1],&name)==LB_ATOM_OK);
    if(is_fresh) { CHECK(name.size==17 && !memcmp(name.data,"decode_fresh_atom",17) && t[2]==make_small(42)); }
    else {
        CHECK(t[1]==am_ok); LbBitstringView view;
        CHECK(!is_literal_ptr(t[2]) && lb_bitstring_view(t[2],&view) && view.bit_size==24 && !memcmp(view.data,"\1\2\3",3));
    }
}
typedef struct { LbSystemAllocator backing; size_t left; } Prefix;
static void *allocate(void *context,size_t bytes)
{ Prefix *p=context; if(!p->left) return NULL; --p->left; return p->backing.allocate(p->backing.context,bytes); }
static void release(void *context,void *ptr)
{ Prefix *p=context; p->backing.release(p->backing.context,ptr); }
static void allocations(LbWorld *w,LbCodeModule *m)
{
    LbAllocDomain *d=w->space->domain; size_t atoms=lb_atoms_count(w->space->atoms),baseline=d->current,blocks=d->block_count; int done=0;
    for(size_t prefix=0;prefix<60 && !done;++prefix) {
        LbProcess *p=create(w,m,fresh,sizeof(fresh)); enter(p); fill(p);
        size_t limit=d->limit; Prefix state={d->system,prefix?prefix-1:0};
        if(prefix) d->system=(LbSystemAllocator){&state,allocate,release}; else d->limit=d->current;
        LbProcessStatus status=run(p); d->system=state.backing; d->limit=limit;
        if(status==LB_PROCESS_DONE) { CHECK(prefix>8 && p->collections>0); value(w,lb_process_result(p),1); CHECK(lb_atoms_count(w->space->atoms)==atoms+1); done=1; }
        else {
            CHECK(status==(prefix?LB_PROCESS_NO_MEMORY:LB_PROCESS_LIMIT) && lb_atoms_count(w->space->atoms)==atoms);
            LbBitstringView input; CHECK(lb_bitstring_view(p->reg[0],&input) && input.bit_size==8*sizeof(fresh) && !memcmp(input.data,fresh,sizeof(fresh)));
        }
        lb_alloc_domain_begin_operation(d); lb_process_destroy(p);
        if(!done) CHECK(d->current==baseline && d->block_count==blocks);
    }
    CHECK(done);
}
typedef enum { DECODE_PRIMITIVE, DECODE_RETURN_PATH } DecodeBoundary;
static uint64_t measured(LbWorld *w,LbCodeModule *m,const unsigned char *bytes,size_t size,int gc,DecodeBoundary boundary)
{
    LbProcess *p=create(w,m,bytes,size); enter(p); if(gc) fill(p);
    LbWorkBudget *budget=lb_work_budget_create(w); CHECK(budget); budget->limit=budget->remaining=10000000;
    lb_work_domain_borrow(w->space->domain,budget);
    if(boundary==DECODE_PRIMITIVE) CHECK(!is_non_value(lb_bif_binary_to_term_1(p,p->reg,NULL)));
    else CHECK(run(p)==LB_PROCESS_DONE);
    uint64_t used=budget->limit-budget->remaining;
    lb_work_domain_borrow(w->space->domain,NULL); lb_process_destroy(p); lb_work_budget_release(budget); return used;
}
static void work_prefixes(LbWorld *w,LbCodeModule *m,uint64_t required,const unsigned char *bytes,size_t size,int is_fresh,int gc,DecodeBoundary boundary)
{
    LbAllocDomain *d=w->space->domain; size_t baseline=d->current,atoms=lb_atoms_count(w->space->atoms),blocks=d->block_count;
    CHECK(required>1 && required<1000000);
    for(uint64_t credit=0;credit<=required;++credit) {
        LbProcess *p=create(w,m,bytes,size); enter(p); if(gc) fill(p);
        LbWorkBudget *budget=lb_work_budget_create(w); CHECK(budget); budget->limit=budget->remaining=credit;
        lb_work_domain_borrow(d,budget); Eterm output;
        if(boundary==DECODE_PRIMITIVE) output=lb_bif_binary_to_term_1(p,p->reg,NULL);
        else { (void)run(p); output=lb_process_result(p); }
        if(credit<required) CHECK(is_non_value(output) && p->status==LB_PROCESS_WORK_LIMIT && lb_atoms_count(w->space->atoms)==atoms);
        else { CHECK(!is_non_value(output) && !budget->remaining); value(w,output,is_fresh); }
        lb_work_domain_borrow(d,NULL); lb_work_domain_clear_failure(d); lb_alloc_domain_begin_operation(d);
        lb_process_destroy(p); lb_work_budget_release(budget);
        if(credit<required || !is_fresh) CHECK(d->current==baseline && d->block_count==blocks);
    }
}
static void semantics(LbWorld *w,LbCodeModule *m)
{
    size_t baseline=w->space->domain->current;
    for(unsigned gc=0;gc<2;++gc) {
        LbProcess *p=create(w,m,data,sizeof(data)); enter(p); if(gc) fill(p);
        CHECK(run(p)==LB_PROCESS_DONE); value(w,lb_process_result(p),0);
        CHECK(lb_process_collect(p,0)==LB_PROCESS_DONE); value(w,lb_process_result(p),0); lb_process_destroy(p);
    }
    for(size_t n=0;n<sizeof(data)-1;++n) {
        LbProcess *p=create(w,m,data,n); CHECK(run(p)==LB_PROCESS_EXCEPTION && p->exception_value==am_badarg); lb_process_destroy(p);
    }
    const unsigned char refused[]={131,80,0,0,0,1},invalid[]={131,255};
    LbProcess *p=create(w,m,refused,sizeof(refused)); CHECK(run(p)==LB_PROCESS_REFUSED); lb_process_destroy(p);
    p=create(w,m,invalid,sizeof(invalid)); CHECK(run(p)==LB_PROCESS_EXCEPTION && p->exception_value==am_badarg); lb_process_destroy(p);
    Eterm nonbinary[]={NIL,am_ok,make_small(42)};
    for(size_t i=0;i<3;++i) { p=create(w,m,data,sizeof(data)); p->reg[0]=nonbinary[i]; CHECK(run(p)==LB_PROCESS_EXCEPTION && p->exception_value==am_badarg); lb_process_destroy(p); }
    size_t atoms=lb_atoms_count(w->space->atoms);
    unsigned char malformed[sizeof(fresh)]; memcpy(malformed,fresh,sizeof(fresh)); malformed[sizeof(fresh)-2]=255;
    p=create(w,m,malformed,sizeof(malformed)); CHECK(run(p)==LB_PROCESS_EXCEPTION && p->exception_value==am_badarg && lb_atoms_count(w->space->atoms)==atoms); lb_process_destroy(p);
    p=create(w,m,data,sizeof(data)); enter(p); p->heap_reserved=19; fill(p);
    Eterm result=lb_bif_binary_to_term_1(p,p->reg,NULL); CHECK(!is_non_value(result) && p->collections>0 && p->heap_reserved==19);
    value(w,result,0); lb_process_destroy(p);
    const unsigned char duplicate[]={131,116,0,0,0,2,97,1,97,2,97,1,97,3};
    p=create(w,m,duplicate,sizeof(duplicate)); CHECK(run(p)==LB_PROCESS_EXCEPTION && p->exception_value==am_badarg); lb_process_destroy(p);
    unsigned char map[138]={131,116,0,0,0,33};
    for(unsigned i=0;i<33;++i) { map[6+4*i]=97; map[7+4*i]=(unsigned char)i; map[8+4*i]=97; map[9+4*i]=(unsigned char)(i+1); }
    p=create(w,m,map,sizeof(map)); enter(p); fill(p); CHECK(run(p)==LB_PROCESS_DONE && p->collections>0);
    CHECK(lb_process_collect(p,0)==LB_PROCESS_DONE); Eterm found;
    CHECK(lb_hamt_get(p,lb_process_result(p),make_small(32),&found)==LB_HAMT_OK && found==make_small(33)); lb_process_destroy(p);
    /* A byte-length binary can start at bit offset three. Use a real refcounted
     * backing and native sub-bits metadata, not an aligned-only shortcut. */
    unsigned char shifted[100]={0};
    for(size_t bit=0;bit<sizeof(data)*8;++bit) if(data[bit/8]&(0x80u>>(bit%8))) shifted[(bit+3)/8]|=(unsigned char)(0x80u>>((bit+3)%8));
    p=create(w,m,shifted,sizeof(shifted)); LbSubBits *sub=(LbSubBits *)boxed_val(p->reg[0]); sub->start=3; sub->end=3+sizeof(data)*8;
    CHECK(run(p)==LB_PROCESS_DONE); value(w,lb_process_result(p),0); lb_process_destroy(p);
    p=create(w,m,shifted,sizeof(shifted)); sub=(LbSubBits *)boxed_val(p->reg[0]); sub->end--;
    CHECK(run(p)==LB_PROCESS_EXCEPTION && p->exception_value==am_badarg); lb_process_destroy(p);
    CHECK(w->space->domain->current==baseline);
}
static void later_denial(LbWorld *w,LbCodeModule *m)
{
    unsigned char encoded[sizeof(fresh)]; memcpy(encoded,fresh,sizeof(fresh)); memcpy(encoded+5,"decode_after_atom",17);
    size_t atoms=lb_atoms_count(w->space->atoms); LbProcess *p=create(w,m,encoded,sizeof(encoded)); enter(p);
    LbWorkBudget *budget=lb_work_budget_create(w); CHECK(budget); budget->limit=budget->remaining=10000000;
    lb_work_domain_borrow(w->space->domain,budget);
    CHECK(lb_process_run(p,1,1000)==LB_PROCESS_YIELDED && is_tuple(p->reg[0]) && lb_atoms_count(w->space->atoms)==atoms+1);
    budget->remaining=0; CHECK(run(p)==LB_PROCESS_WORK_LIMIT && lb_atoms_count(w->space->atoms)==atoms+1);
    lb_work_domain_borrow(w->space->domain,NULL); lb_work_domain_clear_failure(w->space->domain); lb_alloc_domain_begin_operation(w->space->domain);
    lb_process_destroy(p); lb_work_budget_release(budget);
}
static void close_world(LbWorld *w)
{
    LbReclamation *r; CHECK(lb_world_stop(w,&r)==LB_WORLD_OK && lb_reclamation_poll(r)==LB_WORLD_OK && lb_reclamation_consume(r)==LB_WORLD_OK);
    lb_reclamation_release(r); lb_world_release(w);
}
int main(int argc,char **argv)
{
    FILE *f; CHECK(argc==2 && (f=fopen(argv[1],"rb")) && !fseek(f,0,SEEK_END)); long size=ftell(f); CHECK(size>0 && !fseek(f,0,SEEK_SET));
    unsigned char *image=malloc((size_t)size); CHECK(image && fread(image,1,(size_t)size,f)==(size_t)size && !fclose(f));
    LbEngine *engine; LbWorld *w,*probe,*subject;
    CHECK(lb_engine_create(NULL,&engine)==LB_ENGINE_OK && lb_world_create(engine,&w)==LB_WORLD_OK && lb_world_create(engine,&probe)==LB_WORLD_OK && lb_world_create(engine,&subject)==LB_WORLD_OK);
    lb_engine_enter(engine); LbCodeModule *m=load(w,image,(size_t)size),*pm=load(probe,image,(size_t)size),*sm=load(subject,image,(size_t)size); free(image);
    semantics(w,m);
    for(int gc=0;gc<2;++gc) { uint64_t cost=measured(w,m,data,sizeof(data),gc,DECODE_RETURN_PATH); work_prefixes(w,m,cost,data,sizeof(data),0,gc,DECODE_RETURN_PATH); }
    uint64_t fresh_cost=measured(probe,pm,fresh,sizeof(fresh),1,DECODE_PRIMITIVE);
    work_prefixes(subject,sm,fresh_cost,fresh,sizeof(fresh),1,1,DECODE_PRIMITIVE); allocations(w,m); later_denial(w,m);
    lb_engine_leave(engine); close_world(subject); close_world(probe); close_world(w);
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine);
    puts("CORE_EXTERNAL_DECODE_OK actual_bif=true data_maps=true trailing=true unaligned=true malformed=true compressed_refused=true transactional_atoms=true gc=true allocation_work_prefixes=true balanced=true"); return 0;
}
