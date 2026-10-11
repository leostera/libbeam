/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#define _GNU_SOURCE
#define _DARWIN_C_SOURCE
#include "term_compare.h"
#include "process_internal.h"
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"CHECK %s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while(0)
typedef struct { size_t calls,fail,live,bytes; } Memory;
typedef union { max_align_t alignment; size_t size; } Header;
static void *allocate(void *context,size_t size)
{
    Memory *m=context; Header *h;
    if(++m->calls==m->fail) return NULL;
    h=malloc(sizeof(*h)+size); if(!h) return NULL;
    h->size=size; ++m->live; m->bytes+=size; return h+1;
}
static void release(void *context,void *pointer)
{
    Memory *m=context; Header *h=(Header *)pointer-1;
    CHECK(m->live && m->bytes>=h->size); --m->live; m->bytes-=h->size;
    memset(pointer,0xa5,h->size); free(h);
}
static Eterm tree(Eterm *storage,size_t depth)
{
    size_t i; Eterm result=make_small(7);
    for(i=0;i<depth;++i) { result=TUPLE2(storage,result,make_small(i)); storage+=3; }
    return result;
}
static void equal(LbAllocDomain *domain,Eterm a,Eterm b,int expected)
{
    int same=-1; CHECK(lb_term_equal(domain,a,b,&same)==LB_ALLOC_OK && same==expected);
    same=-1; CHECK(lb_term_equal(domain,b,a,&same)==LB_ALLOC_OK && same==expected);
}
static void components(void)
{
    Memory m={0}; LbSystemAllocator allocator={&m,allocate,release}; LbAllocDomain *domain;
    Eterm a[384],b[384],av,bv,empty_a[2],empty_b[2],list_a[2],list_b[2]; size_t i,calls,live,bytes;
    Eterm big_a[]={_make_header(1,POS_BIG_SUBTAG),UINT64_C(1)<<63};
    Eterm big_b[]={_make_header(1,POS_BIG_SUBTAG),UINT64_C(1)<<63};
    Eterm float_a[]={_make_header(1,FLOAT_SUBTAG),0},float_b[]={_make_header(1,FLOAT_SUBTAG),0};
    CHECK(lb_alloc_domain_create(&allocator,&domain)==LB_ALLOC_OK);
    equal(domain,NIL,NIL,1); equal(domain,NIL,make_small(0),0);
    equal(domain,make_atom(10),make_atom(10),1); equal(domain,make_atom(10),make_small(10),0);
    equal(domain,make_boxed(big_a),make_boxed(big_b),1); big_b[1]++;
    equal(domain,make_boxed(big_a),make_boxed(big_b),0);
    equal(domain,make_boxed(float_a),make_boxed(float_b),1); float_b[1]=UINT64_C(1)<<63;
    equal(domain,make_boxed(float_a),make_boxed(float_b),0);
    equal(domain,make_small(0),make_boxed(float_a),0);
    CHECK(lb_term_tuple(empty_a,2,NULL,0,&av) && lb_term_tuple(empty_b,2,NULL,0,&bv)); equal(domain,av,bv,1);
    av=CONS(list_a,make_small(1),make_atom(3)); bv=CONS(list_b,make_small(1),make_atom(3));
    equal(domain,av,bv,1); list_b[1]=NIL; equal(domain,av,bv,0);
    av=tree(a,128); bv=tree(b,128); calls=m.calls; live=m.live; bytes=m.bytes;
    { int same; CHECK(lb_term_equal(domain,av,bv,&same)==LB_ALLOC_OK && same); }
    calls=m.calls-calls; CHECK(calls>1 && m.live==live && m.bytes==bytes);
    for(i=1;i<=calls;++i) {
        int same=42; m.fail=m.calls+i;
        CHECK(lb_term_equal(domain,av,bv,&same)==LB_ALLOC_NO_MEMORY && same==42);
        CHECK(m.live==live && m.bytes==bytes); m.fail=0; equal(domain,av,bv,1);
    }
    equal(domain,av,av|TAG_LITERAL_PTR,1);
    b[1]=make_small(8); equal(domain,av,bv,0);
    av=bv=make_small(7);
    for(i=0;i<128;++i) { av=CONS(a+2*i,av,NIL); bv=CONS(b+2*i,bv,NIL); }
    equal(domain,av,bv,1); b[0]=make_small(8); equal(domain,av,bv,0);
    {
        unsigned char data[65]; Eterm heap[32],*top=heap,small,large; LbOffHeap offheap={0};
        memset(data,0xff,sizeof(data));
        CHECK(lb_bitstring_build(domain,&offheap,&top,data,512,0,&small)==LB_ALLOC_OK);
        CHECK(lb_bitstring_build(domain,&offheap,&top,data,520,0,&large)==LB_ALLOC_OK);
        ((LbSubBits *)boxed_val(large))->end=512; equal(domain,small,large,1);
        ((LbSubBits *)boxed_val(large))->start=3; boxed_val(small)[1]=509;
        equal(domain,small,large,1); lb_offheap_clear(&offheap);
    }
    CHECK(m.live==live && m.bytes==bytes && lb_alloc_domain_destroy(domain)==LB_ALLOC_OK);
    CHECK(!m.live && !m.bytes);
    printf("CORE_COMPARE_COMPONENT_OK deep_terms=true scratch_failure_prefixes=%zu retry=true signed_zero=true improper_lists=true\n",calls);
}
static void bit(unsigned char *p,size_t n,unsigned value)
{
    unsigned char mask=(unsigned char)(1u<<(7-(n&7)));
    if(value) p[n/8]|=mask; else p[n/8]&=(unsigned char)~mask;
}
static void bits(void)
{
    unsigned char a[40],b[40]; size_t ao,bo,n,i;
    for(ao=0;ao<16;++ao) for(bo=0;bo<16;++bo) for(n=0;n<=257;++n) {
        LbBitstringView av={a,ao,n},bv={b,bo,n};
        memset(a,0xa5,sizeof(a)); memset(b,0x5a,sizeof(b));
        for(i=0;i<n;++i) { unsigned value=(unsigned)((i*13+i/7)&1); bit(a,ao+i,value); bit(b,bo+i,value); }
        CHECK(lb_bitstrings_equal(av,bv));
        if(n) { b[(bo+n/2)/8]^=(unsigned char)(1u<<(7-((bo+n/2)&7))); CHECK(!lb_bitstrings_equal(av,bv)); }
    }
    {
        size_t page=(size_t)sysconf(_SC_PAGESIZE); void *ma,*mb; unsigned char *ap,*bp;
        CHECK(page>=80);
        ma=mmap(NULL,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        mb=mmap(NULL,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        CHECK(ma!=MAP_FAILED && mb!=MAP_FAILED);
        CHECK(!mprotect((unsigned char *)ma+page,page,PROT_NONE) && !mprotect((unsigned char *)mb+page,page,PROT_NONE));
        ap=(unsigned char *)ma+page-80; bp=(unsigned char *)mb+page-80;
        memset(ap,0xbd,80); memset(bp,0xbd,80);
        for(i=0;i<8;++i) {
            LbBitstringView av={ap,i,640-i},bv={bp,i,640-i};
            CHECK(lb_bitstrings_equal(av,bv)); bp[79]^=1;
            CHECK(!lb_bitstrings_equal(av,bv)); bp[79]^=1;
        }
        CHECK(!munmap(ma,page*2) && !munmap(mb,page*2));
    }
    puts("CORE_COMPARE_BITS_OK offsets=16 lengths=258 unequal=true exact_payload_guard_pages=true");
}
static Eterm atom(LbCodeSpace *space,const char *name)
{
    Eterm value; CHECK(lb_atoms_find(space->atoms,name,strlen(name),&value)==LB_ATOM_OK); return value;
}
static LbProcess *comparison(LbCodeSpace *space,LbCodeModule *module,const char *function,int different)
{
    LbCodeEntry *entry; LbProcess *process; Eterm args[2]={NIL,NIL};
    CHECK(lb_code_entry_acquire(space,module->name,atom(space,function),2,&entry)==LB_CODE_OK);
    CHECK(lb_process_create(entry,args,2,800,&process)==LB_PROCESS_READY); lb_code_entry_release(entry);
    /* Real owned heap terms, not host pointers or a second instruction evaluator. */
    process->reg[0]=tree(process->htop,128); process->htop+=384;
    process->reg[1]=tree(process->htop,128);
    if(different) process->htop[1]=make_small(8);
    process->htop+=384; return process;
}
static void result(LbProcess *p,const char *expected)
{
    LbBitstringView view; size_t i;
    for(i=0;i<100 && (p->status==LB_PROCESS_READY || p->status==LB_PROCESS_YIELDED);++i)
        lb_process_run(p,1,4);
    CHECK(p->status==LB_PROCESS_DONE && lb_bitstring_view(lb_process_result(p),&view));
    CHECK(!view.bit_offset && view.bit_size==strlen(expected)*8 && !memcmp(view.data,expected,strlen(expected)));
}
static void literal_branches(LbCodeSpace *space,LbCodeModule *module,Memory *memory)
{
    const char *functions[]={"literal","not_literal"}; size_t f;
    for(f=0;f<2;++f) {
        LbCodeEntry *entry; LbProcess *p; Eterm argument=NIL;
        CHECK(lb_code_entry_acquire(space,module->name,atom(space,functions[f]),1,&entry)==LB_CODE_OK);
        CHECK(lb_process_create(entry,&argument,1,40,&p)==LB_PROCESS_READY);
        p->reg[0]=tree(p->htop,8); p->htop+=24;
        memory->fail=memory->calls+1; CHECK(lb_process_run(p,100,100)==LB_PROCESS_NO_MEMORY);
        memory->fail=0; lb_process_destroy(p);
        CHECK(lb_process_create(entry,&argument,1,40,&p)==LB_PROCESS_READY);
        p->reg[0]=tree(p->htop,8); p->htop+=24;
        result(p,"equal"); lb_process_destroy(p);
        CHECK(lb_process_create(entry,&argument,1,40,&p)==LB_PROCESS_READY);
        result(p,"different"); lb_process_destroy(p); lb_code_entry_release(entry);
    }
    {
        LbCodeEntry *entry; LbProcess *p; Eterm argument=NIL;
        CHECK(lb_code_entry_acquire(space,module->name,atom(space,"nilish"),1,&entry)==LB_CODE_OK);
        CHECK(lb_process_create(entry,&argument,1,40,&p)==LB_PROCESS_READY);
        result(p,"nil"); lb_process_destroy(p); lb_code_entry_release(entry);
        CHECK(lb_code_entry_acquire(space,module->name,atom(space,"empty"),1,&entry)==LB_CODE_OK);
        CHECK(lb_process_create(entry,&argument,1,40,&p)==LB_PROCESS_READY);
        CHECK(lb_process_run(p,100,100)==LB_PROCESS_EXCEPTION);
        CHECK(lb_process_exception(p)==atom(space,"function_clause"));
        lb_process_destroy(p); lb_code_entry_release(entry);
    }
}
static void execution(const char *path)
{
    Memory m={0}; LbSystemAllocator allocator={&m,allocate,release}; LbEngine *engine;
    LbCodeSpace *space; LbCodeModule *module; LbBeamError error; LbProcess *p;
    FILE *file=fopen(path,"rb"); long length; unsigned char *data; size_t i,calls,live,bytes;
    CHECK(file && !fseek(file,0,SEEK_END)); length=ftell(file); CHECK(length>0 && length<8*1024*1024 && !fseek(file,0,SEEK_SET));
    data=malloc((size_t)length); CHECK(data && fread(data,1,(size_t)length,file)==(size_t)length && !fclose(file));
    CHECK(lb_engine_create(&allocator,&engine)==LB_ENGINE_OK && lb_code_space_create(engine,&space)==LB_CODE_OK);
    { LbCodeStatus status=lb_code_load(space,data,(size_t)length,&module,&error);
      if(status!=LB_CODE_OK) fprintf(stderr,"load status=%d stage=%s\n",status,error.stage);
      CHECK(status==LB_CODE_OK); } free(data);
    p=comparison(space,module,"same",0); CHECK(lb_process_collect(p,0)==LB_PROCESS_READY);
    calls=m.calls; result(p,"equal"); calls=m.calls-calls; CHECK(calls>1); lb_process_destroy(p);
    p=comparison(space,module,"same",1); result(p,"different"); lb_process_destroy(p);
    p=comparison(space,module,"different",0); result(p,"equal"); lb_process_destroy(p);
    p=comparison(space,module,"different",1); result(p,"different"); lb_process_destroy(p);
    for(i=1;i<=calls*2;++i) {
        const char *function=i<=calls?"same":"different";
        p=comparison(space,module,function,0); live=m.live; bytes=m.bytes; m.fail=m.calls+(i-1)%calls+1;
        CHECK(lb_process_run(p,100,100)==LB_PROCESS_NO_MEMORY);
        CHECK(m.live==live && m.bytes==bytes && lb_process_result(p)==THE_NON_VALUE);
        m.fail=0; lb_process_destroy(p);
        p=comparison(space,module,function,0); result(p,"equal"); lb_process_destroy(p);
    }
    literal_branches(space,module,&m);
    CHECK(lb_code_unload(module)==LB_CODE_OK && lb_code_space_destroy(space)==LB_CODE_OK);
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine); CHECK(!m.live && !m.bytes);
    puts("CORE_COMPARE_EXECUTION_OK generated_eq_ne=true deep_gc_roots=true scratch_oom_not_inequality=true retry=true");
}
int main(int argc,char **argv)
{
    components(); bits(); CHECK(argc==1 || argc==2); if(argc==2) execution(argv[1]); return 0;
}
