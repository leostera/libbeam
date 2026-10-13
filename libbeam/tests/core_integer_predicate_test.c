/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#include "process_internal.h"
#include "world_internal.h"
#include "work_budget_internal.h"
#include "alloc_internal.h"
#include "big.h"
#include "flatmap.h"
#include "binary.h"
#include "writable.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while(0)
typedef enum { SMALL, POSITIVE_BIG, NEGATIVE_BIG, FLOAT, ATOM, FALSE_ATOM, OTHER_ATOM, EMPTY_LIST, LIST, IMPROPER_LIST, TUPLE, MAP, BINARY, EMPTY_BINARY, REF_BINARY, PARTIAL_BITS, OFFSET_BINARY, OFFSET_BITS, KIND_COUNT } Kind;
typedef enum { INTEGER_PREDICATE, LIST_PREDICATE, ATOM_PREDICATE, BINARY_PREDICATE } Predicate;
static Predicate predicate;
static const char *predicate_name(void)
{ return predicate==INTEGER_PREDICATE?"integer":predicate==LIST_PREDICATE?"list":predicate==ATOM_PREDICATE?"atom":"binary"; }
static int accepts(Kind kind)
{
    if(predicate==INTEGER_PREDICATE) return kind<=NEGATIVE_BIG;
    if(predicate==LIST_PREDICATE) return kind==EMPTY_LIST || kind==LIST || kind==IMPROPER_LIST;
    if(predicate==ATOM_PREDICATE) return kind==ATOM || kind==FALSE_ATOM || kind==OTHER_ATOM;
    return kind==BINARY || kind==EMPTY_BINARY || kind==REF_BINARY || kind==OFFSET_BINARY;
}
static Kind positive_kind(void) { return predicate==INTEGER_PREDICATE?POSITIVE_BIG:predicate==LIST_PREDICATE?LIST:predicate==ATOM_PREDICATE?ATOM:BINARY; }
static Kind second_kind(void) { return predicate==INTEGER_PREDICATE?NEGATIVE_BIG:predicate==LIST_PREDICATE?IMPROPER_LIST:predicate==ATOM_PREDICATE?OTHER_ATOM:OFFSET_BINARY; }
static Eterm atom(LbWorld *w,const char *name)
{ Eterm value; CHECK(lb_atoms_find(w->space->atoms,name,strlen(name),&value)==LB_ATOM_OK); return value; }
static LbProcess *create(LbWorld *w,LbCodeModule *module,const char *name,Kind kind)
{
    LbCodeEntry *entry; LbProcess *p; Eterm args[]={make_small(-5),am_true};
    size_t arity=!strcmp(name,"guard_y")?2:1;
    CHECK(lb_code_entry_acquire(w->space,module->name,atom(w,name),arity,&entry)==LB_CODE_OK);
    {
        CHECK(lb_process_create(entry,args,arity,512,&p)==LB_PROCESS_READY);
        Eterm *hp=p->htop;
        switch(kind) {
        case BINARY:case EMPTY_BINARY:case REF_BINARY:case PARTIAL_BITS:case OFFSET_BINARY:case OFFSET_BITS: {
            unsigned char data[100]; memset(data,0xa5,sizeof(data));
            size_t bits=kind==EMPTY_BINARY?0:kind==BINARY?32:kind==PARTIAL_BITS?7:800;
            CHECK(lb_bitstring_build(w->space->domain,&p->off_heap,&p->htop,data,bits,0,&p->reg[0])==LB_ALLOC_OK);
            if(kind==OFFSET_BINARY || kind==OFFSET_BITS) {
                LbSubBits *sub=(LbSubBits *)boxed_val(p->reg[0]); sub->start=3; sub->end=kind==OFFSET_BINARY?11:10;
            }
            break;
        }
        case SMALL: break;
        case POSITIVE_BIG:case NEGATIVE_BIG:
            hp[0]=_make_header(2,kind==POSITIVE_BIG?POS_BIG_SUBTAG:NEG_BIG_SUBTAG);
            hp[1]=0; hp[2]=1; p->reg[0]=make_boxed(hp); p->htop+=3; break;
        case FLOAT: {
            double value=1.0; hp[0]=_make_header(1,FLOAT_SUBTAG); memcpy(hp+1,&value,sizeof(value));
            p->reg[0]=make_boxed(hp); p->htop+=2; break;
        }
        case ATOM:p->reg[0]=am_true; break;
        case FALSE_ATOM:p->reg[0]=am_false; break;
        case OTHER_ATOM:p->reg[0]=atom(w,"no"); break;
        case EMPTY_LIST:p->reg[0]=NIL; break;
        case LIST:case IMPROPER_LIST:
            hp[0]=make_small(1); hp[1]=kind==LIST?NIL:am_true; p->reg[0]=make_list(hp); p->htop+=2; break;
        case TUPLE:p->reg[0]=TUPLE1(hp,make_small(1)); p->htop+=2; break;
        case MAP:
            hp[0]=LB_FLATMAP_HEADER; hp[1]=0; hp[3]=make_arityval_zero(); hp[2]=make_tuple(hp+3);
            p->reg[0]=make_boxed(hp); p->htop+=4; break;
        default:CHECK(0);
        }
    }
    lb_code_entry_release(entry); return p;
}
static LbProcessStatus run(LbProcess *p)
{
    LbProcessStatus status=LB_PROCESS_READY;
    for(size_t i=0;i<1000 && (status==LB_PROCESS_READY || status==LB_PROCESS_YIELDED);++i) status=lb_process_run(p,1,100);
    return status;
}
static void result(LbWorld *w,LbProcess *p,const char *name,Kind kind)
{
    int accepted=accepts(kind); Eterm value=lb_process_result(p);
    if(!strcmp(name,"check")) CHECK(value==(accepted?am_true:am_false));
    else if(!strcmp(name,"guard_y")) CHECK(value==(accepted?am_true:atom(w,"no")));
    else if(!strcmp(name,"allocate_guard") && accepted) {
        CHECK(is_tuple(value) && arityval(*tuple_val(value))==2 && tuple_val(value)[1]==atom(w,predicate_name()));
        Eterm payload=tuple_val(value)[2];
        if(kind>=BINARY) {
            LbBitstringView view; CHECK(lb_bitstring_view(payload,&view));
            CHECK(view.bit_size==(kind==EMPTY_BINARY?0:kind==BINARY?32:kind==REF_BINARY?800:8));
            CHECK(view.bit_offset==(kind==OFFSET_BINARY?3:0));
        }
        else if(kind==ATOM || kind==FALSE_ATOM || kind==OTHER_ATOM) CHECK(payload==(kind==ATOM?am_true:kind==FALSE_ATOM?am_false:atom(w,"no")));
        else if(kind==EMPTY_LIST) CHECK(payload==NIL);
        else if(kind==LIST || kind==IMPROPER_LIST) CHECK(is_list(payload) && list_val(payload)[0]==make_small(1) && list_val(payload)[1]==(kind==LIST?NIL:am_true));
        else if(kind==SMALL) CHECK(payload==make_small(-5));
        else CHECK(lb_is_big(payload) && lb_big_size(payload)==2 && boxed_val(payload)[1]==0 && boxed_val(payload)[2]==1 &&
                   (*boxed_val(payload)&_TAG_HEADER_MASK)==(kind==POSITIVE_BIG?POS_BIG_SUBTAG:NEG_BIG_SUBTAG));
    } else CHECK(value==atom(w,accepted?"yes":"no"));
}
static void fill(LbProcess *p) { while(p->htop<p->stop-S_RESERVED) *p->htop++=NIL; }
static void *deny(void *context,size_t bytes) { (void)context; (void)bytes; return NULL; }
typedef struct { LbSystemAllocator backing; size_t remaining; } Prefix;
static void *prefix_allocate(void *context,size_t bytes)
{
    Prefix *p=context; if(!p->remaining) return NULL;
    --p->remaining; return p->backing.allocate(p->backing.context,bytes);
}
static void prefix_release(void *context,void *ptr)
{ Prefix *p=context; p->backing.release(p->backing.context,ptr); }
static void gc_prefixes(LbWorld *w,LbCodeModule *module)
{
    LbAllocDomain *domain=w->space->domain; size_t baseline=domain->current; int done=0;
    for(size_t allowance=0;allowance<20 && !done;++allowance) {
        LbProcess *p=create(w,module,"allocate_guard",positive_kind());
        if(predicate==INTEGER_PREDICATE) CHECK(p->i[0]==op_is_integer_allocate_fxtt);
        else {
            CHECK(p->i[0]==(predicate==LIST_PREDICATE?op_is_list_fx:predicate==ATOM_PREDICATE?op_is_atom_fx:op_is_binary_fx));
            CHECK(lb_process_run(p,1,100)==LB_PROCESS_YIELDED);
            CHECK(p->i[0]==op_allocate_tt || p->i[0]==op_i_allocate_zero_tt);
        }
        fill(p);
        size_t live=domain->current; Eterm *old_heap=p->htop;
        Prefix prefix={domain->system,allowance}; domain->system=(LbSystemAllocator){&prefix,prefix_allocate,prefix_release};
        LbProcessStatus status=run(p); domain->system=prefix.backing;
        if(status==LB_PROCESS_DONE) {
            CHECK(allowance>0 && p->collections>0); result(w,p,"allocate_guard",positive_kind()); done=1;
        } else CHECK(status==LB_PROCESS_NO_MEMORY && !p->collections && p->htop==old_heap && domain->current==live);
        lb_alloc_domain_begin_operation(domain); lb_process_destroy(p); CHECK(domain->current==baseline);
    }
    CHECK(done);
}
static void boundaries(LbWorld *w,LbCodeModule *module)
{
    LbAllocDomain *domain=w->space->domain; size_t baseline=domain->current;
    LbProcess *rejected=create(w,module,"allocate_guard",FLOAT); fill(rejected);
    size_t limit=domain->limit; LbSystemAllocator system=domain->system;
    domain->limit=domain->current; domain->system.allocate=deny;
    CHECK(run(rejected)==LB_PROCESS_DONE && !rejected->collections);
    domain->limit=limit; domain->system=system;
    result(w,rejected,"allocate_guard",FLOAT); lb_process_destroy(rejected); CHECK(domain->current==baseline);
    const char *names[]={"check","guarded","guard_y","allocate_guard"};
    for(size_t n=0;n<4;++n) for(unsigned gc=0;gc<2;++gc) {
        if(gc && n!=3) continue;
        for(unsigned mode=0;mode<3;++mode) {
            LbProcess *p=create(w,module,names[n],positive_kind()); if(gc) fill(p);
            size_t live=domain->current,limit=domain->limit; LbSystemAllocator system=domain->system;
            if(mode==1) domain->limit=live; if(mode==2) domain->system.allocate=deny;
            LbProcessStatus status=run(p); domain->limit=limit; domain->system=system;
            if(gc && mode) CHECK(status==(mode==1?LB_PROCESS_LIMIT:LB_PROCESS_NO_MEMORY));
            else { CHECK(status==LB_PROCESS_DONE); result(w,p,names[n],positive_kind()); if(gc) CHECK(p->collections>0); }
            lb_alloc_domain_begin_operation(domain); lb_process_destroy(p); CHECK(domain->current==baseline);
        }
        const Kind work_kinds[]={second_kind(),FLOAT,EMPTY_LIST,PARTIAL_BITS};
        for(size_t k=0;k<sizeof(work_kinds)/sizeof(*work_kinds);++k) {
            uint64_t required=0;
            for(uint64_t pass=0;pass<=required+1;++pass) {
                LbProcess *p=create(w,module,names[n],work_kinds[k]); if(gc) fill(p);
                LbWorkBudget *budget=lb_work_budget_create(w); CHECK(budget); uint64_t credit=pass?pass-1:1000000;
                budget->limit=budget->remaining=credit; lb_work_domain_borrow(domain,budget);
                LbProcessStatus status=run(p);
                if(!pass) { CHECK(status==LB_PROCESS_DONE); required=credit-budget->remaining; CHECK(required>1 && required<10000); }
                else if(credit==required) CHECK(status==LB_PROCESS_DONE && budget->remaining==0);
                else CHECK(status==LB_PROCESS_WORK_LIMIT);
                if(status==LB_PROCESS_DONE) result(w,p,names[n],work_kinds[k]);
                lb_work_domain_borrow(domain,NULL); lb_work_domain_clear_failure(domain); lb_alloc_domain_begin_operation(domain);
                lb_process_destroy(p); lb_work_budget_release(budget); CHECK(domain->current==baseline);
            }
        }
    }
}
static void binary_metadata(LbWorld *w,LbCodeModule *module)
{
    const char *names[]={"check","guarded","guard_y"};
    for(size_t n=0;n<3;++n) for(unsigned writable=0;writable<2;++writable) {
        LbProcess *p=create(w,module,names[n],writable?SMALL:OFFSET_BINARY);
        if(writable) CHECK(lb_writable_init(p,100,&p->reg[0])==LB_WRITABLE_OK);
        LbSubBits *sub=(LbSubBits *)boxed_val(p->reg[0]);
        if(!writable) sub->base_flags|=LB_SUB_VOLATILE;
        LbSubBits before=*sub; LbBinRef *ref=(LbBinRef *)boxed_val(sub->orig);
        Uint flags=ref->val->intern.flags,refs=atomic_load(&ref->val->intern.refc);
        CHECK(run(p)==LB_PROCESS_DONE && !p->collections);
        CHECK(lb_process_result(p)==(n==1?atom(w,"yes"):am_true));
        CHECK(!memcmp(sub,&before,sizeof(before)) && ref->val->intern.flags==flags && atomic_load(&ref->val->intern.refc)==refs);
        lb_process_destroy(p);
    }
}
static void literal_guards(LbWorld *w,const char *path)
{
    FILE *file=fopen(path,"rb"); CHECK(file && !fseek(file,0,SEEK_END));
    long length=ftell(file); CHECK(length>0 && !fseek(file,0,SEEK_SET));
    unsigned char *bytes=malloc((size_t)length); CHECK(bytes && fread(bytes,1,(size_t)length,file)==(size_t)length && !fclose(file));
    LbCodeModule *module; LbBeamError error;
    LbCodeStatus status=lb_code_load(w->space,bytes,(size_t)length,&module,&error); free(bytes);
    CHECK(status==LB_CODE_OK);
    const char *names[]={predicate_name(),predicate==INTEGER_PREDICATE?"noninteger":predicate==LIST_PREDICATE?"nonlist":predicate==ATOM_PREDICATE?"nonatom":"nonbinary"};
    for(size_t i=0;i<2;++i) {
        LbProcess *p=create(w,module,names[i],SMALL);
        CHECK(run(p)==LB_PROCESS_DONE && lb_process_result(p)==atom(w,i?"no":"yes")); lb_process_destroy(p);
    }
}
int main(int argc,char **argv)
{
    CHECK(argc==3 || (argc==4 && (!strcmp(argv[3],"list") || !strcmp(argv[3],"atom") || !strcmp(argv[3],"binary"))));
    predicate=argc==3?INTEGER_PREDICATE:!strcmp(argv[3],"list")?LIST_PREDICATE:!strcmp(argv[3],"atom")?ATOM_PREDICATE:BINARY_PREDICATE;
    FILE *file; long length; CHECK((file=fopen(argv[1],"rb")) && !fseek(file,0,SEEK_END));
    length=ftell(file); CHECK(length>0 && !fseek(file,0,SEEK_SET)); unsigned char *bytes=malloc((size_t)length);
    CHECK(bytes && fread(bytes,1,(size_t)length,file)==(size_t)length && !fclose(file));
    LbEngine *engine; LbWorld *world; LbCodeModule *module; LbBeamError error;
    CHECK(lb_engine_create(NULL,&engine)==LB_ENGINE_OK && lb_world_create(engine,&world)==LB_WORLD_OK); lb_engine_enter(engine);
    LbCodeStatus loaded=lb_code_load(world->space,bytes,(size_t)length,&module,&error);
    if(loaded!=LB_CODE_OK) fprintf(stderr,"load=%d stage=%s offset=%zu\n",loaded,error.stage,error.offset);
    CHECK(loaded==LB_CODE_OK); free(bytes);
    literal_guards(world,argv[2]);
    const char *names[]={"check","guarded","allocate_guard","guard_y"};
    for(size_t n=0;n<4;++n) for(Kind kind=SMALL;kind<KIND_COUNT;++kind) {
        LbProcess *p=create(world,module,names[n],kind); CHECK(run(p)==LB_PROCESS_DONE); result(world,p,names[n],kind); lb_process_destroy(p);
    }
    LbProcess *other=create(world,module,"other_types",SMALL);
    CHECK(run(other)==LB_PROCESS_DONE && is_tuple(lb_process_result(other)));
    Eterm *values=tuple_val(lb_process_result(other)); CHECK(arityval(values[0])==4);
    for(size_t i=1;i<=4;++i) CHECK(values[i]==am_false);
    lb_process_destroy(other);
    if(predicate==BINARY_PREDICATE) binary_metadata(world,module);
    gc_prefixes(world,module); boundaries(world,module); lb_engine_leave(engine); LbReclamation *reclamation;
    CHECK(lb_world_stop(world,&reclamation)==LB_WORLD_OK && lb_reclamation_poll(reclamation)==LB_WORLD_OK);
    CHECK(lb_reclamation_consume(reclamation)==LB_WORLD_OK); lb_reclamation_release(reclamation); lb_world_release(world);
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine);
    printf("CORE_TYPE_PREDICATE_OK predicate=%s body=true guards=true true_false_types=true moving_gc=true memory_oom=true gc_allocation_prefixes=true literal_guards=true work_prefixes=true balanced=true\n",predicate_name());
    return 0;
}
