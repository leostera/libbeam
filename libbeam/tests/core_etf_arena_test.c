/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#include "beam_program_internal.h"
#include "process_internal.h"
#include "world_internal.h"
#include "work_budget_internal.h"
#include "alloc_internal.h"
#include "map_hamt.h"
#include "big.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while(0)
static LbBeamProgram *arena(LbWorld *w)
{
    void *memory; CHECK(lb_alloc_domain_allocate(w->space->domain,sizeof(LbBeamProgram),&memory)==LB_ALLOC_OK);
    LbBeamProgram *p=memory; memset(p,0,sizeof(*p)); p->domain=w->space->domain; p->atoms=w->space->atoms; return p;
}
static void composite(Eterm value,int literal)
{
    CHECK(is_tuple(value) && !!is_literal_ptr(value)==literal && arityval(*tuple_val(value))==4);
    Eterm *items=tuple_val(value)+1,list=items[0];
    for(unsigned i=0;i<2;++i) { CHECK(is_list(list) && !!is_literal_ptr(list)==literal && list_val(list)[0]==make_small(97+i)); list=list_val(list)[1]; }
    CHECK(list==NIL && lb_is_big(items[1]) && !!is_literal_ptr(items[1])==literal && lb_big_size(items[1])==2);
    CHECK(boxed_val(items[1])[1]==0 && boxed_val(items[1])[2]==1);
    CHECK(is_boxed(items[2]) && !!is_literal_ptr(items[2])==literal && boxed_val(items[2])[1]==UINT64_C(0x8000000000000000));
    LbBitstringView view; CHECK(!!is_literal_ptr(items[3])==literal && lb_bitstring_view(items[3],&view) && view.bit_size==800 && !view.bit_offset);
    for(size_t i=0;i<100;++i) CHECK(view.data[i]==(unsigned char)i);
    CHECK(!is_literal_ptr(((LbSubBits *)boxed_val(items[3]))->orig)==!literal);
}
static void hamt_tags(Eterm value,int literal)
{
    if(is_list(value)) {
        CHECK(!!is_literal_ptr(value)==literal);
        hamt_tags(list_val(value)[0],literal); hamt_tags(list_val(value)[1],literal);
    } else if(is_boxed(value)) {
        CHECK(!!is_literal_ptr(value)==literal && (lb_hamt_is_root(value) || lb_hamt_is_node(value)));
        Eterm *words=boxed_val(value); size_t first=lb_hamt_is_root(value)?2:1;
        for(size_t i=first;i<lb_hamt_boxed_words(words);++i) hamt_tags(words[i],literal);
    }
}
static size_t encoded(unsigned char bytes[256])
{
    const unsigned char prefix[]={131,104,4,107,0,2,97,98,110,9,0,0,0,0,0,0,0,0,0,1,70,128,0,0,0,0,0,0,0,109,0,0,0,100};
    memcpy(bytes,prefix,sizeof(prefix)); for(size_t i=0;i<100;++i) bytes[sizeof(prefix)+i]=(unsigned char)i;
    size_t n=sizeof(prefix)+100; bytes[n]=255; return n;
}
static LbProcess *process(LbWorld *w,LbCodeModule *module)
{
    Eterm name,arg=NIL; LbCodeEntry *entry; LbProcess *p;
    CHECK(lb_atoms_find(w->space->atoms,"identity",8,&name)==LB_ATOM_OK);
    CHECK(lb_code_entry_acquire(w->space,module->name,name,1,&entry)==LB_CODE_OK);
    CHECK(lb_process_create(entry,&arg,1,256,&p)==LB_PROCESS_READY); lb_code_entry_release(entry); return p;
}
static void ownership(LbWorld *w,LbCodeModule *module,const unsigned char *bytes,size_t size)
{
    size_t baseline=w->space->domain->current;
    for(unsigned literal=0;literal<2;++literal) {
        LbBeamProgram *a=arena(w); LbBeamReader reader={bytes,size+1,0}; Eterm root;
        CHECK(lb_pdecode_etf(a,&reader,&root,literal?LB_ETF_LITERAL:LB_ETF_TEMPORARY) && reader.pos==size);
        composite(root,literal);
        if(!literal) {
            LbProcess *p=process(w,module); size_t words;
            CHECK(lb_flat_size(w->space->domain,root,&words) && lb_heap_reserve(p,words,MAX_REG));
            p->reg[0]=lb_copy_flat(root,&p->htop,&p->off_heap); CHECK(!is_non_value(p->reg[0]));
            lb_beam_program_destroy(a); a=NULL;
            CHECK(lb_process_collect(p,0)==LB_PROCESS_READY); composite(p->reg[0],0);
            CHECK(lb_process_run(p,100,1000)==LB_PROCESS_DONE); composite(lb_process_result(p),0); lb_process_destroy(p);
        }
        lb_beam_program_destroy(a); CHECK(w->space->domain->current==baseline);
    }
    /* Atom writes stay pending; parsing alone does not intern or publish them. */
    const unsigned char atom[]={131,119,5,'f','r','e','s','h'};
    LbBeamProgram *a=arena(w); LbBeamReader reader={atom,sizeof(atom),0}; Eterm root; size_t atoms=lb_atoms_count(w->space->atoms);
    CHECK(lb_pdecode_etf(a,&reader,&root,LB_ETF_TEMPORARY) && root==THE_NON_VALUE && a->name_count==1 && lb_atoms_count(w->space->atoms)==atoms);
    lb_beam_program_destroy(a);
    unsigned char map[138]={131,116,0,0,0,33};
    for(unsigned i=0;i<33;++i) { map[6+4*i]=97; map[7+4*i]=(unsigned char)i; map[8+4*i]=97; map[9+4*i]=(unsigned char)(i+1); }
    for(unsigned literal=0;literal<2;++literal) {
        a=arena(w); reader=(LbBeamReader){map,sizeof(map),0};
        CHECK(lb_pdecode_etf(a,&reader,&root,literal?LB_ETF_LITERAL:LB_ETF_TEMPORARY) && lb_pvalidate_maps(a,NULL));
        hamt_tags(root,(int)literal);
        if(!literal) {
            LbProcess *p=process(w,module); size_t words; CHECK(lb_flat_size(w->space->domain,root,&words) && lb_heap_reserve(p,words,MAX_REG));
            p->reg[0]=lb_copy_flat(root,&p->htop,&p->off_heap); CHECK(!is_non_value(p->reg[0])); lb_beam_program_destroy(a); a=NULL;
            CHECK(lb_process_collect(p,0)==LB_PROCESS_READY); hamt_tags(p->reg[0],0); Eterm value;
            CHECK(lb_hamt_get(p,p->reg[0],make_small(32),&value)==LB_HAMT_OK && value==make_small(33)); lb_process_destroy(p);
        }
        lb_beam_program_destroy(a); CHECK(w->space->domain->current==baseline);
    }
}
typedef struct { LbSystemAllocator backing; size_t left; } Prefix;
static void *allocate(void *context,size_t bytes)
{ Prefix *p=context; if(!p->left) return NULL; --p->left; return p->backing.allocate(p->backing.context,bytes); }
static void release(void *context,void *ptr)
{ Prefix *p=context; p->backing.release(p->backing.context,ptr); }
static void boundaries(LbWorld *w,const unsigned char *bytes,size_t size)
{
    LbAllocDomain *d=w->space->domain; size_t baseline=d->current;
    for(size_t n=0;n<size;++n) {
        LbBeamProgram *a=arena(w); LbBeamReader reader={bytes,n,0}; Eterm root;
        CHECK(!lb_pdecode_etf(a,&reader,&root,LB_ETF_TEMPORARY) && a->error.status==LB_BEAM_BAD_FORMAT);
        lb_beam_program_destroy(a); CHECK(d->current==baseline);
    }
    int done=0;
    for(size_t n=0;n<30 && !done;++n) {
        LbBeamProgram *a=arena(w); LbBeamReader reader={bytes,size,0}; Eterm root;
        Prefix prefix={d->system,n?n-1:0}; size_t limit=d->limit;
        if(n) d->system=(LbSystemAllocator){&prefix,allocate,release}; else d->limit=d->current;
        done=lb_pdecode_etf(a,&reader,&root,LB_ETF_TEMPORARY); d->system=prefix.backing; d->limit=limit;
        if(!done) CHECK(lb_alloc_domain_failure(d)==(n?LB_ALLOC_NO_MEMORY:LB_ALLOC_LIMIT));
        else composite(root,0);
        lb_beam_program_destroy(a); lb_alloc_domain_begin_operation(d); CHECK(d->current==baseline);
    }
    CHECK(done); uint64_t required=0;
    for(uint64_t pass=0;pass<=required+1;++pass) {
        LbBeamProgram *a=arena(w); LbBeamReader reader={bytes,size,0}; Eterm root;
        LbWorkBudget *budget=lb_work_budget_create(w); CHECK(budget); uint64_t credit=pass?pass-1:1000000;
        budget->limit=budget->remaining=credit; lb_work_domain_borrow(d,budget);
        int ok=lb_pdecode_etf(a,&reader,&root,LB_ETF_TEMPORARY);
        if(!pass) { CHECK(ok); required=credit-budget->remaining; CHECK(required>1 && required<10000); }
        else if(credit<required) CHECK(!ok && lb_alloc_domain_failure(d)==LB_ALLOC_WORK_LIMIT);
        else CHECK(ok && !budget->remaining);
        lb_beam_program_destroy(a); lb_work_domain_borrow(d,NULL); lb_work_domain_clear_failure(d); lb_alloc_domain_begin_operation(d);
        lb_work_budget_release(budget); CHECK(d->current==baseline);
    }
}
int main(int argc,char **argv)
{
    FILE *f; CHECK(argc==2 && (f=fopen(argv[1],"rb")) && !fseek(f,0,SEEK_END)); long size=ftell(f); CHECK(size>0 && !fseek(f,0,SEEK_SET));
    unsigned char *image=malloc((size_t)size); CHECK(image && fread(image,1,(size_t)size,f)==(size_t)size && !fclose(f));
    LbEngine *engine; LbWorld *w; LbCodeModule *module; LbBeamError error;
    CHECK(lb_engine_create(NULL,&engine)==LB_ENGINE_OK && lb_world_create(engine,&w)==LB_WORLD_OK); lb_engine_enter(engine);
    CHECK(lb_code_load(w->space,image,(size_t)size,&module,&error)==LB_CODE_OK); free(image);
    unsigned char bytes[256]; size_t length=encoded(bytes); ownership(w,module,bytes,length); boundaries(w,bytes,length);
    lb_engine_leave(engine); LbReclamation *r;
    CHECK(lb_world_stop(w,&r)==LB_WORLD_OK && lb_reclamation_poll(r)==LB_WORLD_OK && lb_reclamation_consume(r)==LB_WORLD_OK);
    lb_reclamation_release(r); lb_world_release(w); CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine);
    puts("CORE_ETF_ARENA_OK storage_tags=true consumed_length=true pending_atoms=true copied_lifetime=true hamt_gc=true truncations=true allocation_work_prefixes=true balanced=true binary_to_term_admission=false"); return 0;
}
