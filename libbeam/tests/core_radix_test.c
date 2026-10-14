/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#define _POSIX_C_SOURCE 200809L
#include "world_internal.h"
#include "process_internal.h"
#include "big.h"
#include "bit_copy.h"
#include "work_budget_internal.h"
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
typedef Eterm (*IntegerFormatter)(LbProcess *,Eterm *,const BeamInstr *);
static LbProcess *integer_input(LbCodeEntry *entry,int collect)
{
    LbProcess *p; Eterm input=NIL; CHECK(lb_process_create(entry,&input,1,128,&p)==LB_PROCESS_READY);
    Eterm *hp=p->htop; hp[0]=_make_header(2,NEG_BIG_SUBTAG); hp[1]=0; hp[2]=1;
    p->reg[0]=make_boxed(hp); p->htop+=3; p->construction_root=p->reg[0]; p->reg[1]=make_small(16);
    p->reg[2]=TUPLE2(p->htop,p->reg[0],am_true); p->htop+=3; p->heap_reserved=7;
    if(collect) while(p->htop<p->stop-S_RESERVED) *p->htop++=NIL;
    return p;
}
static void list_result(LbProcess *p,Eterm result,const char *expected)
{
    for(size_t i=0;expected[i];++i) { CHECK(is_list(result) && CAR(list_val(result))==make_small((unsigned char)expected[i])); result=CDR(list_val(result)); }
    CHECK(is_nil(result) && p->heap_reserved==7 && p->reg[0]==p->construction_root);
    Eterm *source=boxed_val(p->construction_root); CHECK(source[0]==_make_header(2,NEG_BIG_SUBTAG) && !source[1] && source[2]==1);
    CHECK(is_tuple(p->reg[2]) && tuple_val(p->reg[2])[1]==p->construction_root && tuple_val(p->reg[2])[2]==am_true);
}
static void integer_lists(LbWorld *world,LbCodeEntry *entry,Memory *memory)
{
    const IntegerFormatter formatters[]={lb_bif_integer_to_list_1,lb_bif_integer_to_list_2};
    const char *expected[]={"-18446744073709551616","-10000000000000000"};
    LbAllocDomain *domain=&world->domain; size_t baseline=memory->live,baseline_bytes=memory->bytes;
    for(unsigned arity=0;arity<2;++arity) for(unsigned collect=0;collect<2;++collect) {
        for(size_t prefix=1;;++prefix) {
            CHECK(prefix<20); LbProcess *p=integer_input(entry,collect);
            Eterm *heap=p->heap,*top=p->htop,root=p->reg[0]; size_t live=memory->live,bytes=memory->bytes,calls=memory->calls;
            memory->fail=calls+prefix; Eterm result=formatters[arity](p,p->reg,NULL); memory->fail=0;
            if(is_non_value(result)) CHECK(p->status==LB_PROCESS_NO_MEMORY && p->freason==LB_EXC_NULL && p->heap==heap && p->htop==top && p->reg[0]==root && !p->collections && memory->live==live && memory->bytes==bytes);
            else { list_result(p,result,expected[arity]); CHECK(!collect || (p->collections && p->heap!=heap)); }
            int finished=!is_non_value(result) && prefix>memory->calls-calls;
            lb_alloc_domain_begin_operation(domain); lb_process_destroy(p); CHECK(memory->live==baseline && memory->bytes==baseline_bytes);
            if(finished) break;
        }
        uint64_t cost=0;
        for(unsigned mode=0;mode<4;++mode) {
            LbProcess *p=integer_input(entry,collect); LbWorkBudget *budget=lb_work_budget_create(world); CHECK(budget);
            uint64_t credit=mode==0?1000000:mode==1?cost:mode==2?cost-1:0;
            budget->limit=budget->remaining=credit; Eterm *heap=p->heap,*top=p->htop,root=p->reg[0];
            size_t live=memory->live,bytes=memory->bytes; lb_work_domain_borrow(domain,budget);
            Eterm result=formatters[arity](p,p->reg,NULL);
            if(mode<2) {
                CHECK(!is_non_value(result)); list_result(p,result,expected[arity]);
                if(!mode) { cost=credit-budget->remaining; CHECK(cost>1); } else CHECK(!budget->remaining);
            } else CHECK(is_non_value(result) && p->status==LB_PROCESS_WORK_LIMIT && p->freason==LB_EXC_NULL && p->heap==heap && p->htop==top && p->reg[0]==root && !p->collections && memory->live==live && memory->bytes==bytes);
            lb_work_domain_borrow(domain,NULL); lb_work_domain_clear_failure(domain); lb_alloc_domain_begin_operation(domain);
            lb_work_budget_release(budget); lb_process_destroy(p); CHECK(memory->live==baseline && memory->bytes==baseline_bytes);
        }
    }
    for(unsigned arity=0;arity<2;++arity) for(unsigned after_scratch=0;after_scratch<2;++after_scratch) {
        LbProcess *p=integer_input(entry,1); size_t limit=domain->limit,current=domain->current;
        Eterm *heap=p->heap,*top=p->htop,root=p->reg[0]; domain->limit=current+(after_scratch?256:0);
        CHECK(is_non_value(formatters[arity](p,p->reg,NULL)) && p->status==LB_PROCESS_LIMIT && p->freason==LB_EXC_NULL);
        CHECK(p->heap==heap && p->htop==top && p->reg[0]==root && !p->collections && domain->current==current);
        domain->limit=limit; lb_alloc_domain_begin_operation(domain); lb_process_destroy(p); CHECK(memory->live==baseline && memory->bytes==baseline_bytes);
    }
    puts("CORE_INTEGER_LIST_OK allocation_prefixes=true exact_work=true one_below=true zero=true memory_limit=true source_immutable=true poisoned_heap=true side_roots=true proper_lists=true reclaimed=true");
}
static void components(LbWorld *world,Memory *memory)
{
    lb_engine_enter(world->engine); Eterm module,function; LbCodeEntry *entry; LbProcess *p;
    CHECK(lb_atoms_find(world->space->atoms,"radix_slice",11,&module)==LB_ATOM_OK);
    CHECK(lb_atoms_find(world->space->atoms,"identity",8,&function)==LB_ATOM_OK);
    CHECK(lb_code_entry_acquire(world->space,module,function,1,&entry)==LB_CODE_OK);
    size_t baseline=memory->live,baseline_bytes=memory->bytes;
    const unsigned char number[]="-10000000000000000"; size_t length=sizeof(number)-1;
    for(unsigned offset=0;offset<8;++offset) for(unsigned tail=0;tail<8;++tail) for(unsigned collect=0;collect<2;++collect) {
        unsigned char shifted[80]={0}; CHECK(lb_bits_copy(number,length,0,shifted,sizeof(shifted),offset,length*8));
        CHECK(lb_process_create_binary(entry,shifted,sizeof(shifted),128,&p)==LB_PROCESS_READY);
        LbSubBits *sub=(LbSubBits *)boxed_val(p->reg[0]); CHECK(sub->thing_word==LB_HEADER_SUB_BITS);
        sub->start=offset; sub->end=offset+length*8+tail; p->construction_root=p->reg[0]; p->reg[1]=make_small(16);
        p->reg[2]=TUPLE2(p->htop,p->reg[0],am_true); p->htop+=3; p->heap_reserved=7;
        Eterm *heap=p->heap; if(collect) while(p->htop<p->stop-S_RESERVED) *p->htop++=NIL;
        Eterm result=lb_bif_binary_to_integer_2(p,p->reg,NULL);
        if(tail) CHECK(is_non_value(result) && p->freason==LB_EXC_BADARG && !p->collections && p->heap==heap);
        else {
            CHECK(lb_is_big(result) && boxed_val(result)[0]==_make_header(2,NEG_BIG_SUBTAG) && !boxed_val(result)[1] && boxed_val(result)[2]==1);
            CHECK(p->heap_reserved==7 && (!collect || (p->collections && p->heap!=heap)));
            CHECK(is_tuple(p->reg[2]) && tuple_val(p->reg[2])[1]==p->construction_root && tuple_val(p->reg[2])[2]==am_true);
            LbBitstringView view; CHECK(lb_bitstring_view(p->construction_root,&view) && view.bit_offset==offset && view.bit_size==length*8);
            unsigned char copied[sizeof(number)]={0};
            CHECK(lb_bits_copy(view.data,sizeof(shifted),view.bit_offset,copied,sizeof(copied),0,view.bit_size)); CHECK(!memcmp(copied,number,length));
        }
        lb_process_destroy(p); CHECK(memory->live==baseline && memory->bytes==baseline_bytes);
    }
    const struct { const char *text; size_t size; } invalid[]={
        {"",0},{"+",1},{"-",1},{" 1",2},{"1 ",2},{"1_0",3},{"--1",3},{"+-1",3},{"1\n",2},{"\0",1},{"1\0",2},{"\302\262",2}};
    for(unsigned base=2;base<=36;++base) for(size_t i=0;i<sizeof(invalid)/sizeof(*invalid);++i) {
        CHECK(lb_process_create_binary(entry,invalid[i].text,invalid[i].size,128,&p)==LB_PROCESS_READY); p->reg[1]=make_small(base);
        size_t calls=memory->calls; memory->fail=calls+1;
        CHECK(is_non_value(lb_bif_binary_to_integer_2(p,p->reg,NULL)) && p->freason==LB_EXC_BADARG && memory->calls==calls);
        memory->fail=0; lb_process_destroy(p); CHECK(memory->live==baseline && memory->bytes==baseline_bytes);
    }
    for(unsigned base=2;base<=36;++base) {
        unsigned char digit=base==36?'_':base<10?'0'+base:'a'+base-10;
        CHECK(lb_process_create_binary(entry,&digit,1,128,&p)==LB_PROCESS_READY); p->reg[1]=make_small(base);
        size_t calls=memory->calls; memory->fail=calls+1;
        CHECK(is_non_value(lb_bif_binary_to_integer_2(p,p->reg,NULL)) && p->freason==LB_EXC_BADARG && memory->calls==calls);
        memory->fail=0; lb_process_destroy(p); CHECK(memory->live==baseline && memory->bytes==baseline_bytes);
    }
    CHECK(lb_process_create_binary(entry,"1",1,128,&p)==LB_PROCESS_READY);
    Eterm *hp=p->htop; double two=2.0; hp[0]=_make_header(1,FLOAT_SUBTAG); memcpy(hp+1,&two,sizeof(two)); Eterm floating=make_boxed(hp); p->htop+=2;
    hp=p->htop; hp[0]=_make_header(2,POS_BIG_SUBTAG); hp[1]=0; hp[2]=1; Eterm big=make_boxed(hp); p->htop+=3;
    const Eterm bad_bases[]={make_small(0),make_small(1),make_small(37),make_small(-1),floating,am_true,NIL,big};
    for(size_t i=0;i<sizeof(bad_bases)/sizeof(*bad_bases);++i) {
        p->reg[1]=bad_bases[i]; p->freason=LB_EXC_NULL; size_t calls=memory->calls; memory->fail=calls+1;
        CHECK(is_non_value(lb_bif_binary_to_integer_2(p,p->reg,NULL)) && p->freason==LB_EXC_BADARG && memory->calls==calls);
        memory->fail=0;
    }
    Eterm binary=p->reg[0]; Eterm tuple=TUPLE1(p->htop,make_small(1)); p->htop+=2;
    Eterm list=CONS(p->htop,make_small(1),NIL); p->htop+=2;
    const Eterm bad_values[]={NIL,am_true,floating,tuple,list,binary};
    for(size_t i=0;i<sizeof(bad_values)/sizeof(*bad_values);++i) {
        p->reg[0]=bad_values[i]; p->reg[1]=make_small(16); size_t calls=memory->calls; memory->fail=calls+1;
        p->freason=LB_EXC_NULL; CHECK(is_non_value(lb_bif_integer_to_list_1(p,p->reg,NULL)) && p->freason==LB_EXC_BADARG);
        p->freason=LB_EXC_NULL; CHECK(is_non_value(lb_bif_integer_to_list_2(p,p->reg,NULL)) && p->freason==LB_EXC_BADARG);
        CHECK(memory->calls==calls); memory->fail=0;
    }
    p->reg[0]=make_small(0);
    for(size_t i=0;i<sizeof(bad_bases)/sizeof(*bad_bases);++i) {
        p->reg[1]=bad_bases[i]; p->freason=LB_EXC_NULL; size_t calls=memory->calls; memory->fail=calls+1;
        CHECK(is_non_value(lb_bif_integer_to_list_2(p,p->reg,NULL)) && p->freason==LB_EXC_BADARG && memory->calls==calls); memory->fail=0;
    }
    lb_process_destroy(p); integer_lists(world,entry,memory); lb_code_entry_release(entry); lb_engine_leave(world->engine);
    puts("CORE_RADIX_COMPONENT_OK offsets=8 tails=8 gc=true poisoned_heap=true preserved_roots=true invalid_bases=true full_validation_before_allocation=true reclaimed=true");
}
int main(int argc,char **argv)
{
    int reference=argc==3 && !strcmp(argv[1],"--reference"); CHECK(argc==2 || reference);
    FILE *file=fopen(argv[reference?2:1],"rb"); CHECK(file && !fseek(file,0,SEEK_END));
    long length=ftell(file); CHECK(length>0 && !fseek(file,0,SEEK_SET)); unsigned char *bytes=malloc((size_t)length);
    CHECK(bytes && fread(bytes,1,(size_t)length,file)==(size_t)length && !fclose(file));
    Memory memory={0}; LbSystemAllocator allocator={&memory,allocate,release}; LbEngine *engine; LbWorld *world;
    CHECK(lb_engine_create(&allocator,&engine)==LB_ENGINE_OK && lb_world_create(engine,&world)==LB_WORLD_OK);
    LbWorldLoadReport report={0}; LbWorldStatus status=lb_world_load_report(world,(LbBytesView){bytes,(size_t)length},&report);
    if(status!=LB_WORLD_OK) fprintf(stderr,"load=%d stage=%s offset=%zu\n",status,report.stage?report.stage:"",report.offset);
    CHECK(status==LB_WORLD_OK); free(bytes); if(!reference) components(world,&memory);
    size_t rows=0;
    for(unsigned base=2;base<=36;++base) {
        char input[4]; CHECK(snprintf(input,sizeof(input),"%u",base)>0); LbCall *call; LbBytesView result;
        CHECK((base==2?lb_world_start:lb_world_call)(world,text("radix_slice"),text("all"),text(input),&call)==LB_WORLD_OK);
        for(size_t i=0;i<10000 && (status=lb_call_poll(call,&result))==LB_WORLD_PENDING;++i) tick();
        if(status!=LB_WORLD_OK) fprintf(stderr,"base=%u status=%d\n",base,status);
        CHECK(status==LB_WORLD_OK); size_t count=0;
        for(size_t i=0;i<result.size;++i) if(result.data[i]=='\n') ++count;
        CHECK(count==31); rows+=count;
        if(reference) CHECK(fwrite(result.data,1,result.size,stdout)==result.size);
        CHECK(lb_call_consume(call)==LB_WORLD_OK); lb_call_release(call);
    }
    CHECK(rows==1085);
    LbCall *empty; LbBytesView empty_result;
    CHECK(lb_world_call(world,text("radix_slice"),text("list_copy"),text(""),&empty)==LB_WORLD_OK);
    for(size_t i=0;i<10000 && (status=lb_call_poll(empty,&empty_result))==LB_WORLD_PENDING;++i) tick();
    CHECK(status==LB_WORLD_OK && !empty_result.size); CHECK(lb_call_consume(empty)==LB_WORLD_OK); lb_call_release(empty);
    LbReclamation *reclamation; CHECK(lb_world_stop(world,&reclamation)==LB_WORLD_OK);
    for(size_t i=0;i<10000 && lb_reclamation_poll(reclamation)==LB_WORLD_PENDING;++i) tick();
    CHECK(lb_reclamation_consume(reclamation)==LB_WORLD_OK); lb_reclamation_release(reclamation); lb_world_release(world);
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine); CHECK(!memory.live && !memory.bytes);
    if(!reference) puts("CORE_RADIX_EXECUTION_OK rows=1085 bases=35 direct=true dynamic=true ordinary_budgets=true reclaimed=true");
}
