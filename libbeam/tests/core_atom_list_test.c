/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#include "process_internal.h"
#include "world_internal.h"
#include "work_budget_internal.h"
#include "alloc_internal.h"
#include "binary.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while(0)
static size_t encode(const uint32_t *chars,size_t count,unsigned char bytes[1020])
{
    size_t n=0; CHECK(count<=255);
    for(size_t i=0;i<count;++i) {
        uint32_t c=chars[i];
        if(c<0x80) bytes[n++]=(unsigned char)c;
        else if(c<0x800) { bytes[n++]=0xc0|(c>>6); bytes[n++]=0x80|(c&63); }
        else if(c<0x10000) { bytes[n++]=0xe0|(c>>12); bytes[n++]=0x80|((c>>6)&63); bytes[n++]=0x80|(c&63); }
        else { bytes[n++]=0xf0|(c>>18); bytes[n++]=0x80|((c>>12)&63); bytes[n++]=0x80|((c>>6)&63); bytes[n++]=0x80|(c&63); }
    }
    return n;
}
static Eterm intern(LbWorld *w,const uint32_t *chars,size_t count)
{
    unsigned char bytes[1020]; size_t size=encode(chars,count,bytes); Eterm value;
    CHECK(lb_atoms_intern(w->space->atoms,bytes,size,&value)==LB_ATOM_OK); return value;
}
static LbProcess *create(LbWorld *w,LbCodeModule *module,const char *name,Eterm arg)
{
    Eterm function; LbCodeEntry *entry; LbProcess *p;
    CHECK(lb_atoms_find(w->space->atoms,name,strlen(name),&function)==LB_ATOM_OK);
    CHECK(lb_code_entry_acquire(w->space,module->name,function,1,&entry)==LB_CODE_OK);
    CHECK(lb_process_create(entry,&arg,1,2048,&p)==LB_PROCESS_READY); lb_code_entry_release(entry); return p;
}
static LbProcessStatus run(LbProcess *p)
{
    LbProcessStatus status=LB_PROCESS_READY;
    for(size_t i=0;i<1000 && (status==LB_PROCESS_READY || status==LB_PROCESS_YIELDED);++i) status=lb_process_run(p,1,100);
    return status;
}
static void enter_bif(LbProcess *p)
{
    for(unsigned i=0;i<100 && p->i[0]!=op_call_bif_W;++i) CHECK(lb_process_run(p,1,100)==LB_PROCESS_YIELDED);
    CHECK(p->i[0]==op_call_bif_W);
}
static void fill(LbProcess *p)
{ while(p->htop<p->stop-S_RESERVED-p->heap_reserved) *p->htop++=NIL; }
static void text(Eterm list,const uint32_t *chars,size_t count)
{
    for(size_t i=0;i<count;++i) { CHECK(is_list(list) && list_val(list)[0]==make_small(chars[i])); list=list_val(list)[1]; }
    CHECK(is_nil(list));
}
static Eterm characters(LbProcess *p,const uint32_t *chars,size_t count,Eterm tail)
{
    CHECK(lb_heap_reserve(p,2*count,1));
    for(size_t i=count;i;--i) { tail=CONS(p->htop,make_small(chars[i-1]),tail); p->htop+=2; }
    return tail;
}
static void vector(LbWorld *w,LbCodeModule *module,const uint32_t *chars,size_t count)
{
    Eterm name=intern(w,chars,count); size_t atoms=lb_atoms_count(w->space->atoms),baseline=w->space->domain->current;
    for(unsigned gc=0;gc<2;++gc) {
        LbProcess *p=create(w,module,"convert",name); enter_bif(p); if(gc) fill(p);
        CHECK(run(p)==LB_PROCESS_DONE); text(lb_process_result(p),chars,count);
        if(gc && count) CHECK(p->collections>0);
        CHECK(lb_process_collect(p,0)==LB_PROCESS_DONE); text(lb_process_result(p),chars,count); lb_process_destroy(p);
        /* Direct helper contract: retain outstanding heap credit, including
         * across collection, and leave the immediate atom operand unchanged. */
        p=create(w,module,"convert",name); p->heap_reserved=17; if(gc) fill(p);
        Eterm result=lb_bif_atom_to_list_1(p,p->reg,NULL);
        CHECK(!is_non_value(result) && p->reg[0]==name && p->heap_reserved==17); text(result,chars,count);
        p->reg[0]=result; CHECK(lb_process_collect(p,0)==LB_PROCESS_READY); text(p->reg[0],chars,count);
        CHECK(lb_bif_list_to_existing_atom_1(p,p->reg,NULL)==name);
        CHECK(lb_bif_list_to_atom_1(p,p->reg,NULL)==name); lb_process_destroy(p);
        for(unsigned existing=0;existing<2;++existing) {
            p=create(w,module,existing?"existing":"from_list",NIL);
            p->reg[0]=characters(p,chars,count,NIL);
            if(gc) CHECK(lb_process_collect_live(p,0,1));
            CHECK(run(p)==LB_PROCESS_DONE && lb_process_result(p)==name); lb_process_destroy(p);
        }
        CHECK(w->space->domain->current==baseline && lb_atoms_count(w->space->atoms)==atoms);
    }
    unsigned char expected[1020]; size_t size=encode(chars,count,expected);
    LbProcess *p=create(w,module,"utf8",name); CHECK(run(p)==LB_PROCESS_DONE); LbBitstringView view;
    CHECK(lb_bitstring_view(lb_process_result(p),&view) && !view.bit_offset && view.bit_size==size*8);
    CHECK(!size || !memcmp(view.data,expected,size)); lb_process_destroy(p);
    CHECK(w->space->domain->current==baseline);
}
typedef struct { LbSystemAllocator backing; size_t remaining; } Prefix;
static void *allocate(void *context,size_t bytes)
{ Prefix *p=context; if(!p->remaining) return NULL; --p->remaining; return p->backing.allocate(p->backing.context,bytes); }
static void release(void *context,void *ptr)
{ Prefix *p=context; p->backing.release(p->backing.context,ptr); }
static void failures(LbWorld *w,LbCodeModule *module,Eterm name,const uint32_t *chars,size_t count)
{
    LbAllocDomain *domain=w->space->domain; size_t baseline=domain->current;
    for(unsigned execution=0;execution<2;++execution) {
      int done=0;
      for(size_t attempt=0;attempt<20 && !done;++attempt) {
        LbProcess *p=create(w,module,"convert",name);
        if(execution) enter_bif(p); else p->heap_reserved=17;
        size_t credit=p->heap_reserved; fill(p);
        Eterm *heap=p->htop; size_t live=domain->current,limit=domain->limit;
        Prefix prefix={domain->system,attempt?attempt-1:0};
        if(!attempt) domain->limit=live; else domain->system=(LbSystemAllocator){&prefix,allocate,release};
        Eterm result;
        if(execution) { (void)run(p); result=lb_process_result(p); }
        else result=lb_bif_atom_to_list_1(p,p->reg,NULL);
        domain->limit=limit; domain->system=prefix.backing;
        CHECK(p->heap_reserved==credit);
        if(!execution || is_non_value(result)) CHECK(p->reg[0]==name);
        if(is_non_value(result)) {
            CHECK(p->status==(attempt?LB_PROCESS_NO_MEMORY:LB_PROCESS_LIMIT));
            CHECK(p->htop==heap && !p->collections && domain->current==live);
        } else { CHECK(attempt>1 && p->collections>0); text(result,chars,count); done=1; }
        lb_alloc_domain_begin_operation(domain); lb_process_destroy(p); CHECK(domain->current==baseline);
      }
      CHECK(done);
    }
    for(unsigned gc=0;gc<2;++gc) {
        uint64_t required=0;
        for(uint64_t pass=0;pass<=required+1;++pass) {
            LbProcess *p=create(w,module,"convert",name); enter_bif(p); if(gc) fill(p);
            LbWorkBudget *budget=lb_work_budget_create(w); CHECK(budget); uint64_t credit=pass?pass-1:1000000;
            budget->limit=budget->remaining=credit; lb_work_domain_borrow(domain,budget);
            LbProcessStatus status=run(p);
            if(!pass) { CHECK(status==LB_PROCESS_DONE); required=credit-budget->remaining; CHECK(required>1 && required<20000); }
            else if(credit==required) CHECK(status==LB_PROCESS_DONE && budget->remaining==0);
            else CHECK(status==LB_PROCESS_WORK_LIMIT);
            if(status==LB_PROCESS_DONE) text(lb_process_result(p),chars,count);
            lb_work_domain_borrow(domain,NULL); lb_work_domain_clear_failure(domain); lb_alloc_domain_begin_operation(domain);
            lb_process_destroy(p); lb_work_budget_release(budget); CHECK(domain->current==baseline);
        }
    }
}
static void list_atom_failures(LbWorld *w,LbWorld *peer,LbCodeModule *module)
{
    LbAllocDomain *domain=w->space->domain;
    uint32_t chars[256]; for(size_t i=0;i<256;++i) chars[i]=97;
    size_t atoms=lb_atoms_count(w->space->atoms),baseline=domain->current;
    for(unsigned existing=0;existing<2;++existing) for(unsigned kind=0;kind<11;++kind) {
        LbProcess *p=create(w,module,existing?"existing":"from_list",NIL);
        size_t count=kind>=5?(kind==5?255:256):1;
        p->reg[0]=characters(p,chars,count,kind==5?am_false:NIL);
        if(kind==0) p->reg[0]=make_small(7);
        if(kind==1) CAR(list_val(p->reg[0]))=am_false;
        if(kind==2) CAR(list_val(p->reg[0]))=make_small(-1);
        if(kind==3) CAR(list_val(p->reg[0]))=make_small(0xd800);
        if(kind==4) CAR(list_val(p->reg[0]))=make_small(0x110000);
        if(kind==7 || kind==8) {
            Eterm tail=p->reg[0]; for(unsigned i=0;i<(kind==7?255u:254u);++i) tail=CDR(list_val(tail));
            CAR(list_val(tail))=am_false;
        }
        if(kind==9 || kind==10) {
            Eterm *hp=p->htop;
            if(kind==9) { double number=1.0; hp[0]=_make_header(1,FLOAT_SUBTAG); memcpy(hp+1,&number,sizeof(number)); p->htop+=2; }
            else { hp[0]=_make_header(2,POS_BIG_SUBTAG); hp[1]=0; hp[2]=1; p->htop+=3; }
            CAR(list_val(p->reg[0]))=make_boxed(hp);
        }
        Eterm reason=!existing && (kind==6 || kind==7)?am_system_limit:am_badarg;
        CHECK(run(p)==LB_PROCESS_EXCEPTION && p->exception_value==reason);
        lb_process_destroy(p); CHECK(lb_atoms_count(w->space->atoms)==atoms && domain->current==baseline);
    }
    uint32_t fresh[]={0x10ffff,108,105,115,116,0,113}; unsigned char bytes[1020];
    size_t size=encode(fresh,7,bytes); Eterm atom;
    CHECK(lb_atoms_find(w->space->atoms,bytes,size,&atom)==LB_ATOM_NOT_FOUND);
    LbProcess *p=create(w,module,"existing",NIL); p->reg[0]=characters(p,fresh,7,NIL);
    CHECK(run(p)==LB_PROCESS_EXCEPTION && p->exception_value==am_badarg); lb_process_destroy(p);
    CHECK(lb_atoms_count(w->space->atoms)==atoms && domain->current==baseline);
    int done=0;
    for(size_t attempt=0;attempt<20 && !done;++attempt) {
        p=create(w,module,"from_list",NIL); p->reg[0]=characters(p,fresh,7,NIL);
        Eterm input=p->reg[0]; size_t live=domain->current,limit=domain->limit;
        Prefix prefix={domain->system,attempt?attempt-1:0};
        if(!attempt) domain->limit=live; else domain->system=(LbSystemAllocator){&prefix,allocate,release};
        Eterm result=lb_bif_list_to_atom_1(p,p->reg,NULL);
        domain->limit=limit; domain->system=prefix.backing;
        CHECK(p->reg[0]==input);
        if(is_non_value(result)) {
            CHECK(p->status==(attempt?LB_PROCESS_NO_MEMORY:LB_PROCESS_LIMIT));
            CHECK(domain->current==live && lb_atoms_count(w->space->atoms)==atoms);
            CHECK(lb_atoms_find(w->space->atoms,bytes,size,&atom)==LB_ATOM_NOT_FOUND);
        } else {
            atom=result; done=1;
            CHECK(lb_atoms_count(w->space->atoms)==atoms+1);
        }
        lb_alloc_domain_begin_operation(domain); lb_process_destroy(p);
        if(!done) CHECK(domain->current==baseline);
    }
    CHECK(done && lb_atoms_find(peer->space->atoms,bytes,size,&atom)==LB_ATOM_NOT_FOUND);
    CHECK(lb_atoms_find(w->space->atoms,bytes,size,&atom)==LB_ATOM_OK);
    baseline=domain->current; atoms=lb_atoms_count(w->space->atoms);
    for(unsigned existing=0;existing<2;++existing) {
        uint64_t required=0;
        for(unsigned pass=0;pass<4;++pass) {
            p=create(w,module,existing?"existing":"from_list",NIL); p->reg[0]=characters(p,fresh,7,NIL);
            LbWorkBudget *budget=lb_work_budget_create(w); CHECK(budget);
            uint64_t credit=pass==0?budget->limit:pass==1?0:pass==2?required-1:required;
            budget->limit=budget->remaining=credit; lb_work_domain_borrow(domain,budget);
            Eterm result=existing?lb_bif_list_to_existing_atom_1(p,p->reg,NULL):lb_bif_list_to_atom_1(p,p->reg,NULL);
            if(!pass) { CHECK(result==atom); required=credit-budget->remaining; CHECK(required>1); }
            else if(pass==3) CHECK(result==atom && !budget->remaining);
            else CHECK(is_non_value(result) && p->status==LB_PROCESS_WORK_LIMIT && p->freason==LB_EXC_NULL);
            lb_work_domain_borrow(domain,NULL); lb_work_domain_clear_failure(domain); lb_alloc_domain_begin_operation(domain);
            lb_process_destroy(p); lb_work_budget_release(budget);
            CHECK(domain->current==baseline && lb_atoms_count(w->space->atoms)==atoms);
        }
    }
    /* Exhaust publication credit on a genuinely absent name: no atom may
     * appear merely because UTF-8 parsing succeeded. */
    fresh[6]=114; size=encode(fresh,7,bytes);
    p=create(w,module,"from_list",NIL); p->reg[0]=characters(p,fresh,7,NIL);
    LbWorkBudget *budget=lb_work_budget_create(w); CHECK(budget);
    budget->limit=budget->remaining=1+7+size;
    lb_work_domain_borrow(domain,budget);
    CHECK(is_non_value(lb_bif_list_to_atom_1(p,p->reg,NULL)) && p->status==LB_PROCESS_WORK_LIMIT);
    CHECK(lb_atoms_count(w->space->atoms)==atoms && lb_atoms_find(w->space->atoms,bytes,size,&atom)==LB_ATOM_NOT_FOUND);
    lb_work_domain_borrow(domain,NULL); lb_work_domain_clear_failure(domain); lb_alloc_domain_begin_operation(domain);
    lb_process_destroy(p); lb_work_budget_release(budget); CHECK(domain->current==baseline);
}
int main(int argc,char **argv)
{
    FILE *file; CHECK(argc==2 && (file=fopen(argv[1],"rb")) && !fseek(file,0,SEEK_END));
    long length=ftell(file); CHECK(length>0 && !fseek(file,0,SEEK_SET)); unsigned char *bytes=malloc((size_t)length);
    CHECK(bytes && fread(bytes,1,(size_t)length,file)==(size_t)length && !fclose(file));
    LbEngine *engine; LbWorld *world,*peer; LbCodeModule *module; LbBeamError error;
    CHECK(lb_engine_create(NULL,&engine)==LB_ENGINE_OK && lb_world_create(engine,&world)==LB_WORLD_OK);
    CHECK(lb_world_create(engine,&peer)==LB_WORLD_OK); lb_engine_enter(engine);
    LbCodeStatus loaded=lb_code_load(world->space,bytes,(size_t)length,&module,&error);
    if(loaded!=LB_CODE_OK) fprintf(stderr,"load=%d stage=%s offset=%zu\n",loaded,error.stage,error.offset);
    CHECK(loaded==LB_CODE_OK); free(bytes);
    uint32_t nul[]={0},embedded[]={97,0,98},ascii[]={104,101,108,108,111};
    uint32_t lower[]={0x7f,0x80,0xff,0x100,0x7ff,0x800},upper[]={0xd7ff,0xe000,0xffff,0x10000,0x1f600,0x10ffff},maximum[255];
    for(size_t i=0;i<255;++i) maximum[i]=0x10ffff;
    vector(world,module,NULL,0); vector(world,module,nul,1); vector(world,module,embedded,3); vector(world,module,ascii,5);
    vector(world,module,lower,6); vector(world,module,upper,6); vector(world,module,maximum,255);
    uint32_t latin[]={0,127,128,255}; Eterm name=intern(world,latin,4);
    LbProcess *p=create(world,module,"latin1",name); CHECK(run(p)==LB_PROCESS_DONE); LbBitstringView view;
    unsigned char expected[]={0,127,128,255};
    CHECK(lb_bitstring_view(lb_process_result(p),&view) && !view.bit_offset && view.bit_size==32 && !memcmp(view.data,expected,4)); lb_process_destroy(p);
    uint32_t wide[]={256}; p=create(world,module,"latin1",intern(world,wide,1));
    CHECK(run(p)==LB_PROCESS_EXCEPTION && p->exception_value==am_badarg); lb_process_destroy(p);
    for(unsigned kind=0;kind<6;++kind) {
        p=create(world,module,"convert",kind?make_small(0):NIL);
        Eterm *hp=p->htop;
        if(kind==2) { p->reg[0]=TUPLE1(hp,am_true); p->htop+=2; }
        else if(kind==3) { hp[0]=make_small(97); hp[1]=NIL; p->reg[0]=make_list(hp); p->htop+=2; }
        else if(kind==4) {
            double value=1.0; hp[0]=_make_header(1,FLOAT_SUBTAG); memcpy(hp+1,&value,sizeof(value));
            p->reg[0]=make_boxed(hp); p->htop+=2;
        } else if(kind==5) CHECK(lb_bitstring_build(world->space->domain,&p->off_heap,&p->htop,(const unsigned char *)"atom",32,0,&p->reg[0])==LB_ALLOC_OK);
        CHECK(run(p)==LB_PROCESS_EXCEPTION && p->exception_value==am_badarg); lb_process_destroy(p);
    }
    Eterm empty=intern(world,NULL,0);
    for(unsigned mode=0;mode<2;++mode) {
        p=create(world,module,"convert",empty); enter_bif(p); fill(p);
        LbAllocDomain *domain=world->space->domain; size_t live=domain->current,limit=domain->limit;
        Prefix prefix={domain->system,0};
        if(mode) domain->system=(LbSystemAllocator){&prefix,allocate,release}; else domain->limit=live;
        CHECK(run(p)==LB_PROCESS_DONE && lb_process_result(p)==NIL && !p->collections && domain->current==live);
        domain->system=prefix.backing; domain->limit=limit; lb_process_destroy(p);
    }
    failures(world,module,intern(world,upper,6),upper,6);
    failures(world,module,intern(world,maximum,255),maximum,255);
    list_atom_failures(world,peer,module);
    lb_engine_leave(engine); LbReclamation *reclamation;
    CHECK(lb_world_stop(world,&reclamation)==LB_WORLD_OK && lb_reclamation_poll(reclamation)==LB_WORLD_OK);
    CHECK(lb_reclamation_consume(reclamation)==LB_WORLD_OK); lb_reclamation_release(reclamation); lb_world_release(world);
    CHECK(lb_world_stop(peer,&reclamation)==LB_WORLD_OK && lb_reclamation_poll(reclamation)==LB_WORLD_OK);
    CHECK(lb_reclamation_consume(reclamation)==LB_WORLD_OK); lb_reclamation_release(reclamation); lb_world_release(peer);
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine);
    puts("CORE_ATOM_LIST_OK unicode=true empty_nul=true max_name=true binary_regression=true moving_gc=true heap_credit=true allocation_work_prefixes=true list_parsing=true atom_publication_rollback=true namespace_isolation=true balanced=true"); return 0;
}
