/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#include "process_internal.h"
#include "reference.h"
#include "term_compare.h"
#include <stdio.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"CHECK %s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while(0)
typedef struct { size_t calls,fail,live; } Memory;
static void *allocate(void *ctx,size_t size) { Memory *m=ctx; if(++m->calls==m->fail) return NULL; void *p=malloc(size); if(p) ++m->live; return p; }
static void release(void *ctx,void *p) { Memory *m=ctx; CHECK(m->live); --m->live; free(p); }
static LbCodeModule *load(LbCodeSpace *space,const char *path)
{
    FILE *f=fopen(path,"rb"); CHECK(f && !fseek(f,0,SEEK_END)); long n=ftell(f); CHECK(n>0 && !fseek(f,0,SEEK_SET));
    unsigned char *bytes=malloc((size_t)n); CHECK(bytes && fread(bytes,1,(size_t)n,f)==(size_t)n && !fclose(f));
    LbCodeModule *module=NULL; LbBeamError error;
    LbCodeStatus status=lb_code_load(space,bytes,(size_t)n,&module,&error); free(bytes);
    if(status!=LB_CODE_OK) fprintf(stderr,"load %d %s %zu\n",status,error.stage,error.offset);
    CHECK(status==LB_CODE_OK); return module;
}
static LbCodeEntry *entry(LbCodeSpace *s,LbCodeModule *m,const char *name,unsigned arity)
{
    Eterm atom; LbCodeEntry *e; CHECK(lb_atoms_find(s->atoms,name,strlen(name),&atom)==LB_ATOM_OK);
    CHECK(lb_code_entry_acquire(s,lb_code_module_name(m),atom,arity,&e)==LB_CODE_OK); return e;
}
static LbProcess *create(LbCodeEntry *e)
{
    Eterm args[]={NIL,NIL}; LbProcess *p; CHECK(e->entry->info.mfa.arity<=2);
    CHECK(lb_process_create(e,args,e->entry->info.mfa.arity,64,&p)==LB_PROCESS_READY); return p;
}
static LbProcessStatus finish(LbProcess *p)
{
    LbProcessStatus s; do { s=lb_process_run(p,1000,128); } while(s==LB_PROCESS_YIELDED); return s;
}
static void table(LbCodeSpace *space,LbCodeEntry *e,Memory *m)
{
    LbPidTable *t=NULL; size_t before=m->live;
    m->fail=m->calls+1; CHECK(lb_pid_table_create(space,8,&t)==LB_PID_NO_MEMORY && !t && m->live==before); m->fail=0;
    CHECK(lb_pid_table_create(space,8,&t)==LB_PID_OK); LbProcess *p[9]; Eterm old[8];
    for(size_t round=0;round<100;++round) {
        for(size_t i=0;i<9;++i) p[i]=create(e);
        for(size_t i=0;i<8;++i) {
            CHECK(lb_pid_insert(t,&p[i]->identity,p[i])==LB_PID_OK);
            CHECK(p[i]->identity.id==(((round*8+i)<<4)|_TAG_IMMED1_PID));
            if(round) CHECK(!lb_pid_lookup(t,old[i]));
            old[i]=p[i]->identity.id; CHECK(lb_pid_lookup(t,old[i])==p[i]);
        }
        CHECK(lb_pid_count(t)==8 && lb_pid_table_destroy(t)==LB_PID_BUSY);
        CHECK(lb_pid_insert(t,&p[8]->identity,p[8])==LB_PID_FULL && !p[8]->identity.owner);
        for(size_t i=0;i<8;++i) {
            CHECK(lb_pid_remove(t,&p[i]->identity)==LB_PID_OK && !lb_pid_lookup(t,old[i]));
            CHECK(lb_pid_remove(t,&p[i]->identity)==LB_PID_INVALID);
        }
        for(size_t i=0;i<9;++i) lb_process_destroy(p[i]);
    }
    /* Reach the final generation without trillions of allocations. Only the
     * generation bits change: native slot membership/free-ring state remains
     * valid. Retirement must not recycle this PID's slot at generation zero. */
    p[0]=create(e); CHECK(lb_pid_insert(t,&p[0]->identity,p[0])==LB_PID_OK);
    p[0]->identity.id=((((UINT64_MAX>>4)&~(Uint)7)<<4)|(p[0]->identity.id&127));
    Eterm last=p[0]->identity.id; CHECK(lb_pid_lookup(t,last)==p[0]); lb_process_destroy(p[0]);
    CHECK(!lb_pid_lookup(t,last));
    for(size_t i=0;i<8;++i) p[i]=create(e);
    for(size_t i=0;i<7;++i) CHECK(lb_pid_insert(t,&p[i]->identity,p[i])==LB_PID_OK);
    CHECK(lb_pid_insert(t,&p[7]->identity,p[7])==LB_PID_FULL);
    for(size_t i=0;i<8;++i) lb_process_destroy(p[i]);
    CHECK(lb_pid_table_destroy(t)==LB_PID_OK && m->live==before);
}
static void permuted_ring(LbCodeSpace *space,LbCodeEntry *e)
{
    LbPidTable *t; CHECK(lb_pid_table_create(space,64,&t)==LB_PID_OK);
    Eterm stale[64]={0}; uint32_t random=42;
    for(size_t round=0;round<100;++round) {
        LbProcess *processes[64];
        for(size_t i=0;i<64;++i) {
            processes[i]=create(e); CHECK(lb_pid_insert(t,&processes[i]->identity,processes[i])==LB_PID_OK);
            Eterm id=processes[i]->identity.id;
            if(!round) CHECK(id==(((((i&7)<<3)+(i>>3))<<4)|_TAG_IMMED1_PID));
            CHECK(lb_pid_lookup(t,id)==processes[i]);
            for(size_t j=0;j<64;++j) CHECK(!lb_pid_lookup(t,stale[j]));
        }
        for(size_t left=64;left;--left) {
            random=random*1664525u+1013904223u; size_t index=random%left;
            stale[64-left]=processes[index]->identity.id;
            lb_process_destroy(processes[index]); processes[index]=processes[left-1];
        }
        CHECK(!lb_pid_count(t));
    }
    CHECK(lb_pid_table_destroy(t)==LB_PID_OK);
}
static void fun_consumers(LbCodeSpace *space,LbCodeModule *module,LbCodeModule *target_module,Memory *m)
{
    LbCodeEntry *compare=entry(space,module,"reference_equal",2);
    LbFunEntry target={.dispatch={.addresses={compare->entry->dispatch.addresses[0]}},
        .module=module->name,.arity=1,.owner=module};
    for(size_t different=0;different<2;++different) {
        LbProcess *p=create(compare); size_t users=module->users;
        size_t calls=m->calls; m->fail=calls+1;
        CHECK(lb_fun_retain(p,&target)==LB_ALLOC_NO_MEMORY && !p->fun_leases && module->users==users);
        m->fail=0; CHECK(lb_fun_retain(p,&target)==LB_ALLOC_OK && module->users==users+1);
        unsigned char bytes[65]; memset(bytes,0x6b,sizeof(bytes)); Eterm binary;
        CHECK(lb_bitstring_build(space->domain,&p->off_heap,&p->htop,bytes,520,0,&binary)==LB_ALLOC_OK);
        LbFun *fun=(LbFun *)p->htop; p->htop+=LB_FUN_WORDS+1;
        fun->thing_word=LB_FUN_HEADER(1,1); fun->entry.fun=&target; fun->env[0]=binary;
        p->reg[0]=make_boxed((Eterm *)fun);
        size_t words=0; CHECK(lb_flat_size(space->domain,p->reg[0],&words) && words==LB_FUN_WORDS+1+LB_REFC_BITS_WORDS);
        p->reg[1]=lb_copy_flat(p->reg[0],&p->htop,&p->off_heap);
        CHECK(!is_non_value(p->reg[1]) && lb_fun_free(lb_fun(p->reg[1]))==1);
        if(different) ((LbFun *)boxed_val(p->reg[1]))->env[0]=make_small(7);
        CHECK(lb_process_has_other_reference(p,binary,NULL)); /* closure environment is an alias */
        m->fail=m->calls+1; CHECK(lb_process_collect(p,0)==LB_PROCESS_NO_MEMORY && module->users==users+1);
        m->fail=0; CHECK(lb_process_collect(p,0)==LB_PROCESS_READY && module->users==users+1);
        CHECK(lb_fun(p->reg[0])->entry.fun==&target);
        CHECK(finish(p)==LB_PROCESS_DONE); LbBitstringView view;
        CHECK(lb_bitstring_view(lb_process_result(p),&view)); const char *expected=different?"different":"equal";
        CHECK(view.bit_size==strlen(expected)*8 && !memcmp(view.data,expected,strlen(expected)));
        CHECK(lb_process_collect(p,0)==LB_PROCESS_DONE && !p->fun_leases && module->users==users);
        lb_process_destroy(p);
    }
    lb_code_entry_release(compare);
    LbCodeEntry *callee=entry(space,target_module,"captured_pair",2);
    LbFunEntry callable={.dispatch={.addresses={callee->entry->dispatch.addresses[0]}},
        .module=target_module->name,.arity=1,.owner=target_module};
    lb_code_entry_release(callee); /* only the closure/process lease may pin target code */
    for(size_t badarity=0;badarity<2;++badarity) {
        LbCodeEntry *invoke=entry(space,module,badarity?"invoke0":"invoke",badarity?1:2);
        LbProcess *p=create(invoke); CHECK(lb_fun_retain(p,&callable)==LB_ALLOC_OK);
        unsigned char bytes[65]; memset(bytes,0x74,sizeof(bytes)); Eterm binary;
        CHECK(lb_bitstring_build(space->domain,&p->off_heap,&p->htop,bytes,520,0,&binary)==LB_ALLOC_OK);
        LbFun *fun=(LbFun *)p->htop; p->htop+=LB_FUN_WORDS+1;
        fun->thing_word=LB_FUN_HEADER(1,1); fun->entry.fun=&callable; fun->env[0]=binary;
        p->reg[0]=make_boxed((Eterm *)fun); p->reg[1]=make_small(42);
        CHECK(lb_process_collect(p,0)==LB_PROCESS_READY);
        CHECK(lb_code_unload(target_module)==LB_CODE_BUSY);
        if(!badarity) {
            for(size_t i=0;i<100 && *p->i!=op_call_bif_W;++i) CHECK(lb_process_run(p,1,128)==LB_PROCESS_YIELDED);
            CHECK(*p->i==op_call_bif_W); /* current PC is native; only the saved target continuation pins code */
            while(p->htop<p->stop-S_RESERVED-2) *p->htop++=NIL;
        }
        size_t collected=p->collections;
        CHECK(finish(p)==(badarity?LB_PROCESS_EXCEPTION:LB_PROCESS_DONE));
        if(!badarity) CHECK(p->collections>collected); /* collection while executing foreign defining code */
        if(badarity) CHECK(lb_process_exception(p)==am_badarity);
        else {
            Eterm *result=tuple_val(lb_process_result(p)); LbBitstringView view;
            CHECK(arityval(result[0])==3 && result[1]==make_small(42) && lb_is_reference(result[3]));
            CHECK(lb_bitstring_view(result[2],&view) && view.bit_size==520 && !memcmp(view.data,bytes,65));
        }
        CHECK(lb_process_collect(p,0)==p->status); lb_process_destroy(p); lb_code_entry_release(invoke);
    }
    CHECK(!target_module->users);
    LbCodeEntry *invoke=entry(space,module,"invoke0",1); LbProcess *p=create(invoke);
    CHECK(finish(p)==LB_PROCESS_EXCEPTION && lb_process_exception(p)==am_badfun);
    lb_process_destroy(p); lb_code_entry_release(invoke);
}
static void references(LbCodeSpace *space,LbCodeModule *module,Memory *m)
{
    LbEngine *engine=space->engine;
    LbCodeEntry *ref=entry(space,module,"reference",0),*pair=entry(space,module,"reference_pair",0);
    LbProcess *p=create(pair); uint64_t first=engine->reference_counter;
    CHECK(finish(p)==LB_PROCESS_DONE); Eterm *tuple=tuple_val(lb_process_result(p));
    CHECK(arityval(tuple[0])==2); const LbOrdinaryRef *a=lb_reference(tuple[1]),*b=lb_reference(tuple[2]);
    CHECK(a && b && a->num[0]==(first&0x3ffff) && b->num[0]==((first+1)&0x3ffff));
    int equal=-1; CHECK(lb_term_equal(space->domain,tuple[1],tuple[2],&equal)==LB_ALLOC_OK && !equal);
    size_t words=0; CHECK(lb_flat_size(space->domain,lb_process_result(p),&words) && words==9);
    LbCodeEntry *classify=entry(space,module,"reference_classify",1); LbProcess *consumer=create(classify);
    CHECK(lb_heap_reserve(consumer,words,0)); consumer->reg[0]=lb_copy_flat(tuple[1],&consumer->htop,&consumer->off_heap);
    CHECK(lb_term_equal(space->domain,tuple[1],consumer->reg[0],&equal)==LB_ALLOC_OK && equal);
    CHECK(lb_process_collect(p,512)==LB_PROCESS_DONE); tuple=tuple_val(lb_process_result(p));
    CHECK(lb_term_equal(space->domain,tuple[1],consumer->reg[0],&equal)==LB_ALLOC_OK && equal);
    LbCodeEntry *compare=entry(space,module,"reference_equal",2);
    for(size_t different=0;different<2;++different) {
        LbProcess *c=create(compare); CHECK(lb_heap_reserve(c,2*LB_REF_WORDS,0));
        c->reg[0]=lb_copy_flat(tuple[1],&c->htop,&c->off_heap);
        c->reg[1]=lb_copy_flat(tuple[1+different],&c->htop,&c->off_heap);
        CHECK(lb_process_collect(c,128)==LB_PROCESS_READY && finish(c)==LB_PROCESS_DONE);
        LbBitstringView result; CHECK(lb_bitstring_view(lb_process_result(c),&result));
        const char *expected=different?"different":"equal";
        CHECK(result.bit_size==8*strlen(expected) && !memcmp(result.data,expected,strlen(expected)));
        lb_process_destroy(c);
    }
    lb_code_entry_release(compare);
    CHECK(finish(consumer)==LB_PROCESS_DONE); LbBitstringView view;
    CHECK(lb_bitstring_view(lb_process_result(consumer),&view) && view.bit_size==24 && !memcmp(view.data,"ref",3));
    lb_process_destroy(consumer); lb_process_destroy(p);
    consumer=create(classify); CHECK(finish(consumer)==LB_PROCESS_DONE);
    CHECK(lb_bitstring_view(lb_process_result(consumer),&view) && view.bit_size==56);
    lb_process_destroy(consumer); lb_code_entry_release(classify);
    /* Force a moving collection in the actual make_ref trampoline. Failed
     * tospace allocation must not consume the Engine's reference number. */
    for(size_t fail=0;fail<2;++fail) {
        p=create(ref);
        while(p->htop<p->stop-S_RESERVED-2) *p->htop++=NIL;
        uint64_t before=engine->reference_counter;
        if(fail) m->fail=m->calls+1;
        CHECK(finish(p)==(fail?LB_PROCESS_NO_MEMORY:LB_PROCESS_DONE));
        CHECK(engine->reference_counter==before+(fail?0:1));
        if(!fail) CHECK(p->collections && lb_reference(lb_process_result(p)));
        m->fail=0; lb_process_destroy(p);
    }
    const uint64_t boundaries[]={UINT64_C(0x3ffff),UINT64_C(0x40000),UINT64_C(0xffffffff),UINT64_C(0x100000000),UINT64_MAX-1};
    uint64_t saved=engine->reference_counter;
    for(size_t i=0;i<5;++i) {
        engine->reference_counter=boundaries[i]; p=create(ref); CHECK(finish(p)==LB_PROCESS_DONE);
        a=lb_reference(lb_process_result(p));
        CHECK(a->num[0]==(boundaries[i]&0x3ffff) && a->num[1]==((boundaries[i]&0xfffc0000)|1) && a->num[2]==(boundaries[i]>>32));
        lb_process_destroy(p);
    }
    p=create(ref); size_t calls=m->calls; CHECK(finish(p)==LB_PROCESS_LIMIT && m->calls==calls);
    CHECK(engine->reference_counter==UINT64_MAX && lb_process_exception(p)==THE_NON_VALUE); lb_process_destroy(p);
    engine->reference_counter=saved; /* test-only injection; all generated refs destroyed */
    lb_code_entry_release(ref); lb_code_entry_release(pair);
}
static void fun_load_failures(LbCodeSpace *space,const char *path,Memory *m)
{
    FILE *file=fopen(path,"rb"); CHECK(file && !fseek(file,0,SEEK_END)); long size=ftell(file);
    CHECK(size>0 && !fseek(file,0,SEEK_SET)); unsigned char *bytes=malloc((size_t)size);
    CHECK(bytes && fread(bytes,1,(size_t)size,file)==(size_t)size && !fclose(file));
    size_t live=m->live,atoms=lb_atoms_count(space->atoms);
    for(size_t prefix=1;;++prefix) {
        CHECK(prefix<1000); LbCodeModule *module=NULL; LbBeamError error;
        m->fail=m->calls+prefix;
        LbCodeStatus status=lb_code_load(space,bytes,(size_t)size,&module,&error); m->fail=0;
        if(status==LB_CODE_OK) { CHECK(lb_code_unload(module)==LB_CODE_OK); break; }
        CHECK(status==LB_CODE_NO_MEMORY && !module && !space->loading);
        CHECK(m->live==live && lb_atoms_count(space->atoms)==atoms);
    }
    free(bytes);
}
static void prepared_children(LbCodeSpace *space,LbCodeModule *module,Memory *m)
{
    unsigned char bytes[65]; memset(bytes,0x68,sizeof(bytes));
    for(size_t nested=0;nested<2;++nested) {
        LbCodeEntry *e=entry(space,module,nested?"process_nested":"process_factory",1);
        LbProcess *parent,*child;
        CHECK(lb_process_create_binary(e,bytes,65,32,&parent)==LB_PROCESS_READY && finish(parent)==LB_PROCESS_DONE);
        Eterm closure=lb_process_result(parent),parent_pid=parent->identity.id;
        CHECK(lb_fun(closure) && lb_fun_arity(lb_fun(closure))==0);
        CHECK(!lb_pid_lookup(space->pids,parent_pid));
        size_t live=m->live,members=lb_pid_count(space->pids),users=module->users;
        for(size_t prefix=1;;++prefix) {
            CHECK(prefix<100); m->fail=m->calls+prefix;
            LbProcessStatus status=lb_process_create_fun(parent,closure,16,&child); m->fail=0;
            if(status==LB_PROCESS_READY) break;
            CHECK(status==LB_PROCESS_NO_MEMORY && !child);
            CHECK(m->live==live && module->users==users && lb_pid_count(space->pids)==members);
        }
        Eterm child_pid=child->identity.id;
        CHECK(child_pid!=parent_pid && lb_pid_lookup(space->pids,child_pid)==child);
        CHECK(child->current==&child->local_mfa && child->current->arity==lb_fun_free(lb_fun(closure)));
        lb_process_destroy(parent); // only cloned environment/code ownership may survive
        CHECK(lb_process_collect(child,1024)==LB_PROCESS_READY);
        CHECK(finish(child)==LB_PROCESS_DONE && !lb_pid_lookup(space->pids,child_pid));
        Eterm result=lb_process_result(child); CHECK(is_boxed(result) && is_arity_value(*boxed_val(result)));
        Eterm *tuple=boxed_val(result); CHECK(arityval(tuple[0])==3 && tuple[1]==parent_pid && tuple[2]==child_pid);
        Eterm payload=tuple[3]; if(nested) { CHECK(arityval(*boxed_val(payload))==2); payload=boxed_val(payload)[2]; }
        LbBitstringView view; CHECK(lb_bitstring_view(payload,&view) && view.bit_size==520 && !memcmp(view.data,bytes,65));
        lb_process_destroy(child); lb_code_entry_release(e);
    }
    LbCodeEntry *e=entry(space,module,"factory",1); LbProcess *parent,*child=(void *)1;
    CHECK(lb_process_create_binary(e,bytes,65,32,&parent)==LB_PROCESS_READY && finish(parent)==LB_PROCESS_DONE);
    size_t live=m->live;
    CHECK(lb_process_create_fun(parent,lb_process_result(parent),32,&child)==LB_PROCESS_INVALID && !child && m->live==live);
    CHECK(lb_process_create_fun(parent,NIL,32,&child)==LB_PROCESS_INVALID && !child && m->live==live);
    lb_process_destroy(parent); lb_code_entry_release(e);
}
static void guest_funs(LbCodeSpace *space,LbCodeModule *module,Memory *m)
{
    const char *functions[]={"echo","nested","closed","badarity","factory","closed_factory"};
    unsigned char bytes[65]; memset(bytes,0x63,sizeof(bytes));
    for(size_t i=0;i<6;++i) {
        LbCodeEntry *e=entry(space,module,functions[i],1); LbProcess *p;
        CHECK(lb_process_create_binary(e,bytes,sizeof(bytes),32,&p)==LB_PROCESS_READY);
        CHECK(finish(p)==(i==3?LB_PROCESS_EXCEPTION:LB_PROCESS_DONE));
        if(i==3) CHECK(lb_process_exception(p)==am_badarity);
        else if(i>=4) {
            const LbFun *fun=lb_fun(lb_process_result(p)); CHECK(fun && fun->entry.fun->owner==module);
            CHECK(lb_fun_free(fun)==(i==4?1:0));
            CHECK(lb_code_unload(module)==LB_CODE_BUSY);
        } else {
            LbBitstringView view; CHECK(lb_bitstring_view(lb_process_result(p),&view));
            if(i==2) CHECK(view.bit_size==48 && !memcmp(view.data,"closed",6));
            else CHECK(view.bit_size==520 && !memcmp(view.data,bytes,65));
        }
        CHECK(lb_process_collect(p,0)==p->status); lb_process_destroy(p); lb_code_entry_release(e);
    }
    /* Native constructor and literal producers both acquire leases before
     * publication; inject their first lease allocation and retry from scratch. */
    for(size_t closed=0;closed<2;++closed) {
        LbCodeEntry *e=entry(space,module,closed?"closed_factory":"factory",1);
        LbProcess *p; CHECK(lb_process_create_binary(e,bytes,65,64,&p)==LB_PROCESS_READY);
        m->fail=m->calls+1; CHECK(finish(p)==LB_PROCESS_NO_MEMORY && !p->fun_leases);
        CHECK(lb_process_result(p)==THE_NON_VALUE); m->fail=0; lb_process_destroy(p);
        CHECK(lb_process_create_binary(e,bytes,65,64,&p)==LB_PROCESS_READY && finish(p)==LB_PROCESS_DONE);
        CHECK(lb_fun(lb_process_result(p))); lb_process_destroy(p); lb_code_entry_release(e);
    }
}
int main(int argc,char **argv)
{
    CHECK(lb_instruction_supported(op_self_x) && lb_instruction_supported(op_self_y) && lb_instruction_supported(op_is_pid_fx) && lb_instruction_supported(op_i_make_fun3_Fdtt));
    if(argc==1) { puts("CORE_PID_GATE_OK"); return 0; } CHECK(argc==4);
    Memory m={0}; LbSystemAllocator allocator={&m,allocate,release}; LbEngine *engine; LbCodeSpace *a,*b;
    CHECK(lb_engine_create(&allocator,&engine)==LB_ENGINE_OK && lb_code_space_create(engine,&a)==LB_CODE_OK && lb_code_space_create(engine,&b)==LB_CODE_OK);
    LbCodeModule *ma=load(a,argv[1]),*mb=load(b,argv[1]); LbCodeEntry *ea=entry(a,ma,"identity",0),*eb=entry(b,mb,"identity",0);
    LbCodeModule *target_module=load(a,argv[2]);
    table(a,ea,&m); permuted_ring(a,ea); references(a,ma,&m); fun_consumers(a,ma,target_module,&m);
    CHECK(lb_code_unload(target_module)==LB_CODE_OK);
    LbProcess *failed=create(ea); size_t before=m.live; m.fail=m.calls+1;
    CHECK(finish(failed)==LB_PROCESS_NO_MEMORY && !a->pids && !failed->identity.owner && m.live==before);
    m.fail=0; lb_process_destroy(failed);
    LbProcess *pa=create(ea),*pb=create(eb);
    CHECK(lb_process_run(pa,1,128)==LB_PROCESS_YIELDED && lb_process_run(pb,1,128)==LB_PROCESS_YIELDED);
    Eterm pid=pa->reg[0]; CHECK(is_internal_pid(pid) && pid==pa->identity.id);
    CHECK(a->pids!=b->pids && lb_pid_lookup(a->pids,pid)==pa && lb_pid_lookup(b->pids,pb->identity.id)==pb);
    CHECK(lb_pid_remove(b->pids,&pa->identity)==LB_PID_INVALID);
    CHECK(finish(pa)==LB_PROCESS_DONE && finish(pb)==LB_PROCESS_DONE);
    CHECK(!lb_pid_lookup(a->pids,pid) && !lb_pid_lookup(b->pids,pb->identity.id));
    CHECK(lb_process_collect(pa,256)==LB_PROCESS_DONE && lb_process_result(pa)==pid);
    lb_process_destroy(pa); CHECK(!lb_pid_lookup(a->pids,pid) && lb_pid_count(a->pids)==0);
    pa=create(ea); CHECK(finish(pa)==LB_PROCESS_DONE && lb_process_result(pa)!=pid);
    CHECK(lb_pid_insert(b->pids,&pa->identity,pa)==LB_PID_INVALID);
    lb_process_destroy(pa); lb_process_destroy(pb);
    LbCodeEntry *pair=entry(a,ma,"pair",0); pa=create(pair); CHECK(finish(pa)==LB_PROCESS_DONE);
    Eterm *tuple=tuple_val(lb_process_result(pa)); CHECK(arityval(tuple[0])==2 && tuple[1]==pa->identity.id && tuple[2]==tuple[1]);
    CHECK(lb_process_collect(pa,128)==LB_PROCESS_DONE); lb_process_destroy(pa); lb_code_entry_release(pair);
    LbCodeEntry *classify=entry(a,ma,"classify",1); pa=create(ea); CHECK(finish(pa)==LB_PROCESS_DONE);
    LbProcess *consumer=create(classify); consumer->reg[0]=pa->identity.id;
    CHECK(finish(consumer)==LB_PROCESS_DONE); LbBitstringView view; CHECK(lb_bitstring_view(lb_process_result(consumer),&view) && view.bit_size==24 && !memcmp(view.data,"pid",3));
    lb_process_destroy(consumer); lb_process_destroy(pa); consumer=create(classify);
    CHECK(finish(consumer)==LB_PROCESS_DONE && lb_bitstring_view(lb_process_result(consumer),&view) && view.bit_size==56 && !memcmp(view.data,"not_pid",7));
    lb_process_destroy(consumer); lb_code_entry_release(classify);
    /* The real self instruction, not just the table API, refuses exhaustion
     * as infrastructure and leaves its destination/exception fields untouched. */
    LbProcess **held=malloc(1025*sizeof(*held)); CHECK(held);
    for(size_t i=0;i<1025;++i) held[i]=create(ea);
    for(size_t i=0;i<1024;++i) CHECK(lb_process_run(held[i],1,128)==LB_PROCESS_YIELDED);
    CHECK(lb_pid_count(a->pids)==1024 && finish(held[1024])==LB_PROCESS_LIMIT);
    CHECK(held[1024]->reg[0]==NIL && lb_process_exception(held[1024])==THE_NON_VALUE);
    for(size_t i=0;i<1025;++i) lb_process_destroy(held[i]); free(held); CHECK(!lb_pid_count(a->pids));
    fun_load_failures(a,argv[3],&m);
    LbCodeModule *fun_module=load(a,argv[3]); guest_funs(a,fun_module,&m); prepared_children(a,fun_module,&m);
    CHECK(lb_code_unload(fun_module)==LB_CODE_OK);
    lb_code_entry_release(ea); lb_code_entry_release(eb);
    CHECK(lb_code_unload(ma)==LB_CODE_OK && lb_code_unload(mb)==LB_CODE_OK);
    CHECK(lb_code_space_destroy(a)==LB_CODE_OK && lb_code_space_destroy(b)==LB_CODE_OK && lb_engine_shutdown(engine)==LB_ENGINE_OK);
    lb_engine_release(engine); CHECK(!m.live); puts("CORE_PID_OK self_execution=true real_process_directory=true gc=true stale_pid_rejected=true isolated_directories=true oom_retry=true physical_retirement=true"); return 0;
}
