/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#include "process_internal.h"
#include <stdio.h>
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
static LbAllocDomain *domain(Memory *memory)
{
    LbSystemAllocator callbacks={memory,allocate,release}; LbAllocDomain *d;
    CHECK(lb_alloc_domain_create(&callbacks,&d)==LB_ALLOC_OK); return d;
}
static LbBinRef *binary_ref(Eterm term)
{
    LbSubBits *sub=(LbSubBits *)boxed_val(term); LbBinRef *ref;
    CHECK(sub->thing_word==LB_HEADER_SUB_BITS);
    ref=(LbBinRef *)boxed_val(sub->orig); CHECK(ref->thing_word==LB_HEADER_BIN_REF); return ref;
}
static Uint refcount(Eterm term)
{
    return atomic_load_explicit(&binary_ref(term)->val->intern.refc,memory_order_relaxed);
}
static void component_test(void)
{
    Memory m={0}; LbAllocDomain *d=domain(&m);
    Eterm storage[32],copied[64],tuple[3],*top=storage,*copy=copied,first,second,result=THE_NON_VALUE;
    LbOffHeap source={0},destination={0}; LbBitstringView view;
    unsigned char data[65]; size_t live=m.live,bytes=m.bytes,words;
    memset(data,0xff,sizeof(data)); memset(storage,0x5a,sizeof(storage));
    CHECK(lb_bitstring_heap_words(0)==2 && lb_bitstring_heap_words(512)==10 && lb_bitstring_heap_words(513)==8);
    CHECK(lb_bitstring_heap_words(SIZE_MAX)==0 && lb_binary_allocation_size(SIZE_MAX)==0);
    CHECK(lb_bitstring_build(d,&source,&top,data,SIZE_MAX,0,&result)==LB_ALLOC_INVALID && top==storage && !source.first);
    m.fail=m.calls+1;
    CHECK(lb_bitstring_build(d,&source,&top,data,513,TAG_LITERAL_PTR,&result)==LB_ALLOC_NO_MEMORY);
    CHECK(top==storage && !source.first && is_non_value(result) && storage[0]==UINT64_C(0x5a5a5a5a5a5a5a5a));
    CHECK(m.live==live && m.bytes==bytes); m.fail=0;
    CHECK(lb_bitstring_build(d,&source,&top,data,513,TAG_LITERAL_PTR,&first)==LB_ALLOC_OK);
    CHECK(refcount(first)==1 && is_literal_ptr(first) && is_literal_ptr(((LbSubBits *)boxed_val(first))->orig));
    CHECK(lb_bitstring_view(first,&view) && !view.bit_offset && view.bit_size==513 && view.data[64]==0x80);
    memset(data,0,sizeof(data)); CHECK(view.data[0]==0xff);
    CHECK(lb_bitstring_build(d,&source,&top,data,520,TAG_LITERAL_PTR,&second)==LB_ALLOC_OK);
    result=TUPLE2(tuple,first,second);
    CHECK(lb_flat_size(d,result,&words) && words==3+2*LB_REFC_BITS_WORDS);
    /* Reach the overflow branch without enormous allocations. The production
     * primitive must refuse and undo an earlier reference from this same copy. */
    atomic_store(&binary_ref(second)->val->intern.refc,UINTPTR_MAX);
    CHECK(is_non_value(lb_copy_flat(result,&copy,&destination)) && copy==copied && !destination.first && !destination.overhead);
    CHECK(refcount(first)==1 && refcount(second)==UINTPTR_MAX);
    atomic_store(&binary_ref(second)->val->intern.refc,1);
    destination.overhead=UINT64_MAX;
    CHECK(is_non_value(lb_copy_flat(result,&copy,&destination)) && copy==copied && refcount(first)==1);
    destination.overhead=0;
    result=lb_copy_flat(result,&copy,&destination);
    CHECK(!is_non_value(result) && (size_t)(copy-copied)==words && refcount(first)==2 && refcount(second)==2);
    lb_offheap_clear(&source);
    first=tuple_val(result)[1]; second=tuple_val(result)[2];
    CHECK(refcount(first)==1 && refcount(second)==1 && lb_alloc_domain_destroy(d)==LB_ALLOC_BUSY);
    CHECK(lb_bitstring_view(first,&view) && view.data[0]==0xff && view.data[64]==0x80);
    lb_offheap_clear(&destination);
    CHECK(m.live==live && m.bytes==bytes);
    CHECK(!lb_bitstring_view(make_small(1),&view) && !view.data && !view.bit_size);
    CHECK(lb_alloc_domain_destroy(d)==LB_ALLOC_OK && !m.live && !m.bytes);
    puts("CORE_BINARY_COMPONENT_OK layouts=true refcount_overflow_rollback=true physical_release=true execution=false");
}
static unsigned char *read_image(const char *path,size_t *size)
{
    FILE *file=fopen(path,"rb"); long length; unsigned char *data;
    CHECK(file && !fseek(file,0,SEEK_END)); length=ftell(file);
    CHECK(length>0 && !fseek(file,0,SEEK_SET)); *size=(size_t)length;
    data=malloc(*size); CHECK(data && fread(data,1,*size,file)==*size && !fclose(file)); return data;
}
static LbCodeModule *load(LbCodeSpace *space,const char *path)
{
    size_t size; unsigned char *data=read_image(path,&size); LbCodeModule *module; LbBeamError error;
    LbCodeStatus status=lb_code_load(space,data,size,&module,&error);
    if(status!=LB_CODE_OK) fprintf(stderr,"%s status=%d stage=%s offset=%zu\n",path,status,error.stage,error.offset);
    CHECK(status==LB_CODE_OK); memset(data,0xa5,size); free(data); return module;
}
static Eterm atom(LbCodeSpace *space,const char *name)
{
    Eterm value; CHECK(lb_atoms_find(lb_code_space_atoms(space),name,strlen(name),&value)==LB_ATOM_OK); return value;
}
static LbCodeEntry *entry(LbCodeSpace *space,LbCodeModule *module,const char *name,unsigned arity)
{
    LbCodeEntry *result;
    CHECK(lb_code_entry_acquire(space,lb_code_module_name(module),atom(space,name),arity,&result)==LB_CODE_OK); return result;
}
static LbProcess *invoke(LbCodeSpace *space,LbCodeModule *module,const char *name,Eterm *arguments,size_t arity)
{
    LbCodeEntry *e=entry(space,module,name,(unsigned)arity); LbProcess *p;
    CHECK(lb_process_create(e,arguments,arity,5,&p)==LB_PROCESS_READY); lb_code_entry_release(e); return p;
}
static void run(LbProcess *p,int collect)
{
    LbProcessStatus status=LB_PROCESS_YIELDED; size_t steps=0;
    while(status==LB_PROCESS_YIELDED && steps++<1000) {
        status=lb_process_run(p,1,100);
        if(collect) CHECK(lb_process_collect(p,0)==status);
    }
    CHECK(status==LB_PROCESS_DONE);
}
static void zero_binary(Eterm term,size_t bits,unsigned char last)
{
    LbBitstringView view; size_t i,bytes=(bits+7)/8;
    CHECK(lb_bitstring_view(term,&view) && view.bit_size==bits && !view.bit_offset);
    for(i=0;i+1<bytes;++i) CHECK(!view.data[i]);
    CHECK(view.data[bytes-1]==last);
}
static Eterm payload(LbCodeSpace *space,Eterm attributes)
{
    Eterm key=atom(space,"payload");
    for(;is_list(attributes);attributes=CDR(list_val(attributes))) {
        Eterm pair=CAR(list_val(attributes));
        CHECK(is_tuple(pair) && arityval(*tuple_val(pair))==2);
        if(tuple_val(pair)[1]==key) {
            Eterm values=tuple_val(pair)[2]; CHECK(is_list(values) && is_nil(CDR(list_val(values))));
            return CAR(list_val(values));
        }
    }
    CHECK(0); return THE_NON_VALUE;
}
static void copied_inputs(LbAllocDomain *d,Memory *m,LbCodeSpace *space,LbCodeModule *module)
{
    const size_t sizes[]={0,1,64,65,4096,65536};
    unsigned char data[65536]; size_t i,j;
    LbCodeEntry *identity=entry(space,module,"identity",1),*wrap=entry(space,module,"wrap",1);
    for(i=0;i<sizeof(sizes)/sizeof(sizes[0]);++i) {
        LbProcess *p; LbBitstringView view; Eterm result; size_t size=sizes[i],live,bytes;
        for(j=0;j<size;++j) data[j]=(unsigned char)j;
        CHECK(lb_process_create_binary(identity,data,size,5,&p)==LB_PROCESS_READY);
        memset(data,0xa5,size); run(p,1); result=lb_process_result(p);
        CHECK(lb_bitstring_view(result,&view) && view.bit_size==size*8 && !is_literal_ptr(result));
        for(j=0;j<size;++j) CHECK(view.data[j]==(unsigned char)j);
        if(size>64) CHECK(refcount(result)==1 && p->off_heap.first==binary_ref(result));
        else CHECK(!p->off_heap.first);
        live=m->live; bytes=m->bytes; m->fail=m->calls+1;
        CHECK(lb_process_collect(p,100)==LB_PROCESS_NO_MEMORY && lb_process_result(p)==result);
        CHECK(m->live==live && m->bytes==bytes); m->fail=0;
        CHECK(lb_process_collect(p,100)==LB_PROCESS_DONE); lb_process_destroy(p);
        CHECK(lb_process_create_binary(wrap,data,size,5,&p)==LB_PROCESS_READY); run(p,1);
        result=lb_process_result(p); CHECK(is_tuple(result) && arityval(*tuple_val(result))==2);
        CHECK(tuple_val(result)[1]==tuple_val(result)[2]);
        if(size>64) CHECK(refcount(tuple_val(result)[1])==1 && !p->off_heap.first->next);
        lb_process_destroy(p);
    }
    { LbProcess *p; size_t calls,live=m->live,bytes=m->bytes;
      calls=m->calls; CHECK(lb_process_create_binary(identity,data,4096,5,&p)==LB_PROCESS_READY);
      calls=m->calls-calls; lb_process_destroy(p);
      for(i=1;i<=calls;++i) {
          m->fail=m->calls+i;
          CHECK(lb_process_create_binary(identity,data,4096,5,&p)==LB_PROCESS_NO_MEMORY && !p);
          CHECK(m->live==live && m->bytes==bytes); m->fail=0;
          CHECK(lb_process_create_binary(identity,data,4096,5,&p)==LB_PROCESS_READY); run(p,1); lb_process_destroy(p);
      }
      CHECK(lb_process_create_binary(identity,NULL,1,5,&p)==LB_PROCESS_INVALID && !p);
      CHECK(lb_process_create_binary(identity,data,SIZE_MAX,5,&p)==LB_PROCESS_INVALID && !p);
      CHECK(m->live==live && m->bytes==bytes);
    }
    { LbCodeEntry *drop=entry(space,module,"drop",1); LbProcess *p; size_t live;
      CHECK(lb_process_create_binary(drop,data,4096,5,&p)==LB_PROCESS_READY); lb_code_entry_release(drop);
      run(p,0); CHECK(p->off_heap.first && lb_process_result(p)==atom(space,"discarded")); live=m->live;
      CHECK(lb_process_collect(p,0)==LB_PROCESS_DONE && !p->off_heap.first && !p->off_heap.overhead);
      CHECK(m->live+1==live); lb_process_destroy(p);
    }
    CHECK(lb_alloc_domain_destroy(d)==LB_ALLOC_BUSY);
    lb_code_entry_release(wrap); lb_code_entry_release(identity);
}
static void metadata_copy_failure(Memory *memory,LbCodeSpace *space,LbCodeModule *module)
{
    Eterm literal=payload(space,module->attributes);
    LbBinary *binary=binary_ref(literal)->val;
    unsigned char input[65]={0}; unsigned variant;
    for(variant=0;variant<2;++variant) {
        LbCodeEntry *e=entry(space,module,variant?"all_info":"attributes",1);
        LbProcess *p; LbBinRef *old_refs; Eterm *old_top;
        LbProcessStatus status=LB_PROCESS_YIELDED; size_t steps=0,live=memory->live,bytes=memory->bytes;
        CHECK(lb_process_create_binary(e,input,sizeof(input),1024,&p)==LB_PROCESS_READY);
        old_refs=p->off_heap.first; old_top=p->htop; CHECK(old_refs && refcount(literal)==1);
        atomic_store(&binary->intern.refc,UINTPTR_MAX);
        while(status==LB_PROCESS_YIELDED && steps++<1000) status=lb_process_run(p,1,100);
        CHECK(status==LB_PROCESS_NO_MEMORY && p->htop==old_top && p->off_heap.first==old_refs);
        CHECK(!old_refs->next && atomic_load(&old_refs->val->intern.refc)==1 && refcount(literal)==UINTPTR_MAX);
        atomic_store(&binary->intern.refc,1);
        lb_process_destroy(p); CHECK(memory->live==live && memory->bytes==bytes);
        /* Same module and implementation still work after the refused copy. */
        CHECK(lb_process_create_binary(e,input,sizeof(input),5,&p)==LB_PROCESS_READY);
        run(p,1); lb_process_destroy(p); CHECK(refcount(literal)==1);
        lb_code_entry_release(e);
    }
}
static void executable_test(const char *path)
{
    Memory m={0}; LbSystemAllocator cb={&m,allocate,release}; LbEngine *engine=NULL;
    LbCodeSpace *space; LbCodeModule *module;
    LbBeamError error; size_t size,calls,i,live,bytes,atoms; unsigned char *data=read_image(path,&size);
    CHECK(lb_engine_create(&cb,&engine)==LB_ENGINE_OK && lb_code_space_create(engine,&space)==LB_CODE_OK); calls=m.calls;
    { LbCodeStatus status=lb_code_load(space,data,size,&module,&error);
      if(status!=LB_CODE_OK) fprintf(stderr,"%s status=%d stage=%s offset=%zu\n",path,status,error.stage,error.offset);
      CHECK(status==LB_CODE_OK); }
    calls=m.calls-calls;
    for(i=0;i<2;++i) {
        LbProcess *p=invoke(space,module,i?"bits":"literal",NULL,0);
        LbBitstringView view; Eterm result; size_t j;
        run(p,1); result=lb_process_result(p);
        CHECK(is_tuple(result) && is_literal_ptr(result) && arityval(*tuple_val(result))==1);
        result=tuple_val(result)[1]; CHECK(is_literal_ptr(result) && refcount(result)==1 && !p->off_heap.first);
        CHECK(lb_bitstring_view(result,&view) && view.bit_size==(i?643u:640u) && !view.bit_offset);
        for(j=0;j<80;++j) CHECK(view.data[j]=='0'+j%10);
        if(i) CHECK(view.data[80]==0xa0);
        CHECK(lb_code_unload(module)==LB_CODE_BUSY); lb_process_destroy(p);
    }
    copied_inputs(engine->domain,&m,space,module);
    metadata_copy_failure(&m,space,module);
    { Eterm what=atom(space,"attributes"),value; LbProcess *p=invoke(space,module,"module_info",&what,1);
      run(p,1); value=payload(space,lb_process_result(p)); zero_binary(value,2056,0x7f);
      CHECK(!is_literal_ptr(value) && refcount(value)==2); lb_process_destroy(p);
    }
    CHECK(lb_code_unload(module)==LB_CODE_OK && lb_code_space_destroy(space)==LB_CODE_OK);
    for(i=1;i<=calls;++i) {
        CHECK(lb_code_space_create(engine,&space)==LB_CODE_OK);
        live=m.live; bytes=m.bytes; atoms=lb_atoms_count(lb_code_space_atoms(space)); m.fail=m.calls+i;
        CHECK(lb_code_load(space,data,size,&module,&error)==LB_CODE_NO_MEMORY && !module);
        CHECK(m.live==live && m.bytes==bytes && lb_atoms_count(lb_code_space_atoms(space))==atoms); m.fail=0;
        CHECK(lb_code_load(space,data,size,&module,&error)==LB_CODE_OK);
        CHECK(lb_code_unload(module)==LB_CODE_OK && lb_code_space_destroy(space)==LB_CODE_OK);
    }
    free(data); CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine);
    CHECK(!m.live && !m.bytes);
    printf("CORE_BINARY_EXECUTION_OK literal=true copied_input=true shared_tuple_root=true gc_dead_release=true failure_prefixes=%zu engine_lifecycle=false\n",calls);
}
static void source_retirement(const char *binary_path,const char *first_path,const char *peer_path)
{
    Memory m={0}; LbSystemAllocator cb={&m,allocate,release}; LbEngine *engine=NULL;
    LbCodeSpace *space; LbCodeModule *first,*peer,*source;
    LbProcess *p; Eterm module,value; const unsigned char *original; LbBitstringView view;
    CHECK(lb_engine_create(&cb,&engine)==LB_ENGINE_OK && lb_code_space_create(engine,&space)==LB_CODE_OK);
    first=load(space,first_path); peer=load(space,peer_path); source=load(space,binary_path);
    module=lb_code_module_name(source); p=invoke(space,peer,"metadata",&module,1); run(p,1);
    value=payload(space,lb_process_result(p)); CHECK(refcount(value)==2);
    CHECK(lb_bitstring_view(value,&view)); original=view.data;
    CHECK(lb_code_unload(source)==LB_CODE_OK && refcount(value)==1);
    CHECK(lb_process_collect(p,0)==LB_PROCESS_DONE); zero_binary(payload(space,lb_process_result(p)),2056,0x7f);
    /* A replacement has distinct payload storage; it cannot resurrect the
     * retired module or steal the surviving process's reference. */
    source=load(space,binary_path);
    CHECK(refcount(payload(space,lb_process_result(p)))==1);
    CHECK(lb_bitstring_view(payload(space,lb_process_result(p)),&view) && view.data==original);
    {
        LbProcess *replacement=invoke(space,peer,"metadata",&module,1);
        run(replacement,1); value=payload(space,lb_process_result(replacement));
        CHECK(refcount(value)==2 && lb_bitstring_view(value,&view) && view.data!=original);
        lb_process_destroy(p);
        CHECK(lb_process_collect(replacement,0)==LB_PROCESS_DONE);
        zero_binary(payload(space,lb_process_result(replacement)),2056,0x7f);
        lb_process_destroy(replacement);
    }
    CHECK(lb_code_unload(source)==LB_CODE_OK && lb_code_unload(peer)==LB_CODE_OK && lb_code_unload(first)==LB_CODE_OK);
    CHECK(lb_code_space_destroy(space)==LB_CODE_OK && lb_engine_shutdown(engine)==LB_ENGINE_OK);
    lb_engine_release(engine); CHECK(!m.live && !m.bytes);
    puts("CORE_BINARY_RETIREMENT_OK copied_binary_survives_source_module=true replacement=true resource_balance=true");
}
int main(int argc,char **argv)
{
    component_test();
    if(argc==2 || argc==4) executable_test(argv[1]); else CHECK(argc==1);
    if(argc==4) source_retirement(argv[1],argv[2],argv[3]);
    return 0;
}
