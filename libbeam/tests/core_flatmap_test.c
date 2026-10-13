/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#include "process_internal.h"
#include "flatmap.h"
#include "binary_error.h"
#include "term_compare.h"
#include "writable.h"
#include <stdio.h>
#include <inttypes.h>
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
static Eterm map(Eterm *storage,Eterm child)
{
    return lb_map3(storage,am_cause,child,am_function,make_small(7),am_module,NIL);
}
static void components(void)
{
    Memory m={0}; LbSystemAllocator cb={&m,allocate,release}; LbAllocDomain *domain;
    Eterm a[128*LB_MAP3_WORDS],b[128*LB_MAP3_WORDS],av=NIL,bv=NIL;
    size_t i,calls,before,live,bytes,words=0; int equal;
    CHECK(lb_alloc_domain_create(&cb,&domain)==LB_ALLOC_OK);
    for(i=0;i<128;++i) { av=map(a+i*LB_MAP3_WORDS,av); bv=map(b+i*LB_MAP3_WORDS,bv); }
    live=m.live; bytes=m.bytes; before=m.calls;
    CHECK(lb_term_equal(domain,av,bv,&equal)==LB_ALLOC_OK && equal); calls=m.calls-before;
    CHECK(calls && m.live==live && m.bytes==bytes);
    for(i=1;i<=calls;++i) {
        equal=77; m.fail=m.calls+i;
        CHECK(lb_term_equal(domain,av,bv,&equal)==LB_ALLOC_NO_MEMORY && equal==77);
        CHECK(m.live==live && m.bytes==bytes); m.fail=0;
        CHECK(lb_term_equal(domain,av,bv,&equal)==LB_ALLOC_OK && equal);
    }
    b[3]=make_small(1);
    CHECK(lb_term_equal(domain,av,bv,&equal)==LB_ALLOC_OK && !equal); b[3]=NIL;
    before=m.calls; CHECK(lb_flat_size(domain,av,&words) && words==128*LB_MAP3_WORDS);
    calls=m.calls-before; CHECK(calls);
    for(i=1;i<=calls;++i) {
        words=99; m.fail=m.calls+i;
        CHECK(!lb_flat_size(domain,av,&words) && words==99 && m.live==live && m.bytes==bytes); m.fail=0;
    }
    {
        Eterm *storage=malloc(sizeof(a)),*top=storage,result; LbOffHeap offheap={0};
        CHECK(storage); result=lb_copy_flat(av,&top,&offheap);
        CHECK(!is_non_value(result) && top-storage==128*LB_MAP3_WORDS && !offheap.first);
        CHECK(lb_term_equal(domain,av,result,&equal)==LB_ALLOC_OK && equal); free(storage);
    }
    /* Native flatmap sizes 0..32, including equal/unequal key tuples and sizes. */
    for(i=0;i<=LB_FLATMAP_MAX_SIZE;++i) {
        Eterm x[LB_FLATMAP_WORDS+2*LB_FLATMAP_MAX_SIZE+2]={LB_FLATMAP_HEADER};
        Eterm y[LB_FLATMAP_WORDS+2*LB_FLATMAP_MAX_SIZE+2]={LB_FLATMAP_HEADER};
        Eterm *kx=x+LB_FLATMAP_WORDS+i,*ky=y+LB_FLATMAP_WORDS+i; size_t j;
        x[1]=y[1]=i; kx[0]=ky[0]=i?make_arityval(i):make_arityval_zero();
        if(!i) kx[1]=ky[1]=THE_NON_VALUE;
        for(j=0;j<i;++j) { kx[j+1]=ky[j+1]=make_small(j); x[j+3]=y[j+3]=make_small(j+10); }
        x[2]=make_tuple(kx); y[2]=make_tuple(ky);
        CHECK(lb_term_equal(domain,make_boxed(x),make_boxed(y),&equal)==LB_ALLOC_OK && equal);
        CHECK(lb_flat_size(domain,make_boxed(x),&words) && words==LB_FLATMAP_WORDS+i+(i?i+1:2));
        if(i) { ky[i]=make_small(99); CHECK(lb_term_equal(domain,make_boxed(x),make_boxed(y),&equal)==LB_ALLOC_OK && !equal); }
        y[1]=i==LB_FLATMAP_MAX_SIZE?i-1:i+1; ky[0]=make_arityval(y[1]);
        for(j=0;j<y[1];++j) { ky[j+1]=make_small(j); y[j+3]=make_small(j+10); }
        CHECK(lb_term_equal(domain,make_boxed(x),make_boxed(y),&equal)==LB_ALLOC_OK && !equal);
        CHECK(lb_term_equal(domain,make_boxed(x),make_small(0),&equal)==LB_ALLOC_OK && !equal);
    }
    {
        unsigned char data[65]={0}; Eterm binaries[16],*hp=binaries,left,right,owner[10],copied[64],*cp=copied;
        LbOffHeap source={0},destination={0}; LbBinRef *first,*second; Eterm object;
        CHECK(lb_bitstring_build(domain,&source,&hp,data,520,0,&left)==LB_ALLOC_OK);
        CHECK(lb_bitstring_build(domain,&source,&hp,data,520,0,&right)==LB_ALLOC_OK);
        first=(LbBinRef *)boxed_val(((LbSubBits *)boxed_val(left))->orig);
        second=(LbBinRef *)boxed_val(((LbSubBits *)boxed_val(right))->orig);
        object=lb_map3(owner,am_cause,left,am_function,right,am_module,NIL);
        atomic_store(&second->val->intern.refc,UINTPTR_MAX);
        CHECK(is_non_value(lb_copy_flat(object,&cp,&destination)) && cp==copied && !destination.first && !destination.overhead);
        CHECK(atomic_load(&first->val->intern.refc)==1 && atomic_load(&second->val->intern.refc)==UINTPTR_MAX);
        atomic_store(&second->val->intern.refc,1); lb_offheap_clear(&source);
        CHECK(m.live==live && m.bytes==bytes);
    }
    CHECK(lb_alloc_domain_destroy(domain)==LB_ALLOC_OK && !m.live && !m.bytes);
    puts("CORE_FLATMAP_COMPONENT_OK native_layout=true sizes_0_3=true nested_equality=true scratch_oom_unchanged=true copied_graph=true exact_retirement=true map_instructions=false");
}
static LbCodeModule *load(LbCodeSpace *space,const char *path)
{
    FILE *file=fopen(path,"rb"); long size; unsigned char *data; LbCodeModule *module; LbBeamError error;
    CHECK(file && !fseek(file,0,SEEK_END)); size=ftell(file); CHECK(size>0 && !fseek(file,0,SEEK_SET));
    data=malloc((size_t)size); CHECK(data && fread(data,1,(size_t)size,file)==(size_t)size && !fclose(file));
    CHECK(lb_code_load(space,data,(size_t)size,&module,&error)==LB_CODE_OK); memset(data,0xa5,(size_t)size); free(data); return module;
}
static LbCodeEntry *entry(LbCodeSpace *space,LbCodeModule *module,const char *name,unsigned arity)
{
    Eterm function; LbCodeEntry *result;
    CHECK(lb_atoms_find(space->atoms,name,strlen(name),&function)==LB_ATOM_OK);
    CHECK(lb_code_entry_acquire(space,lb_code_module_name(module),function,arity,&result)==LB_CODE_OK); return result;
}
static LbProcess *create(LbCodeEntry *entry,unsigned arity,size_t words)
{
    Eterm args[2]={NIL,NIL}; LbProcess *p;
    CHECK(lb_process_create(entry,args,arity,words,&p)==LB_PROCESS_READY); return p;
}
static void run(LbProcess *p)
{
    LbProcessStatus status=LB_PROCESS_YIELDED; size_t steps=0;
    while(status==LB_PROCESS_YIELDED && steps++<1000) {
        status=lb_process_run(p,1,100); CHECK(lb_process_collect(p,0)==status);
    }
    CHECK(status==LB_PROCESS_DONE);
}
static Eterm cause(Eterm term)
{
    Eterm *m=boxed_val(term),*keys=tuple_val(m[2]);
    CHECK(m[0]==LB_FLATMAP_HEADER && m[1]==3 && arityval(keys[0])==3);
    CHECK(keys[1]==am_cause && keys[2]==am_function && keys[3]==am_module);
    CHECK(m[4]==am_format_bs_fail && m[5]==am_erl_erts_errors);
    CHECK(is_tuple(m[3]) && arityval(*tuple_val(m[3]))==4); return m[3];
}
static void payloads(Memory *m,LbCodeEntry *identity)
{
    const Eterm operations[]={am_binary,am_binary,am_binary,am_binary,am_binary,
        am_float,am_float,am_integer,am_integer,am_string,am_utf8,am_utf16,am_utf32,am_none};
    unsigned kind; size_t live=m->live,bytes=m->bytes;
    for(kind=0;kind<sizeof(operations)/sizeof(operations[0]);++kind) {
        LbProcess *p=create(identity,1,5); Eterm result=make_small(99),*heap=p->heap,*top=p->htop;
        size_t before=m->live,held=m->bytes;
        m->fail=m->calls+1;
        CHECK(lb_binary_error_payload(p,(LbBinarySegmentKind)kind,MAX_SMALL,am_type,make_small(42),&result)==LB_ALLOC_NO_MEMORY);
        CHECK(result==make_small(99) && p->heap==heap && p->htop==top && p->construction_root==NIL);
        CHECK(p->fvalue==NIL && p->freason==LB_EXC_NULL && m->live==before && m->bytes==held); m->fail=0;
        CHECK(lb_binary_error_payload(p,(LbBinarySegmentKind)kind,MAX_SMALL,am_type,make_small(42),&p->reg[0])==LB_ALLOC_OK);
        run(p); result=cause(p->reg[0]);
        CHECK(tuple_val(result)[1]==make_small(MAX_SMALL) && tuple_val(result)[2]==operations[kind] &&
              tuple_val(result)[3]==am_type && tuple_val(result)[4]==make_small(42));
        lb_process_destroy(p); CHECK(m->live==live && m->bytes==bytes);
    }
    {
        LbProcess *p=create(identity,1,5); Eterm result=make_small(99); size_t calls=m->calls;
        CHECK(lb_binary_error_payload(p,LB_BSC_INTEGER,(Uint)MAX_SMALL+1,am_size,NIL,&result)==LB_ALLOC_INVALID);
        CHECK(lb_binary_error_payload(p,LB_BSC_INTEGER,1,NIL,NIL,&result)==LB_ALLOC_INVALID);
        CHECK(m->calls==calls && result==make_small(99)); lb_process_destroy(p);
    }
}
static void retained_values(Memory *m,LbCodeEntry *identity,LbCodeEntry *same)
{
    unsigned char bytes[65]; LbProcess *p=create(identity,1,5),*peer;
    Eterm source,copy,storage[64],*top=storage; LbOffHeap shared={0}; LbBitstringView view;
    size_t words; int equal; LbBitWriter writer;
    memset(bytes,0xa5,sizeof(bytes));
    CHECK(lb_heap_reserve(p,LB_REFC_BITS_WORDS,MAX_REG));
    CHECK(lb_bitstring_build(p->entry_module->space->domain,&p->off_heap,&p->htop,bytes,520,0,&source)==LB_ALLOC_OK);
    /* Only the constructor's explicit root protects this non-register value.
     * Fill with valid dead objects so the constructor must collect. */
    p->heap_reserved=5;
    while((size_t)(p->stop-p->htop)>=2+S_RESERVED+p->heap_reserved) {
        (void)CONS(p->htop,NIL,NIL); p->htop+=2;
    }
    {
        size_t live=m->live,held=m->bytes; Eterm *heap=p->heap,result=make_small(99);
        m->fail=m->calls+1;
        CHECK(lb_binary_error_payload(p,LB_BSC_BINARY,2,am_type,source,&result)==LB_ALLOC_NO_MEMORY);
        CHECK(result==make_small(99) && p->heap==heap && m->live==live && m->bytes==held && p->heap_reserved==5);
        m->fail=0;
    }
    CHECK(lb_binary_error_payload(p,LB_BSC_BINARY,2,am_type,source,&p->reg[0])==LB_ALLOC_OK);
    CHECK(p->collections>=2 && p->heap_reserved==5 && (size_t)(p->stop-p->htop)>=5+S_RESERVED);
    CHECK(lb_flat_size(p->entry_module->space->domain,p->reg[0],&words) && words==23);
    copy=lb_copy_flat(p->reg[0],&top,&shared); CHECK(top-storage==(ptrdiff_t)words && !is_non_value(copy));
    CHECK(lb_term_equal(p->entry_module->space->domain,p->reg[0],copy,&equal)==LB_ALLOC_OK && equal);
    *--p->stop=p->reg[0]; p->reg[0]=NIL;
    CHECK(lb_process_collect(p,0)==LB_PROCESS_READY); p->fvalue=*p->stop++;
    CHECK(lb_process_collect(p,0)==LB_PROCESS_READY); p->reg[0]=p->fvalue; run(p);
    lb_process_destroy(p);
    source=tuple_val(cause(copy))[4]; CHECK(lb_bitstring_view(source,&view) && view.bit_size==520 && view.data[0]==0xa5);
    /* Copy into a second real process and consume the map through actual eq/ne. */
    peer=create(same,2,100); top=peer->htop;
    peer->reg[0]=lb_copy_flat(copy,&top,&peer->off_heap); peer->reg[1]=lb_copy_flat(copy,&top,&peer->off_heap); peer->htop=top;
    CHECK(!is_non_value(peer->reg[0]) && !is_non_value(peer->reg[1])); run(peer);
    CHECK(lb_bitstring_view(peer->reg[0],&view) && view.bit_size==40 && !memcmp(view.data,"equal",5)); lb_process_destroy(peer);
    peer=create(same,2,100); top=peer->htop;
    peer->reg[0]=lb_copy_flat(copy,&top,&peer->off_heap); peer->reg[1]=lb_copy_flat(copy,&top,&peer->off_heap); peer->htop=top;
    tuple_val(cause(peer->reg[1]))[1]=make_small(3); run(peer);
    CHECK(lb_bitstring_view(peer->reg[0],&view) && view.bit_size==72 && !memcmp(view.data,"different",9)); lb_process_destroy(peer);
    lb_offheap_clear(&shared);
    /* A map value is a genuine alias. Private append must not mutate it. */
    p=create(identity,1,100); CHECK(lb_writable_init(p,64,&p->reg[0])==LB_WRITABLE_OK);
    p->reg[1]=map(p->htop,p->reg[0]); p->htop+=LB_MAP3_WORDS;
    CHECK(lb_writable_private_append(p,(LbBinarySource){LB_BINARY_SOURCE_X,0},1,&writer,&source)==LB_WRITABLE_NOT_UNIQUE);
    p->reg[1]=NIL; CHECK(lb_process_collect(p,0)==LB_PROCESS_READY);
    CHECK(lb_writable_private_append(p,(LbBinarySource){LB_BINARY_SOURCE_X,0},1,&writer,&p->reg[0])==LB_WRITABLE_OK);
    CHECK(lb_bits_put_integer(&writer,make_small(1),1,LB_BITS_BIG_ENDIAN)==LB_BITS_OK); run(p); lb_process_destroy(p);
}
static void executable(const char *path)
{
    Memory m={0}; LbSystemAllocator cb={&m,allocate,release}; LbEngine *engine; LbCodeSpace *space;
    LbCodeModule *module; LbCodeEntry *identity,*same; size_t live,bytes;
    CHECK(lb_engine_create(&cb,&engine)==LB_ENGINE_OK && lb_code_space_create(engine,&space)==LB_CODE_OK);
    module=load(space,path); identity=entry(space,module,"identity",1); same=entry(space,module,"same",2);
    live=m.live; bytes=m.bytes; payloads(&m,identity); retained_values(&m,identity,same);
    CHECK(m.live==live && m.bytes==bytes);
    lb_code_entry_release(same); lb_code_entry_release(identity);
    CHECK(lb_code_unload(module)==LB_CODE_OK && lb_code_space_destroy(space)==LB_CODE_OK);
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine); CHECK(!m.live && !m.bytes);
    puts("CORE_BINARY_ERROR_PAYLOAD_OK native_map3=true operations=14 cause_tuple=true moving_gc=true copied_binary_lifetime=true exact_comparison_execution=true alias_guard=true allocation_failure_retry=true physical_retirement=true guest_construction=false exception_flags=false");
}
static void print_atom(LbCodeSpace *space,Eterm atom)
{
    LbBeamBytes name; CHECK(lb_atoms_name(space->atoms,atom,&name)==LB_ATOM_OK);
    CHECK(fwrite(name.data,1,name.size,stdout)==name.size);
}
static void reference(const char *path)
{
    const struct { LbBinarySegmentKind kind; Eterm info; Sint value; } cases[]={
        {LB_BSC_UTF32,am_type,0x110000}, {LB_BSC_UTF32,am_type,-1},
        {LB_BSC_INTEGER,am_size,-1}, {LB_BSC_FLOAT,am_invalid,7}, {LB_BSC_BINARY,am_type,42}};
    Memory m={0}; LbSystemAllocator cb={&m,allocate,release}; LbEngine *engine; LbCodeSpace *space;
    LbCodeModule *module; LbCodeEntry *identity; size_t i;
    CHECK(lb_engine_create(&cb,&engine)==LB_ENGINE_OK && lb_code_space_create(engine,&space)==LB_CODE_OK);
    module=load(space,path); identity=entry(space,module,"identity",1);
    for(i=0;i<sizeof(cases)/sizeof(cases[0]);++i) {
        LbProcess *p=create(identity,1,5); Eterm *c,*map_words,*keys; unsigned j;
        CHECK(lb_binary_error_payload(p,cases[i].kind,1,cases[i].info,make_small(cases[i].value),&p->reg[0])==LB_ALLOC_OK);
        run(p); c=tuple_val(cause(p->reg[0])); map_words=boxed_val(p->reg[0]); keys=tuple_val(map_words[2]);
        printf("[{"); print_atom(space,keys[1]); printf(",{%" PRIdPTR ",",signed_val(c[1]));
        print_atom(space,c[2]); printf(","); print_atom(space,c[3]); printf(",%" PRIdPTR "}}",signed_val(c[4]));
        for(j=2;j<=3;++j) { printf(",{"); print_atom(space,keys[j]); printf(","); print_atom(space,map_words[j+2]); printf("}"); }
        puts("]"); lb_process_destroy(p);
    }
    lb_code_entry_release(identity); CHECK(lb_code_unload(module)==LB_CODE_OK && lb_code_space_destroy(space)==LB_CODE_OK);
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine); CHECK(!m.live && !m.bytes);
}
int main(int argc,char **argv)
{
    if(argc==3 && !strcmp(argv[1],"--reference")) reference(argv[2]);
    else { components(); if(argc==2) executable(argv[1]); else CHECK(argc==1); }
    return 0;
}
