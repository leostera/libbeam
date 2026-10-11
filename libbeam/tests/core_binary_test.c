/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#include "process_internal.h"
#include "bit_copy.h"
#include "bit_writer.h"
#include "writable.h"
#include "binary_error.h"
#include "term_compare.h"
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
static void writable_failures(Memory *m,LbCodeEntry *identity)
{
    size_t mode,fail;
    for(mode=0;mode<5;++mode) for(fail=1;fail<=(mode>=3?1u:2u);++fail) {
        size_t build=mode==3?2:mode==4?0:4096;
        LbProcess *p; Eterm source=NIL,result=make_small(99),before;
        LbBitWriter writer={0},saved; LbBinary *backing=NULL;
        size_t live,bytes; Eterm *heap,*top; Uint flags=0; LbSubBits sub={0};
        CHECK(lb_process_create(identity,&source,1,5,&p)==LB_PROCESS_READY);
        if(mode==1) {
            unsigned char input[65]; memset(input,0xa5,sizeof(input));
            CHECK(lb_heap_reserve(p,LB_REFC_BITS_WORDS,MAX_REG));
            CHECK(lb_bitstring_build(p->entry_module->space->domain,&p->off_heap,&p->htop,
                                     input,sizeof(input)*8,0,&source)==LB_ALLOC_OK);
        } else if(mode>=2) {
            CHECK(lb_writable_init(p,1,&source)==LB_WRITABLE_OK); p->reg[0]=source;
            CHECK(lb_writable_append(p,source,3,0,1,&writer,&source)==LB_WRITABLE_OK);
            CHECK(lb_bits_put_integer(&writer,make_small(5),3,LB_BITS_BIG_ENDIAN)==LB_BITS_OK);
            p->reg[0]=source; backing=binary_ref(source)->val; flags=backing->intern.flags;
            sub=*(LbSubBits *)boxed_val(source);
        }
        /* Append's source deliberately has no register root. Its temporary
         * construction root must survive the forced collection on success. */
        p->reg[0]=NIL; p->heap_reserved=9;
        live=m->live; bytes=m->bytes; heap=p->heap; top=p->htop;
        before=result; saved=writer; m->fail=m->calls+fail;
        if(!mode) CHECK(lb_writable_init(p,1,&result)==LB_WRITABLE_NO_MEMORY);
        else CHECK(lb_writable_append(p,source,build,4096,1,&writer,&result)==LB_WRITABLE_NO_MEMORY);
        CHECK(result==before && !memcmp(&writer,&saved,sizeof(writer)));
        CHECK(p->heap==heap && p->htop==top && p->construction_root==NIL && p->heap_reserved==9);
        CHECK(m->live==live && m->bytes==bytes);
        if(backing) CHECK(backing->intern.flags==flags && backing->orig_bytes[0]==0xa0 &&
                          !memcmp(&sub,boxed_val(source),sizeof(sub)) && refcount(source)==1);
        m->fail=0;
        if(!mode) CHECK(lb_writable_init(p,1,&result)==LB_WRITABLE_OK);
        else {
            CHECK(lb_writable_append(p,source,build,4096,1,&writer,&result)==LB_WRITABLE_OK);
            if(build) CHECK(lb_bits_put_integer(&writer,make_small(-1),build,LB_BITS_BIG_ENDIAN)==LB_BITS_OK);
            else CHECK(!writer.data && !writer.capacity);
        }
        p->reg[0]=result;
        CHECK(p->construction_root==NIL && p->heap_reserved==9 &&
              (size_t)(p->stop-p->htop)>=p->heap_reserved+S_RESERVED);
        run(p,1); lb_process_destroy(p);
    }
}
static unsigned bit_at(const unsigned char *data,size_t bit)
{
    return (data[bit/8]>>(7-bit%8))&1;
}
static void writable_windows(LbCodeEntry *identity)
{
    unsigned char input[66]; size_t offset,length,j;
    for(j=0;j<sizeof(input);++j) input[j]=(unsigned char)(37*j+11);
    for(offset=0;offset<16;++offset) for(length=0;length<=257;++length) {
        LbProcess *p; LbSubBits *sub; Eterm result; LbBitWriter writer; LbBitstringView view;
        CHECK(lb_process_create_binary(identity,input,sizeof(input),5,&p)==LB_PROCESS_READY);
        sub=(LbSubBits *)boxed_val(p->reg[0]); sub->start=offset; sub->end=offset+length;
        CHECK(lb_writable_append(p,p->reg[0],5,80,1,&writer,&result)==LB_WRITABLE_OK);
        CHECK(lb_bits_put_integer(&writer,make_small(19),5,LB_BITS_BIG_ENDIAN)==LB_BITS_OK);
        p->reg[0]=result; run(p,1);
        CHECK(lb_bitstring_view(lb_process_result(p),&view) && view.bit_size==length+5);
        for(j=0;j<length;++j) CHECK(bit_at(view.data,j)==bit_at(input,offset+j));
        for(j=0;j<5;++j) CHECK(bit_at(view.data,length+j)==((19u>>(4-j))&1));
        CHECK(p->writable.first==binary_ref(p->reg[0]) && !p->off_heap.first);
        lb_process_destroy(p);
    }
}
static void writable_lifetimes(Memory *m,LbCodeEntry *identity)
{
    LbProcess *p; Eterm initial=NIL,result,copy,storage[LB_REFC_BITS_WORDS],*top=storage;
    LbBitWriter writer; LbBitstringView view; LbOffHeap shared={0}; LbBinary *old;
    size_t live=m->live,bytes=m->bytes; uintptr_t old_address; int equal;
    CHECK(lb_process_create(identity,&initial,1,5,&p)==LB_PROCESS_READY);
    CHECK(lb_writable_init(p,1,&initial)==LB_WRITABLE_OK); p->reg[0]=initial;
    CHECK(lb_writable_append(p,p->reg[0],3,0,1,&writer,&result)==LB_WRITABLE_OK);
    CHECK(lb_bits_put_integer(&writer,make_small(5),3,LB_BITS_BIG_ENDIAN)==LB_BITS_OK);
    p->reg[1]=p->reg[0]; p->reg[0]=result; old=binary_ref(result)->val; old_address=(uintptr_t)old;
    CHECK(!lb_binary_retain(old) && refcount(result)==1);
    CHECK(lb_writable_append(p,p->reg[0],4096,4096,1,&writer,&result)==LB_WRITABLE_OK);
    CHECK(lb_bits_put_integer(&writer,make_small(-1),4096,LB_BITS_BIG_ENDIAN)==LB_BITS_OK);
    p->reg[2]=p->reg[0]; p->reg[0]=result;
    CHECK((uintptr_t)binary_ref(result)->val!=old_address && binary_ref(result)==binary_ref(p->reg[2]));
    CHECK(lb_bitstring_view(p->reg[1],&view) && !view.bit_size);
    CHECK(lb_bitstring_view(p->reg[2],&view) && view.bit_size==3 && (view.data[0]&0xe0)==0xa0);
    CHECK((((LbSubBits *)boxed_val(p->reg[2]))->base_flags&3)==LB_SUB_VOLATILE);
    CHECK(lb_process_collect(p,0)==LB_PROCESS_READY);
    CHECK(lb_bitstring_view(p->reg[2],&view) && view.bit_size==3 && (view.data[0]&0xe0)==0xa0);
    /* Branching from the revoked prefix must copy, never alter the other tail. */
    CHECK(lb_writable_append(p,p->reg[2],8,0,1,&writer,&result)==LB_WRITABLE_OK);
    CHECK(lb_bits_put_integer(&writer,make_small(0x35),8,LB_BITS_BIG_ENDIAN)==LB_BITS_OK);
    p->reg[3]=result;
    CHECK(binary_ref(result)->val!=binary_ref(p->reg[0])->val);
    CHECK(lb_bitstring_view(p->reg[0],&view) && view.bit_size==4099 && view.data[1]==0xff);
    /* Grow the branch too, then pin only its new view. Its older volatile view
     * has a stale cached pointer and must still resolve safely after pinning. */
    CHECK(lb_writable_append(p,p->reg[3],4096,4096,1,&writer,&result)==LB_WRITABLE_OK);
    CHECK(lb_bits_put_integer(&writer,make_small(0),4096,LB_BITS_BIG_ENDIAN)==LB_BITS_OK);
    p->reg[4]=result;
    CHECK(lb_bitstring_pin(p->reg[4]) && !binary_ref(p->reg[4])->val->intern.flags);
    CHECK(lb_bitstring_view(p->reg[3],&view) && view.bit_size==11 && view.data[0]==0xa6 && view.data[1]==0xa0);
    {
        Eterm frozen_storage[LB_REFC_BITS_WORDS],*frozen_top=frozen_storage,frozen_copy;
        LbOffHeap temporary={0}; LbSubBits snapshot=*(LbSubBits *)boxed_val(p->reg[3]);
        frozen_copy=lb_copy_flat(p->reg[3],&frozen_top,&temporary);
        CHECK(!is_non_value(frozen_copy) && !(((LbSubBits *)boxed_val(frozen_copy))->base_flags&3));
        CHECK(!memcmp(&snapshot,boxed_val(p->reg[3]),sizeof(snapshot)));
        CHECK(lb_term_equal(p->entry_module->space->domain,frozen_copy,p->reg[3],&equal)==LB_ALLOC_OK && equal);
        lb_offheap_clear(&temporary); CHECK(refcount(p->reg[3])==1);
    }
    CHECK(lb_bitstring_pin(p->reg[3]));
    copy=lb_copy_flat(p->reg[4],&top,&shared); CHECK(!is_non_value(copy) && refcount(copy)==2);
    CHECK(lb_term_equal(p->entry_module->space->domain,copy,p->reg[4],&equal)==LB_ALLOC_OK && equal);
    /* A frozen latest view must take the copying branch too. Its sharing
     * permission cannot be revoked by a later append. Drop the new result. */
    CHECK(lb_writable_append(p,p->reg[4],8,0,1,&writer,&result)==LB_WRITABLE_OK);
    CHECK(lb_bits_put_integer(&writer,make_small(0x42),8,LB_BITS_BIG_ENDIAN)==LB_BITS_OK);
    CHECK(binary_ref(result)->val!=binary_ref(p->reg[4])->val && refcount(copy)==2);
    CHECK(lb_process_collect(p,0)==LB_PROCESS_READY && refcount(copy)==2);
    CHECK(p->off_heap.first==binary_ref(p->reg[4]) && p->writable.first==binary_ref(p->reg[0]));
    p->reg[0]=p->reg[4]; run(p,1);
    CHECK(!p->writable.first && refcount(copy)==2);
    lb_process_destroy(p);
    CHECK(m->live==live+1 && m->bytes>bytes && refcount(copy)==1);
    CHECK(lb_bitstring_view(copy,&view) && view.bit_size==4107 && view.data[0]==0xa6);
    lb_offheap_clear(&shared); CHECK(m->live==live && m->bytes==bytes);
}
static void writable_boundaries(LbCodeEntry *identity)
{
    LbProcess *p; Eterm source=NIL,result=make_small(99); LbBitWriter writer={0};
    LbBitstringView view; unsigned unit; size_t i;
    const size_t capacities[]={0,1,(1u<<24)-1,(1u<<24)+1,LB_MAX_BINARY_BYTES};
    CHECK(lb_process_create(identity,&source,1,5,&p)==LB_PROCESS_READY);
    CHECK(lb_writable_init(p,LB_MAX_BINARY_BYTES+1u,&result)==LB_WRITABLE_LIMIT && result==make_small(99));
    CHECK(lb_writable_append(p,NIL,0,0,1,&writer,&result)==LB_WRITABLE_TYPE);
    CHECK(lb_writable_init(p,0,&source)==LB_WRITABLE_OK); p->reg[0]=source;
    CHECK(lb_writable_append(p,source,3,0,1,&writer,&result)==LB_WRITABLE_OK);
    CHECK(lb_bits_put_integer(&writer,make_small(5),3,LB_BITS_BIG_ENDIAN)==LB_BITS_OK); p->reg[0]=result;
    for(unit=0;unit<=257;++unit) {
        Eterm saved=p->reg[0]; LbBitWriter before=writer; result=make_small(99);
        LbWritableStatus status=lb_writable_append(p,saved,0,0,unit,&writer,&result);
        if(unit==1 || unit==3) CHECK(status==LB_WRITABLE_OK && result==saved && !writer.data && !writer.capacity);
        else CHECK(status==(unit==0 || unit==257?LB_WRITABLE_INVALID:LB_WRITABLE_UNIT) &&
                   result==make_small(99) && !memcmp(&writer,&before,sizeof(writer)));
    }
    result=make_small(99);
    CHECK(lb_writable_append(p,p->reg[0],SIZE_MAX,0,1,&writer,&result)==LB_WRITABLE_LIMIT && result==make_small(99));
    CHECK(lb_writable_append(p,p->reg[0],0,4096,1,&writer,&result)==LB_WRITABLE_OK && result==p->reg[0]);
    CHECK(lb_bitstring_view(result,&view) && view.bit_size==3 && view.data[0]==0xa0);
    lb_process_destroy(p);
    for(i=0;i<sizeof(capacities)/sizeof(capacities[0]);++i) {
        size_t size=capacities[i],expected=size>(1u<<24)?size+size/5:2*size;
        if(expected>LB_MAX_BINARY_BYTES) expected=LB_MAX_BINARY_BYTES;
        source=NIL; CHECK(lb_process_create(identity,&source,1,5,&p)==LB_PROCESS_READY);
        CHECK(lb_writable_init(p,0,&source)==LB_WRITABLE_OK); p->reg[0]=source;
        CHECK(lb_writable_append(p,source,size*8,0,1,&writer,&result)==LB_WRITABLE_OK); p->reg[0]=result;
        CHECK(lb_bitstring_view(result,&view) && view.bit_size==size*8);
        CHECK((size_t)binary_ref(result)->val->orig_size==expected);
        if(size) {
            CHECK(lb_bits_put_integer(&writer,make_small(-1),size*8,LB_BITS_BIG_ENDIAN)==LB_BITS_OK);
            CHECK(view.data[0]==0xff && view.data[size-1]==0xff);
        }
        CHECK(lb_process_collect(p,0)==LB_PROCESS_READY); lb_process_destroy(p);
    }
}
static void writable_heap_sources(LbCodeEntry *identity)
{
    const size_t sizes[]={0,1,64}; unsigned char data[64]; size_t i;
    memset(data,0xa5,sizeof(data));
    for(i=0;i<sizeof(sizes)/sizeof(sizes[0]);++i) {
        LbProcess *p; LbBitWriter writer; LbBitstringView view; Eterm result;
        CHECK(lb_process_create_binary(identity,data,sizes[i],5,&p)==LB_PROCESS_READY);
        CHECK(!p->off_heap.first && lb_bitstring_pin(p->reg[0]) && !lb_bitstring_pin(NIL));
        CHECK(lb_writable_append(p,p->reg[0],0,100,8,&writer,&result)==LB_WRITABLE_OK && result==p->reg[0]);
        CHECK(!p->writable.first && !writer.data && !writer.offset);
        CHECK(lb_writable_append(p,p->reg[0],8,1000,8,&writer,&result)==LB_WRITABLE_OK);
        CHECK(lb_bits_put_integer(&writer,make_small(0x35),8,LB_BITS_BIG_ENDIAN)==LB_BITS_OK);
        p->reg[0]=result; run(p,1);
        CHECK(lb_bitstring_view(p->reg[0],&view) && view.bit_size==(sizes[i]+1)*8 &&
              !memcmp(view.data,data,sizes[i]) && view.data[sizes[i]]==0x35);
        lb_process_destroy(p);
    }
}
static void writable_comparisons(LbCodeSpace *space,LbCodeModule *module)
{
    LbCodeEntry *same=entry(space,module,"same",2); unsigned i;
    for(i=0;i<2;++i) {
        LbProcess *p; Eterm args[2]={NIL,NIL},result; LbBitWriter writer; LbBitstringView view;
        unsigned char data=i?0xc0:0xa0;
        CHECK(lb_process_create(same,args,2,5,&p)==LB_PROCESS_READY);
        CHECK(lb_writable_init(p,0,&p->reg[0])==LB_WRITABLE_OK);
        CHECK(lb_writable_append(p,p->reg[0],3,3,1,&writer,&result)==LB_WRITABLE_OK);
        CHECK(lb_bits_put_integer(&writer,make_small(5),3,LB_BITS_BIG_ENDIAN)==LB_BITS_OK); p->reg[0]=result;
        CHECK(lb_bitstring_build(space->domain,&p->off_heap,&p->htop,&data,3,0,&p->reg[1])==LB_ALLOC_OK);
        run(p,1);
        CHECK(lb_bitstring_view(p->reg[0],&view) && view.bit_size==(i?9u:5u)*8 &&
              !memcmp(view.data,i?"different":"equal",view.bit_size/8) && !p->writable.first);
        lb_process_destroy(p);
    }
    lb_code_entry_release(same);
}
static void private_aliases(Memory *m,LbCodeEntry *identity)
{
    size_t kind;
    for(kind=0;kind<8;++kind) {
        LbProcess *p; Eterm nil=NIL,result=make_small(99),source; LbBitWriter writer={0};
        size_t calls,stack=kind==1?1:0;
        CHECK(lb_process_create(identity,&nil,1,100,&p)==LB_PROCESS_READY);
        CHECK(lb_writable_init(p,64,&p->reg[0])==LB_WRITABLE_OK); source=p->reg[0];
        switch(kind) {
        case 0:p->reg[1]=source; break;
        case 1:*--p->stop=source; break;
        case 2:p->fvalue=source; break;
        case 3:p->freason=source; break;
        case 4:p->construction_root=source; break;
        case 5:case 7:p->reg[1]=TUPLE2(p->htop,source,NIL); p->htop+=3; break;
        case 6:p->reg[1]=CONS(p->htop,source,NIL); p->htop+=2; break;
        }
        if(kind==7) p->reg[1]=NIL; /* unreachable storage is conservative evidence */
        calls=m->calls;
        CHECK(lb_writable_private_append(p,(LbBinarySource){LB_BINARY_SOURCE_X,0},1,&writer,&result)==LB_WRITABLE_NOT_UNIQUE);
        CHECK(m->calls==calls && result==make_small(99) && p->reg[0]==source && !writer.data);
        CHECK(((LbSubBits *)boxed_val(source))->end==0 && refcount(source)==1);
        p->reg[1]=p->fvalue=p->freason=p->construction_root=NIL; p->stop+=stack;
        if(kind>=5) {
            CHECK(lb_writable_private_append(p,(LbBinarySource){LB_BINARY_SOURCE_X,0},1,&writer,&result)==LB_WRITABLE_NOT_UNIQUE);
            CHECK(lb_process_collect(p,0)==LB_PROCESS_READY);
        }
        CHECK(lb_writable_private_append(p,(LbBinarySource){LB_BINARY_SOURCE_X,0},3,&writer,&result)==LB_WRITABLE_OK && p->reg[0]==NIL);
        CHECK(lb_bits_put_integer(&writer,make_small(5),3,LB_BITS_BIG_ENDIAN)==LB_BITS_OK);
        p->reg[0]=result; run(p,1); lb_process_destroy(p);
    }
    {
        LbProcess *p; Eterm nil=NIL,word; LbBitWriter writer;
        CHECK(lb_process_create(identity,&nil,1,100,&p)==LB_PROCESS_READY);
        CHECK(lb_writable_init(p,64,&p->reg[0])==LB_WRITABLE_OK); word=p->reg[0];
        /* Opaque payload words that equal a term address are not references. */
        *p->htop++=_make_header(2,POS_BIG_SUBTAG); *p->htop++=word; *p->htop++=1;
        *p->htop++=_make_header(1,FLOAT_SUBTAG); *p->htop++=word;
        *p->htop++=_make_header(2,HEAP_BITS_SUBTAG); *p->htop++=64; *p->htop++=word;
        CHECK(lb_writable_private_append(p,(LbBinarySource){LB_BINARY_SOURCE_X,0},3,&writer,&p->reg[0])==LB_WRITABLE_OK);
        CHECK(lb_bits_put_integer(&writer,make_small(5),3,LB_BITS_BIG_ENDIAN)==LB_BITS_OK);
        run(p,1); lb_process_destroy(p);
    }
}
static void private_failures(Memory *m,LbCodeEntry *identity)
{
    size_t frozen,fail;
    for(frozen=0;frozen<2;++frozen) for(fail=1;fail<=(frozen?2u:1u);++fail) {
        LbProcess *p; Eterm nil=NIL,result=make_small(99),source; LbBitWriter writer={0};
        LbSubBits snapshot; LbBinary *binary; size_t live,bytes; Eterm *heap;
        CHECK(lb_process_create(identity,&nil,1,5,&p)==LB_PROCESS_READY);
        CHECK(lb_writable_init(p,1,&p->reg[0])==LB_WRITABLE_OK);
        CHECK(lb_writable_private_append(p,(LbBinarySource){LB_BINARY_SOURCE_X,0},3,&writer,&p->reg[0])==LB_WRITABLE_OK);
        CHECK(lb_bits_put_integer(&writer,make_small(5),3,LB_BITS_BIG_ENDIAN)==LB_BITS_OK);
        if(frozen) CHECK(lb_bitstring_pin(p->reg[0]));
        *--p->stop=p->reg[0]; p->reg[0]=NIL; source=p->stop[0];
        /* Force fallback heap allocation with valid dead cons cells. */
        while((size_t)(p->stop-p->htop)>=2+S_RESERVED) {
            (void)CONS(p->htop,NIL,NIL); p->htop+=2;
        }
        snapshot=*(LbSubBits *)boxed_val(source); binary=binary_ref(source)->val;
        live=m->live; bytes=m->bytes; heap=p->heap; m->fail=m->calls+fail;
        CHECK(lb_writable_private_append(p,(LbBinarySource){LB_BINARY_SOURCE_Y,0},4096,&writer,&result)==LB_WRITABLE_NO_MEMORY);
        CHECK(p->heap==heap && p->stop[0]==source && result==make_small(99) && m->live==live && m->bytes==bytes);
        CHECK(!memcmp(&snapshot,boxed_val(source),sizeof(snapshot)) && binary->orig_bytes[0]==0xa0);
        m->fail=0;
        CHECK(lb_writable_private_append(p,(LbBinarySource){LB_BINARY_SOURCE_Y,0},4096,&writer,&result)==LB_WRITABLE_OK && p->stop[0]==NIL);
        CHECK(lb_bits_put_integer(&writer,make_small(-1),4096,LB_BITS_BIG_ENDIAN)==LB_BITS_OK);
        ++p->stop; p->reg[0]=result; run(p,1); lb_process_destroy(p);
    }
}
static void private_refusals(LbCodeEntry *identity)
{
    LbProcess *p; Eterm nil=NIL,result=make_small(99); LbBitWriter writer={0}; size_t i;
    LbBinarySource invalid[]={{LB_BINARY_SOURCE_X,MAX_REG},{LB_BINARY_SOURCE_Y,SIZE_MAX},{(LbBinarySourceKind)99,0}};
    CHECK(lb_process_create(identity,&nil,1,100,&p)==LB_PROCESS_READY);
    CHECK(lb_writable_private_append(p,(LbBinarySource){LB_BINARY_SOURCE_X,0},0,&writer,&result)==LB_WRITABLE_TYPE);
    CHECK(lb_writable_init(p,0,&p->reg[0])==LB_WRITABLE_OK);
    for(i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i)
        CHECK(lb_writable_private_append(p,invalid[i],0,&writer,&result)==LB_WRITABLE_INVALID);
    CHECK(lb_writable_private_append(p,(LbBinarySource){LB_BINARY_SOURCE_X,0},SIZE_MAX,&writer,&result)==LB_WRITABLE_LIMIT);
    ((LbSubBits *)boxed_val(p->reg[0]))->start=1;
    CHECK(lb_writable_private_append(p,(LbBinarySource){LB_BINARY_SOURCE_X,0},0,&writer,&result)==LB_WRITABLE_INVALID);
    ((LbSubBits *)boxed_val(p->reg[0]))->start=0;
    CHECK(result==make_small(99) && !writer.data);
    CHECK(lb_bitstring_pin(p->reg[0]));
    CHECK(lb_writable_private_append(p,(LbBinarySource){LB_BINARY_SOURCE_X,0},0,&writer,&p->reg[0])==LB_WRITABLE_OK);
    CHECK(binary_ref(p->reg[0])->val->orig_size==0 && writer.capacity==0 && writer.offset==0);
    CHECK(lb_writable_append(p,p->reg[0],1,0,1,&writer,&p->reg[1])==LB_WRITABLE_OK);
    CHECK(lb_bits_put_integer(&writer,make_small(1),1,LB_BITS_BIG_ENDIAN)==LB_BITS_OK);
    CHECK(lb_writable_private_append(p,(LbBinarySource){LB_BINARY_SOURCE_X,0},1,&writer,&result)==LB_WRITABLE_INVALID);
    p->reg[0]=p->reg[1]; p->reg[1]=NIL; run(p,1); lb_process_destroy(p);
    CHECK(lb_process_create(identity,&nil,1,100,&p)==LB_PROCESS_READY);
    CHECK(lb_writable_init(p,1024,&p->reg[0])==LB_WRITABLE_OK);
    CHECK(lb_writable_private_append(p,(LbBinarySource){LB_BINARY_SOURCE_X,0},3,&writer,&p->reg[0])==LB_WRITABLE_OK);
    CHECK(lb_bits_put_integer(&writer,make_small(5),3,LB_BITS_BIG_ENDIAN)==LB_BITS_OK);
    CHECK(lb_bitstring_pin(p->reg[0]));
    CHECK(lb_writable_private_append(p,(LbBinarySource){LB_BINARY_SOURCE_X,0},0,&writer,&p->reg[0])==LB_WRITABLE_OK);
    CHECK(binary_ref(p->reg[0])->val->orig_size==2 && writer.data[0]==0xa0);
    run(p,1); lb_process_destroy(p);
}
static void private_lifetime(Memory *m,LbCodeEntry *identity)
{
    LbProcess *p; Eterm nil=NIL,result,copy,storage[LB_REFC_BITS_WORDS],*top=storage;
    LbOffHeap shared={0}; LbBitWriter writer; LbBitstringView view; size_t calls,live=m->live,bytes=m->bytes;
    CHECK(lb_process_create(identity,&nil,1,100,&p)==LB_PROCESS_READY);
    CHECK(lb_writable_init(p,64,&p->reg[0])==LB_WRITABLE_OK);
    CHECK(lb_writable_append(p,p->reg[0],3,0,1,&writer,&result)==LB_WRITABLE_OK);
    p->reg[1]=p->reg[0]; p->reg[0]=result;
    CHECK(lb_bits_put_integer(&writer,make_small(5),3,LB_BITS_BIG_ENDIAN)==LB_BITS_OK);
    calls=m->calls; m->fail=calls+1;
    CHECK(lb_writable_private_append(p,(LbBinarySource){LB_BINARY_SOURCE_X,0},0,&writer,&p->reg[0])==LB_WRITABLE_OK);
    CHECK(lb_writable_private_append(p,(LbBinarySource){LB_BINARY_SOURCE_X,0},2,&writer,&p->reg[0])==LB_WRITABLE_OK && m->calls==calls);
    CHECK(lb_bits_put_integer(&writer,make_small(3),2,LB_BITS_BIG_ENDIAN)==LB_BITS_OK); m->fail=0;
    CHECK(lb_writable_private_append(p,(LbBinarySource){LB_BINARY_SOURCE_X,0},4096,&writer,&p->reg[0])==LB_WRITABLE_OK);
    CHECK(lb_bits_put_integer(&writer,make_small(0),4096,LB_BITS_BIG_ENDIAN)==LB_BITS_OK);
    CHECK(lb_bitstring_view(p->reg[1],&view) && !view.bit_size); /* old prefix follows moved backing */
    CHECK(lb_bitstring_pin(p->reg[0])); copy=lb_copy_flat(p->reg[0],&top,&shared); CHECK(!is_non_value(copy));
    CHECK(lb_writable_private_append(p,(LbBinarySource){LB_BINARY_SOURCE_X,0},0,&writer,&p->reg[0])==LB_WRITABLE_OK);
    CHECK(binary_ref(copy)->val!=binary_ref(p->reg[0])->val && refcount(copy)==2);
    CHECK(lb_bitstring_view(copy,&view) && view.bit_size==4101 && view.data[0]==0xb8);
    run(p,1); lb_process_destroy(p);
    CHECK(refcount(copy)==1 && m->live==live+1 && lb_bitstring_view(copy,&view) && view.data[0]==0xb8);
    lb_offheap_clear(&shared); CHECK(m->live==live && m->bytes==bytes);
}
static void shared_prefix_segments(LbCodeEntry *identity,int vectors)
{
    unsigned char pattern[33]; size_t n,i; unsigned private;
    for(i=0;i<sizeof(pattern);++i) pattern[i]=(unsigned char)(0xa5+i*73);
    for(private=0;private<2;++private) for(n=1;n<=257;++n) {
        Eterm nil=NIL,storage[LB_REFC_BITS_WORDS],*top=storage,copy; LbOffHeap shared={0};
        LbProcess *p; LbBitWriter writer; LbBitstringView view;
        CHECK(lb_process_create(identity,&nil,1,5,&p)==LB_PROCESS_READY);
        CHECK(lb_writable_init(p,64,&p->reg[0])==LB_WRITABLE_OK);
        CHECK(lb_writable_append(p,p->reg[0],n,0,1,&writer,&p->reg[0])==LB_WRITABLE_OK);
        CHECK(lb_bits_put_string(&writer,pattern,n/8)==LB_BITS_OK);
        if(n&7) CHECK(lb_bits_put_integer(&writer,make_small(pattern[n/8]>>(8-(n&7))),n&7,LB_BITS_BIG_ENDIAN)==LB_BITS_OK);
        CHECK(writer.offset==n); p->reg[1]=p->reg[0];
        CHECK(lb_writable_append(p,p->reg[0],private?1:n,0,1,&writer,&p->reg[0])==LB_WRITABLE_OK);
        if(private) {
            CHECK(lb_bits_put_integer(&writer,make_small(0),1,LB_BITS_BIG_ENDIAN)==LB_BITS_OK);
            CHECK(lb_writable_private_append(p,(LbBinarySource){LB_BINARY_SOURCE_X,0},n,&writer,&p->reg[0])==LB_WRITABLE_OK);
        }
        /* The old prefix follows the BinRef even after growth. Its final byte
         * can also contain destination bits, but its logical value is stable. */
        CHECK(lb_bits_put_binary_all(&writer,p->reg[1],1)==LB_BITS_OK && writer.offset==2*n+private);
        CHECK(lb_bitstring_view(p->reg[1],&view) && view.bit_size==n);
        for(i=0;i<n;++i) CHECK(bit_at(view.data,view.bit_offset+i)==bit_at(pattern,i));
        CHECK(lb_bitstring_pin(p->reg[0])); copy=lb_copy_flat(p->reg[0],&top,&shared); CHECK(!is_non_value(copy));
        run(p,1); lb_process_destroy(p);
        CHECK(lb_bitstring_view(copy,&view) && view.bit_size==2*n+private);
        for(i=0;i<n;++i) {
            CHECK(bit_at(view.data,view.bit_offset+i)==bit_at(pattern,i));
            CHECK(bit_at(view.data,view.bit_offset+n+private+i)==bit_at(pattern,i));
        }
        if(private) CHECK(!bit_at(view.data,view.bit_offset+n));
        if(vectors) {
            CHECK(!view.bit_offset); printf("%u:%zu:",private,n);
            for(i=0;i<(view.bit_size+7)/8;++i) printf("%02x",view.data[i]);
            puts("");
        }
        lb_offheap_clear(&shared);
    }
    if(!vectors) puts("CORE_BINARY_SHARED_EDGE_OK normal_append=true private_append=true volatile_prefix=true lengths=257 source_bits_preserved=true backing_growth=true identity_execution=true moving_gc=true copied_lifetime=true exact_retirement=true guest_construction=false");
}
static void fresh_binary_failures(Memory *m,LbCodeEntry *identity)
{
    const size_t sizes[]={0,1,512,513,520,65536*8}; size_t i,fail,prefixes=0;
    for(i=0;i<sizeof(sizes)/sizeof(sizes[0]);++i) {
        size_t allocations=sizes[i]>512?2:1;
        for(fail=1;fail<=allocations;++fail) {
            Eterm nil=NIL,result=make_small(17); LbProcess *p; LbBitWriter writer={NULL,77,88};
            Eterm *heap,*top,*stop; size_t live,bytes; unsigned char *data;
            CHECK(lb_process_create(identity,&nil,1,5,&p)==LB_PROCESS_READY);
            heap=p->heap; top=p->htop; stop=p->stop; live=m->live; bytes=m->bytes;
            m->fail=m->calls+fail;
            CHECK(lb_binary_begin(p,sizes[i],0,&writer,&result)==LB_WRITABLE_NO_MEMORY);
            CHECK(m->live==live && m->bytes==bytes && result==make_small(17));
            CHECK(!writer.data && writer.capacity==77 && writer.offset==88);
            CHECK(p->heap==heap && p->htop==top && p->stop==stop && !p->heap_reserved);
            CHECK(!p->off_heap.first && !p->off_heap.overhead && !p->writable.first && p->reg[0]==NIL);
            m->fail=0; ++prefixes;
            CHECK(lb_binary_begin(p,sizes[i],0,&writer,&p->reg[0])==LB_WRITABLE_OK);
            CHECK(!writer.offset && writer.capacity==(sizes[i]+7)/8);
            data=writer.data;
            for(size_t j=0;j<writer.capacity;++j) CHECK(!data[j]);
            CHECK(lb_bits_put_integer(&writer,make_small(-1),sizes[i],LB_BITS_BIG_ENDIAN)==LB_BITS_OK);
            CHECK(writer.offset==sizes[i]); run(p,1); lb_process_destroy(p);
        }
    }
    CHECK(prefixes==9);
}
static void fresh_binary_sizes(Memory *m,LbCodeEntry *identity)
{
    const size_t sizes[]={0,1,7,8,63,64,511,512,513,519,520,4099,65536*8};
    size_t i,j;
    for(i=0;i<sizeof(sizes)/sizeof(sizes[0]);++i) {
        Eterm nil=NIL,storage[16],*top=storage,copy; LbOffHeap shared={0}; LbProcess *p;
        LbBitWriter writer; LbBitstringView view; size_t calls;
        CHECK(lb_process_create(identity,&nil,1,100,&p)==LB_PROCESS_READY);
        p->heap_reserved=9; calls=m->calls;
        if(sizes[i]<=512) m->fail=calls+1;
        CHECK(lb_binary_begin(p,sizes[i],3,&writer,&p->reg[0])==LB_WRITABLE_OK);
        CHECK(m->calls==calls+(sizes[i]>512) && p->heap_reserved==9 && !p->writable.first);
        CHECK((size_t)(p->stop-p->htop)>=9+S_RESERVED); m->fail=0;
        if(sizes[i]<=512) {
            CHECK(!p->off_heap.first && (*boxed_val(p->reg[0])&_HEADER_SUBTAG_MASK)==HEAP_BITS_SUBTAG);
            for(j=0;j<(lb_bitstring_heap_words(sizes[i])-2)*sizeof(Eterm);++j) CHECK(!writer.data[j]);
        } else {
            CHECK(refcount(p->reg[0])==1 && !binary_ref(p->reg[0])->val->intern.flags);
            CHECK((size_t)binary_ref(p->reg[0])->val->orig_size==writer.capacity);
        }
        CHECK(lb_bits_put_integer(&writer,make_small(-1),sizes[i],LB_BITS_BIG_ENDIAN)==LB_BITS_OK);
        CHECK(writer.offset==sizes[i]);
        copy=lb_copy_flat(p->reg[0],&top,&shared); CHECK(!is_non_value(copy));
        run(p,1); CHECK(lb_bitstring_view(p->reg[0],&view) && view.bit_size==sizes[i]);
        lb_process_destroy(p);
        CHECK(lb_bitstring_view(copy,&view) && view.bit_size==sizes[i] && !view.bit_offset);
        for(j=0;j<sizes[i];++j) CHECK(bit_at(view.data,j));
        for(j=sizes[i];j<(sizes[i]+7)/8*8;++j) CHECK(!bit_at(view.data,j));
        lb_offheap_clear(&shared);
    }
}
static void fresh_binary_roots(Memory *m,LbCodeEntry *identity)
{
    unsigned char input[65]; LbProcess *p; LbBitWriter writer={NULL,77,88}; LbBitstringView view; Eterm source;
    memset(input,0x57,sizeof(input));
    CHECK(lb_process_create_binary(identity,input,sizeof(input),5,&p)==LB_PROCESS_READY);
    CHECK(lb_heap_reserve(p,4,MAX_REG));
    p->reg[1]=TUPLE2(p->htop,p->reg[0],make_small(2)); p->htop+=3;
    *--p->stop=p->reg[0]; p->reg[0]=NIL; p->heap_reserved=5;
    size_t collections=p->collections;
    for(size_t fail=1;fail<=2;++fail) {
        Eterm *heap=p->heap,*top=p->htop,*stop=p->stop,tuple=p->reg[1],saved=*stop;
        LbBinRef *first=p->off_heap.first; size_t live=m->live,bytes=m->bytes; Uint overhead=p->off_heap.overhead;
        m->fail=m->calls+fail;
        CHECK(lb_binary_begin(p,525,4096,&writer,&p->reg[0])==LB_WRITABLE_NO_MEMORY);
        CHECK(p->heap==heap && p->htop==top && p->stop==stop && p->reg[1]==tuple && *stop==saved && p->reg[0]==NIL);
        CHECK(!writer.data && writer.capacity==77 && writer.offset==88 && p->heap_reserved==5);
        CHECK(p->off_heap.first==first && p->off_heap.overhead==overhead && refcount(saved)==1);
        CHECK(m->live==live && m->bytes==bytes && p->collections==collections); m->fail=0;
    }
    CHECK(lb_binary_begin(p,525,4096,&writer,&p->reg[0])==LB_WRITABLE_OK);
    CHECK(p->collections==collections+1 && p->heap_reserved==5 && (size_t)(p->stop-p->htop)>=4096+S_RESERVED);
    source=tuple_val(p->reg[1])[1]; CHECK(*p->stop++==source);
    CHECK(lb_bits_put_integer(&writer,make_small(31),5,LB_BITS_BIG_ENDIAN)==LB_BITS_OK);
    CHECK(lb_bits_put_binary_all(&writer,source,1)==LB_BITS_OK && writer.offset==525);
    run(p,1); CHECK(lb_bitstring_view(p->reg[0],&view) && view.bit_size==525);
    for(size_t i=0;i<5;++i) CHECK(bit_at(view.data,i));
    for(size_t i=0;i<520;++i) CHECK(bit_at(view.data,i+5)==bit_at(input,i));
    lb_process_destroy(p);
}
static void fresh_binary_abandon(Memory *m,LbCodeEntry *identity)
{
    Eterm nil=NIL,unpublished; LbProcess *p; LbBitWriter writer;
    CHECK(lb_process_create(identity,&nil,1,5,&p)==LB_PROCESS_READY);
    CHECK(lb_binary_begin(p,525,0,&writer,&unpublished)==LB_WRITABLE_OK);
    CHECK(lb_bits_put_integer(&writer,make_small(31),5,LB_BITS_BIG_ENDIAN)==LB_BITS_OK);
    CHECK(lb_bits_put_integer(&writer,am_badarg,520,LB_BITS_BIG_ENDIAN)==LB_BITS_BADARG && writer.offset==5);
    /* Abandon the unrooted result after a real writer failure. Creating the
     * error payload may itself fail, but must not leak this partial backing. */
    while((size_t)(p->stop-p->htop)>=2+S_RESERVED) {
        (void)CONS(p->htop,NIL,NIL); p->htop+=2;
    }
    size_t live=m->live,bytes=m->bytes; LbBinRef *partial=p->off_heap.first;
    CHECK(partial && !p->writable.first); m->fail=m->calls+1;
    CHECK(lb_binary_error_payload(p,LB_BSC_INTEGER,2,am_type,am_badarg,&p->reg[0])==LB_ALLOC_NO_MEMORY);
    CHECK(p->reg[0]==NIL && p->off_heap.first==partial && m->live==live && m->bytes==bytes); m->fail=0;
    CHECK(lb_binary_error_payload(p,LB_BSC_INTEGER,2,am_type,am_badarg,&p->reg[0])==LB_ALLOC_OK);
    CHECK(!p->off_heap.first && !p->off_heap.overhead); /* writer span now expired */
    run(p,1); lb_process_destroy(p);
}
static void fresh_binary_limits(Memory *m,LbCodeEntry *identity)
{
    Eterm nil=NIL,result=make_small(17); LbProcess *p; LbBitWriter writer={NULL,77,88}; size_t calls,live,bytes;
    CHECK(lb_process_create(identity,&nil,1,100,&p)==LB_PROCESS_READY);
    calls=m->calls; live=m->live; bytes=m->bytes;
    CHECK(lb_binary_begin(p,SIZE_MAX,0,&writer,&result)==LB_WRITABLE_LIMIT);
    CHECK(lb_binary_begin(p,LB_MAX_BINARY_BYTES*8+1,0,&writer,&result)==LB_WRITABLE_LIMIT);
    CHECK(lb_binary_begin(NULL,0,0,&writer,&result)==LB_WRITABLE_INVALID);
    CHECK(lb_binary_begin(p,0,0,NULL,&result)==LB_WRITABLE_INVALID);
    CHECK(lb_binary_begin(p,0,0,&writer,NULL)==LB_WRITABLE_INVALID);
    CHECK(m->calls==calls && !writer.data && writer.capacity==77 && writer.offset==88 && result==make_small(17));
    CHECK(lb_binary_begin(p,513,SIZE_MAX,&writer,&result)==LB_WRITABLE_NO_MEMORY);
    CHECK(m->calls==calls && m->live==live && m->bytes==bytes && !p->off_heap.first && !writer.data && result==make_small(17));
    p->off_heap.overhead=UINT64_MAX;
    CHECK(lb_binary_begin(p,513,0,&writer,&result)==LB_WRITABLE_NO_MEMORY && m->calls==calls);
    p->off_heap.overhead=0;
    CHECK(lb_binary_begin(p,LB_MAX_BINARY_BYTES*8,0,&writer,&p->reg[0])==LB_WRITABLE_OK);
    CHECK(writer.capacity==LB_MAX_BINARY_BYTES && !writer.data[0] && !writer.data[writer.capacity-1]);
    CHECK(lb_bits_put_integer(&writer,make_small(0),LB_MAX_BINARY_BYTES*8,LB_BITS_BIG_ENDIAN)==LB_BITS_OK);
    run(p,1); lb_process_destroy(p);
}
static void writable_binaries(Memory *m,LbCodeSpace *space,LbCodeModule *module)
{
    LbCodeEntry *identity=entry(space,module,"identity",1); size_t live=m->live,bytes=m->bytes;
    shared_prefix_segments(identity,0); CHECK(m->live==live && m->bytes==bytes);
    fresh_binary_failures(m,identity); CHECK(m->live==live && m->bytes==bytes);
    fresh_binary_sizes(m,identity); CHECK(m->live==live && m->bytes==bytes);
    fresh_binary_roots(m,identity); CHECK(m->live==live && m->bytes==bytes);
    fresh_binary_limits(m,identity); CHECK(m->live==live && m->bytes==bytes);
    fresh_binary_abandon(m,identity); CHECK(m->live==live && m->bytes==bytes);
    puts("CORE_BINARY_BEGIN_OK native_heap_refc=true initialized_slack=true failure_prefixes=11 retry=true pending_credit=true extra_reservation=true x_y_operand_relocation=true identity_execution=true copied_lifetime=true exact_retirement=true maximum_capacity=true partial_failure_retirement=true error_payload_oom_retry=true guest_construction=false");
    writable_failures(m,identity); CHECK(m->live==live && m->bytes==bytes);
    writable_windows(identity); CHECK(m->live==live && m->bytes==bytes);
    writable_lifetimes(m,identity); CHECK(m->live==live && m->bytes==bytes);
    writable_boundaries(identity); CHECK(m->live==live && m->bytes==bytes);
    writable_heap_sources(identity); CHECK(m->live==live && m->bytes==bytes);
    writable_comparisons(space,module); CHECK(m->live==live && m->bytes==bytes);
    private_aliases(m,identity); CHECK(m->live==live && m->bytes==bytes);
    private_failures(m,identity); CHECK(m->live==live && m->bytes==bytes);
    private_lifetime(m,identity); CHECK(m->live==live && m->bytes==bytes);
    private_refusals(identity); CHECK(m->live==live && m->bytes==bytes);
    lb_code_entry_release(identity);
    puts("CORE_PRIVATE_APPEND_OK checked_unique_slot=true conservative_dead_aliases=true opaque_payloads=true x_y_roots=true frozen_fallback=true zero_extension=true allocation_failures=3 retry=true moving_gc=true identity_execution=true exact_retirement=true guest_construction=false");
    puts("CORE_WRITABLE_OK c_append=true volatile_prefixes=true moving_gc=true pin_and_copy=true failure_prefixes=8 retry=true windows=4128 identity_execution=true exact_comparison_execution=true capacity_boundaries=true exact_retirement=true guest_construction=false private_append_instruction=false");
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
static void unaligned_results(Memory *memory,LbCodeSpace *space,LbCodeModule *module)
{
    LbCodeEntry *identity=entry(space,module,"identity",1);
    unsigned char data[66],expected[64],copied[64]; size_t offset,i,n;
    size_t live=memory->live,bytes=memory->bytes;
    for(i=0;i<sizeof(data);++i) data[i]=(unsigned char)(i*173+19);
    for(offset=0;offset<16;++offset) for(n=0;n<=512;++n) {
        LbProcess *p; LbSubBits *sub; LbBitstringView view;
        CHECK(lb_process_create_binary(identity,data,sizeof(data),5,&p)==LB_PROCESS_READY);
        /* A valid private native argument view. No guest slicing instruction
         * is claimed: the guest executes the actual identity/1 BEAM code. */
        sub=(LbSubBits *)boxed_val(p->reg[0]); CHECK(sub->thing_word==LB_HEADER_SUB_BITS);
        sub->start=offset; sub->end=offset+n;
        run(p,1);
        CHECK(lb_bitstring_view(lb_process_result(p),&view) && view.bit_offset==offset && view.bit_size==n);
        memset(expected,0xa5,sizeof(expected)); memset(copied,0xa5,sizeof(copied));
        for(i=0;i<n;++i) {
            unsigned bit=(data[(offset+i)/8]>>(7-(offset+i)%8))&1,mask=1u<<(7-i%8);
            expected[i/8]=(unsigned char)((expected[i/8]&~mask)|(bit?mask:0));
        }
        CHECK(lb_bits_copy(view.data,(offset+n+7)/8,offset,copied,sizeof(copied),0,n));
        CHECK(!memcmp(copied,expected,sizeof(copied)) && refcount(lb_process_result(p))==1);
        lb_process_destroy(p); CHECK(memory->live==live && memory->bytes==bytes);
    }
    lb_code_entry_release(identity);
    puts("CORE_BINARY_BIT_COPY_OK native_argument_views=true identity_execution=true moving_gc=true offsets=16 lengths=513 exact_retirement=true guest_slicing=false");
}
static void constructed_arguments(Memory *memory,LbCodeSpace *space,LbCodeModule *module)
{
    const size_t widths[]={169,4099}; unsigned char data[600],copied[600];
    Eterm number[3]={_make_header(2,NEG_BIG_SUBTAG),1,1},argument=NIL;
    Eterm source_storage[4],*source_top=source_storage,source_term,fp_words[2]={_make_header(1,FLOAT_SUBTAG),0};
    LbOffHeap source_chain={0}; double fp=0x1.0020000000001p0;
    LbCodeEntry *identity=entry(space,module,"identity",1); size_t i;
    size_t live=memory->live,bytes=memory->bytes;
    memcpy(fp_words+1,&fp,sizeof(fp));
    CHECK(lb_bitstring_build(space->domain,&source_chain,&source_top,(const unsigned char *)"native",48,0,&source_term)==LB_ALLOC_OK);
    CHECK(!source_chain.first);
    for(i=0;i<sizeof(widths)/sizeof(*widths);++i) {
        LbBitWriter w; LbProcess *p; LbBitstringView view; size_t words,bit_size;
        memset(data,0,sizeof(data)); CHECK(lb_bit_writer_init(&w,data,sizeof(data))==LB_BITS_OK);
        CHECK(lb_bits_put_integer(&w,make_small(17),5,LB_BITS_BIG_ENDIAN)==LB_BITS_OK);
        CHECK(lb_bits_put_integer(&w,make_boxed(number),widths[i],LB_BITS_LITTLE_ENDIAN)==LB_BITS_OK);
        CHECK(lb_bits_put_utf8(&w,make_small(0x2603))==LB_BITS_OK);
        CHECK(lb_bits_put_utf16(&w,make_small(0x1d11e),LB_BITS_LITTLE_ENDIAN)==LB_BITS_OK);
        CHECK(lb_bits_put_utf32(&w,make_small(0x10ffff),LB_BITS_BIG_ENDIAN)==LB_BITS_OK);
        CHECK(lb_bits_put_integer(&w,make_small(-7),15,LB_BITS_BIG_ENDIAN)==LB_BITS_OK);
        CHECK(lb_bits_put_string(&w,(const unsigned char *)"value:",6)==LB_BITS_OK);
        CHECK(lb_bits_put_binary(&w,source_term,13)==LB_BITS_OK);
        CHECK(lb_bits_put_binary_all(&w,source_term,8)==LB_BITS_OK);
        CHECK(lb_bits_put_float(&w,make_boxed(fp_words),16,LB_BITS_BIG_ENDIAN).status==LB_BITS_OK);
        CHECK(lb_bits_put_float(&w,make_small(((Sint)1<<58)+((Sint)1<<34)+1),32,LB_BITS_LITTLE_ENDIAN).status==LB_BITS_OK);
        CHECK(lb_bits_put_float(&w,make_boxed(number),64,LB_BITS_BIG_ENDIAN).status==LB_BITS_OK);
        bit_size=w.offset; words=lb_bitstring_heap_words(bit_size);
        CHECK(lb_process_create(identity,&argument,1,5,&p)==LB_PROCESS_READY);
        CHECK(lb_heap_reserve(p,words,1));
        CHECK(lb_bitstring_build(space->domain,&p->off_heap,&p->htop,data,bit_size,0,&p->reg[0])==LB_ALLOC_OK);
        memcpy(copied,data,sizeof(data)); memset(data,0xa5,sizeof(data)); run(p,1);
        CHECK(lb_bitstring_view(lb_process_result(p),&view) && view.bit_size==bit_size);
        memset(data,0,sizeof(data));
        CHECK(lb_bits_copy(view.data,(view.bit_offset+bit_size+7)/8,view.bit_offset,data,sizeof(data),0,bit_size));
        CHECK(!memcmp(data,copied,sizeof(data)));
        CHECK((bit_size>512)==(p->off_heap.first!=NULL));
        lb_process_destroy(p); CHECK(memory->live==live && memory->bytes==bytes);
    }
    lb_code_entry_release(identity);
    puts("CORE_BINARY_WRITER_OK c_constructed_arguments=true float_binary_string=true heap_offheap=true identity_execution=true moving_gc=true copied_lifetime=true exact_retirement=true guest_construction=false");
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
    unaligned_results(&m,space,module);
    constructed_arguments(&m,space,module);
    writable_binaries(&m,space,module);
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
static void alias_reference(const char *path)
{
    Memory m={0}; LbSystemAllocator callbacks={&m,allocate,release}; LbEngine *engine; LbCodeSpace *space;
    LbCodeModule *module; LbCodeEntry *identity;
    CHECK(lb_engine_create(&callbacks,&engine)==LB_ENGINE_OK && lb_code_space_create(engine,&space)==LB_CODE_OK);
    module=load(space,path); identity=entry(space,module,"identity",1);
    shared_prefix_segments(identity,1); lb_code_entry_release(identity);
    CHECK(lb_code_unload(module)==LB_CODE_OK && lb_code_space_destroy(space)==LB_CODE_OK);
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine); CHECK(!m.live && !m.bytes);
}
int main(int argc,char **argv)
{
    if(argc==3 && !strcmp(argv[1],"--alias-reference")) { alias_reference(argv[2]); return 0; }
    component_test();
    if(argc==2 || argc==4) executable_test(argv[1]); else CHECK(argc==1);
    if(argc==4) source_retirement(argv[1],argv[2],argv[3]);
    return 0;
}
