/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#include "process.h"
#include "engine_internal.h"
#include "md5.h"
#include "opcodes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
static void release(void *context,void *ptr)
{
    Memory *m=context; Header *h=(Header *)ptr-1;
    CHECK(m->live && m->bytes>=h->size); --m->live; m->bytes-=h->size;
    memset(ptr,0xa5,h->size); free(h);
}
static Eterm atom(LbCodeSpace *space,const char *name)
{
    Eterm term;
    CHECK(lb_atoms_find(lb_code_space_atoms(space),name,strlen(name),&term)==LB_ATOM_OK); return term;
}
static LbProcess *start(LbCodeSpace *space,LbCodeModule *module,const char *name,const Eterm *args,size_t count)
{
    LbCodeEntry *entry=NULL; LbProcess *p=NULL;
    CHECK(lb_code_entry_acquire(space,lb_code_module_name(module),atom(space,name),(unsigned)count,&entry)==LB_CODE_OK);
    CHECK(lb_process_create(entry,args,count,5,&p)==LB_PROCESS_READY);
    lb_code_entry_release(entry); CHECK(lb_code_unload(module)==LB_CODE_BUSY);
    return p;
}
static void run(LbProcess *p,LbProcessStatus expected)
{
    size_t i; LbProcessStatus status=LB_PROCESS_YIELDED;
    for(i=0;i<1000 && status==LB_PROCESS_YIELDED;++i) status=lb_process_run(p,1,100);
    if(status!=expected) fprintf(stderr,"run status=%d expected=%d steps=%zu\n",status,expected,i);
    CHECK(status==expected);
}
static size_t list_length(Eterm term)
{
    size_t count=0;
    while(is_list(term)) { term=CDR(list_val(term)); CHECK(++count<100000); }
    CHECK(is_nil(term)); return count;
}
static void semantics(LbCodeSpace *space,LbCodeModule *module)
{
    LbProcess *p; Eterm arg,result,old;
    p=start(space,module,"value",NULL,0); run(p,LB_PROCESS_DONE);
    CHECK(lb_process_result(p)==make_small(42)); lb_process_destroy(p);
    arg=make_small(73); p=start(space,module,"identity",&arg,1); run(p,LB_PROCESS_DONE);
    CHECK(lb_process_result(p)==arg); lb_process_destroy(p);
    arg=make_small(17); p=start(space,module,"pair",&arg,1); run(p,LB_PROCESS_DONE);
    result=lb_process_result(p); CHECK(is_tuple(result) && arityval(*tuple_val(result))==2);
    CHECK(tuple_val(result)[1]==arg && tuple_val(result)[2]==make_small(42));
    CHECK(lb_process_collections(p)>0); old=result;
    CHECK(lb_process_collect(p,100)==LB_PROCESS_DONE); result=lb_process_result(p);
    CHECK(result!=old && tuple_val(result)[1]==arg && tuple_val(result)[2]==make_small(42));
    lb_process_destroy(p);
    p=start(space,module,"unicode",NULL,0); run(p,LB_PROCESS_DONE);
    { LbBeamBytes name; CHECK(lb_atoms_name(lb_code_space_atoms(space),lb_process_result(p),&name)==LB_ATOM_OK);
      CHECK(name.size==6 && !memcmp(name.data,"\xce\xbb\xf0\x9f\xa4\x96",6)); }
    lb_process_destroy(p);
    p=start(space,module,"literal",NULL,0); run(p,LB_PROCESS_DONE);
    result=lb_process_result(p); CHECK(is_tuple(result) && is_literal_ptr(result));
    CHECK(list_length(tuple_val(result)[2])==3);
    CHECK(lb_process_collect(p,1)==LB_PROCESS_DONE && lb_process_result(p)==result);
    lb_process_destroy(p);
    { const char *keys[]={"module","exports","functions","attributes","compile","md5","native","nifs","native_addresses"}; size_t i;
      for(i=0;i<sizeof(keys)/sizeof(keys[0]);++i) {
        arg=atom(space,keys[i]); p=start(space,module,"module_info",&arg,1); run(p,LB_PROCESS_DONE); result=lb_process_result(p);
        if(i==0) CHECK(result==lb_code_module_name(module));
        if(i==1 || i==2) CHECK(list_length(result)==7);
        if(i==3 || i==4) CHECK(list_length(result)>0 && !is_literal_ptr(result));
        if(i==5) { size_t j; Eterm *bits=boxed_val(result); CHECK((bits[0]&_HEADER_SUBTAG_MASK)==HEAP_BITS_SUBTAG && bits[1]==128);
          printf("CORE_MODULE_MD5 "); for(j=0;j<16;++j) printf("%02x",((unsigned char *)(bits+2))[j]); puts(""); }
        if(i==6) CHECK(result==atom(space,"false"));
        if(i>=7) CHECK(is_nil(result));
        CHECK(lb_process_collect(p,1)==LB_PROCESS_DONE); lb_process_destroy(p);
      }
    }
    p=start(space,module,"module_info",NULL,0); run(p,LB_PROCESS_DONE); CHECK(list_length(lb_process_result(p))==5);
    CHECK(lb_process_collect(p,0)==LB_PROCESS_DONE && list_length(lb_process_result(p))==5); lb_process_destroy(p);
    arg=make_small(123); p=start(space,module,"module_info",&arg,1); run(p,LB_PROCESS_EXCEPTION);
    CHECK(lb_process_exception(p)==atom(space,"badarg")); lb_process_destroy(p);
}
static uint32_t be32(const unsigned char *p)
{
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
}
static void put32(unsigned char *p,uint32_t value)
{
    p[0]=(unsigned char)(value>>24); p[1]=(unsigned char)(value>>16); p[2]=(unsigned char)(value>>8); p[3]=(unsigned char)value;
}
static void arity_regression(LbEngine *engine,Memory *m,const unsigned char *data,size_t size)
{
    LbAllocDomain *domain=engine->domain;
    LbCodeSpace *space; LbBeamProgram *program; const LbBeamOp *op; LbCodeModule *module; LbBeamError error;
    size_t offset=SIZE_MAX,pos=12,n=0,old_end,new_end,at,total,live,bytes,atoms;
    unsigned char *bad;
    CHECK(lb_code_space_create(engine,&space)==LB_CODE_OK);
    CHECK(lb_beam_program_prepare(domain,lb_code_space_atoms(space),data,size,&program,&error)==LB_BEAM_OK);
    for(op=lb_beam_program_ops(program);op;op=op->next)
        if(op->op==genop_call_ext_only_2 && op->a[0].val==2 && op->a[1].val==1) offset=op->offset;
    CHECK(offset!=SIZE_MAX); lb_beam_program_destroy(program);
    while(pos<size) { n=be32(data+pos+4); if(!memcmp(data+pos,"Code",4)) break; pos+=8+((n+3)&~(size_t)3); }
    CHECK(pos<size);
    at=pos+8+4+be32(data+pos+8)+offset+2;
    CHECK(data[at]==0x10 && data[at-1]==0x20 && data[at-2]==genop_call_ext_only_2);
    old_end=pos+8+((n+3)&~(size_t)3); new_end=pos+8+((n+2+3)&~(size_t)3); total=size+new_end-old_end;
    bad=malloc(total); CHECK(bad); memcpy(bad,data,at);
    /* Legal compact extended-list encoding, but illegal for this non-variadic
     * instruction: keep import index 1, smuggle one trailing operand. */
    bad[at]=0x17; bad[at+1]=0x10; bad[at+2]=0x00;
    memcpy(bad+at+3,data+at+1,pos+8+n-at-1);
    memset(bad+pos+8+n+2,0,new_end-(pos+8+n+2));
    memcpy(bad+new_end,data+old_end,size-old_end);
    put32(bad+4,(uint32_t)total-8); put32(bad+pos+4,(uint32_t)n+2);
    live=m->live; bytes=m->bytes; atoms=lb_atoms_count(lb_code_space_atoms(space));
    CHECK(lb_code_load(space,bad,total,&module,&error)==LB_CODE_FORMAT && !module);
    CHECK(!strcmp(error.stage,"operation arity"));
    CHECK(m->live==live && m->bytes==bytes && lb_atoms_count(lb_code_space_atoms(space))==atoms);
    free(bad); CHECK(lb_code_space_destroy(space)==LB_CODE_OK);
}
static void file_test(const char *path)
{
    FILE *file=fopen(path,"rb"); long length; unsigned char *data;
    Memory m={0}; LbSystemAllocator callbacks={&m,allocate,release}; LbEngine *engine=NULL;
    LbCodeSpace *space=NULL,*peer=NULL; LbCodeModule *module=NULL,*other=NULL; LbBeamError error;
    size_t calls,i,live,bytes,atoms; LbCodeStatus status;
    CHECK(file && !fseek(file,0,SEEK_END)); length=ftell(file); CHECK(length>0 && length<8*1024*1024 && !fseek(file,0,SEEK_SET));
    data=malloc((size_t)length); CHECK(data && fread(data,1,(size_t)length,file)==(size_t)length && !fclose(file));
    CHECK(lb_engine_create(&callbacks,&engine)==LB_ENGINE_OK);
    CHECK(lb_code_space_create(engine,&space)==LB_CODE_OK); calls=m.calls;
    status=lb_code_load(space,data,(size_t)length,&module,&error);
    if(status!=LB_CODE_OK) fprintf(stderr,"load status=%d stage=%s offset=%zu\n",status,error.stage,error.offset);
    CHECK(status==LB_CODE_OK); calls=m.calls-calls;
    { LbCodeModule *duplicate=NULL;
      live=m.live; bytes=m.bytes; atoms=lb_atoms_count(lb_code_space_atoms(space));
      CHECK(lb_code_load(space,data,(size_t)length,&duplicate,&error)==LB_CODE_EXISTS && !duplicate && error.status!=LB_BEAM_OK);
      CHECK(m.live==live && m.bytes==bytes && lb_atoms_count(lb_code_space_atoms(space))==atoms); }
    CHECK(lb_code_space_destroy(space)==LB_CODE_BUSY);
    semantics(space,module);
    CHECK(lb_code_space_create(engine,&peer)==LB_CODE_OK);
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_BUSY && lb_engine_is_open(engine));
    CHECK(lb_code_load(peer,data,(size_t)length,&other,&error)==LB_CODE_OK);
    { LbProcess *p=start(peer,other,"value",NULL,0);
      CHECK(lb_code_unload(module)==LB_CODE_OK && lb_code_space_destroy(space)==LB_CODE_OK);
      run(p,LB_PROCESS_DONE); CHECK(lb_process_result(p)==make_small(42)); lb_process_destroy(p); }
    CHECK(lb_code_unload(other)==LB_CODE_OK && lb_code_space_destroy(peer)==LB_CODE_OK);
    arity_regression(engine,&m,data,(size_t)length);
    /* Every allocation prefix of transformation, emission, linking and atom
     * preparation, including failure after provisional names are assigned. */
    for(i=1;i<=calls;++i) {
        CHECK(lb_code_space_create(engine,&space)==LB_CODE_OK);
        live=m.live; bytes=m.bytes; atoms=lb_atoms_count(lb_code_space_atoms(space));
        m.fail=m.calls+i;
        status=lb_code_load(space,data,(size_t)length,&module,&error);
        if(status!=LB_CODE_NO_MEMORY) fprintf(stderr,"prefix=%zu/%zu status=%d stage=%s\n",i,calls,status,error.stage);
        CHECK(status==LB_CODE_NO_MEMORY && module==NULL);
        CHECK(m.live==live && m.bytes==bytes && lb_atoms_count(lb_code_space_atoms(space))==atoms);
        m.fail=0;
        CHECK(lb_code_load(space,data,(size_t)length,&module,&error)==LB_CODE_OK);
        CHECK(lb_code_unload(module)==LB_CODE_OK && lb_code_space_destroy(space)==LB_CODE_OK);
    }
    CHECK(lb_code_space_create(engine,&space)==LB_CODE_OK);
    for(i=0;i<384;++i) {
        size_t pos=(i*131+17)%(size_t)length;
        unsigned char saved=data[pos];
        live=m.live; bytes=m.bytes; atoms=lb_atoms_count(lb_code_space_atoms(space));
        data[pos]^=(unsigned char)(1u<<(i%8));
        status=lb_code_load(space,data,(size_t)length,&module,&error); data[pos]=saved;
        if(status==LB_CODE_OK) CHECK(lb_code_unload(module)==LB_CODE_OK);
        else CHECK(module==NULL && m.live==live && m.bytes==bytes && lb_atoms_count(lb_code_space_atoms(space))==atoms);
    }
    CHECK(lb_code_space_destroy(space)==LB_CODE_OK);
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine);
    CHECK(!m.live && !m.bytes); free(data);
    printf("CORE_CODE_EXECUTION_OK native_words=true generated_dispatch=true forced_gc=true errors=true yields=true failure_prefixes=%zu engine_lifecycle=false\n",calls);
}
static unsigned char *read_image(const char *path,size_t *size)
{
    FILE *f=fopen(path,"rb"); long length; unsigned char *bytes;
    CHECK(f && !fseek(f,0,SEEK_END)); length=ftell(f); CHECK(length>0 && length<8*1024*1024 && !fseek(f,0,SEEK_SET));
    bytes=malloc((size_t)length); CHECK(bytes && fread(bytes,1,(size_t)length,f)==(size_t)length && !fclose(f));
    *size=(size_t)length; return bytes;
}
static void linked_test(const char *first_path,const char *peer_path)
{
    Memory m={0}; LbSystemAllocator callbacks={&m,allocate,release}; LbEngine *engine=NULL;
    LbCodeSpace *space=NULL; LbCodeModule *first=NULL,*peer=NULL; LbBeamError error;
    size_t first_size,peer_size,i,count,live,bytes,atoms;
    unsigned char *first_bytes=read_image(first_path,&first_size),*peer_bytes=read_image(peer_path,&peer_size);
    LbProcess *p; LbProcessStatus status; Eterm result,arg;
    CHECK(lb_engine_create(&callbacks,&engine)==LB_ENGINE_OK && lb_code_space_create(engine,&space)==LB_CODE_OK);
    live=m.live; bytes=m.bytes; atoms=lb_atoms_count(lb_code_space_atoms(space));
    CHECK(lb_code_load(space,peer_bytes,peer_size,&peer,&error)==LB_CODE_UNRESOLVED && peer==NULL);
    CHECK(m.live==live && m.bytes==bytes && lb_atoms_count(lb_code_space_atoms(space))==atoms);
    CHECK(lb_code_load(space,first_bytes,first_size,&first,&error)==LB_CODE_OK);
    { LbCodeStatus s=lb_code_load(space,peer_bytes,peer_size,&peer,&error);
      if(s!=LB_CODE_OK) fprintf(stderr,"peer load=%d stage=%s offset=%zu\n",s,error.stage,error.offset);
      CHECK(s==LB_CODE_OK); }
    free(first_bytes); free(peer_bytes);
    CHECK(lb_code_unload(first)==LB_CODE_BUSY); /* actual import references */
    p=start(space,peer,"forward",NULL,0); run(p,LB_PROCESS_DONE); result=lb_process_result(p);
    CHECK(is_tuple(result) && is_literal_ptr(result) && list_length(tuple_val(result)[2])==3);
    CHECK(lb_code_unload(first)==LB_CODE_BUSY && lb_code_unload(peer)==LB_CODE_BUSY); lb_process_destroy(p);
    p=start(space,peer,"local",NULL,0); status=LB_PROCESS_YIELDED;
    for(i=0;i<100 && status==LB_PROCESS_YIELDED;++i) {
        status=lb_process_run(p,1,100);
        CHECK(status==LB_PROCESS_YIELDED || status==LB_PROCESS_DONE);
        /* Every dispatch safepoint, including after test_heap and before tuple
         * construction: reserved heap space and all physical roots survive. */
        CHECK(lb_process_collect(p,0)==status);
    }
    CHECK(status==LB_PROCESS_DONE); result=lb_process_result(p);
    CHECK(is_tuple(result) && arityval(*tuple_val(result))==64);
    for(i=1;i<=64;++i) CHECK(tuple_val(result)[i]==atom(space,"ok"));
    lb_process_destroy(p);
    for(i=0;i<2;++i) {
        Eterm args[]={make_small(7),make_small((Sint)i)};
        p=start(space,peer,"branch",args,2); run(p,LB_PROCESS_DONE); result=lb_process_result(p);
        CHECK(is_tuple(result) && tuple_val(result)[1]==args[0] && tuple_val(result)[2]==atom(space,i?"nonzero":"zero"));
        lb_process_destroy(p);
    }
    arg=atom(space,"attributes"); p=start(space,peer,"module_info",&arg,1); run(p,LB_PROCESS_DONE);
    CHECK(lb_process_collect(p,0)==LB_PROCESS_DONE); result=lb_process_result(p);
    while(is_list(result)) {
        Eterm pair=CAR(list_val(result));
        if(tuple_val(pair)[1]==atom(space,"stress_attribute")) {
            Eterm data=CAR(list_val(tuple_val(pair)[2])),*fields=tuple_val(data);
            CHECK(arityval(fields[0])==5 && is_nil(fields[1]));
            CHECK(is_tuple(fields[2]) && arityval(*tuple_val(fields[2]))==0);
            CHECK((boxed_val(fields[3])[0]&_HEADER_SUBTAG_MASK)==FLOAT_SUBTAG);
            CHECK((boxed_val(fields[4])[0]&_HEADER_SUBTAG_MASK)==HEAP_BITS_SUBTAG && boxed_val(fields[4])[1]==3);
            CHECK((boxed_val(fields[5])[0]&_HEADER_SUBTAG_MASK)==POS_BIG_SUBTAG); break;
        }
        result=CDR(list_val(result));
    }
    CHECK(is_list(result)); lb_process_destroy(p);
    /* Every process-construction allocation and failed-collection retry. */
    { LbCodeEntry *entry; size_t before_live=m.live,before_bytes=m.bytes;
      CHECK(lb_code_entry_acquire(space,lb_code_module_name(peer),atom(space,"local"),0,&entry)==LB_CODE_OK);
      for(i=1;i<=2;++i) {
        live=m.live; bytes=m.bytes; m.fail=m.calls+i;
        CHECK(lb_process_create(entry,NULL,0,5,&p)==LB_PROCESS_NO_MEMORY && !p);
        CHECK(m.live==live && m.bytes==bytes); m.fail=0;
      }
      lb_code_entry_release(entry); CHECK(m.live==before_live && m.bytes==before_bytes);
    }
    p=start(space,peer,"module_info",NULL,0); count=m.calls; run(p,LB_PROCESS_DONE); count=m.calls-count;
    result=lb_process_result(p); live=m.live; bytes=m.bytes; m.fail=m.calls+1;
    CHECK(lb_process_collect(p,100)==LB_PROCESS_NO_MEMORY && lb_process_result(p)==result && m.live==live && m.bytes==bytes);
    m.fail=0; CHECK(lb_process_collect(p,100)==LB_PROCESS_DONE); lb_process_destroy(p);
    for(i=1;i<=count;++i) {
        p=start(space,peer,"module_info",NULL,0); m.fail=m.calls+i;
        run(p,LB_PROCESS_NO_MEMORY); m.fail=0; lb_process_destroy(p);
    }
    p=start(space,peer,"loop",NULL,0);
    lb_engine_release(engine); engine=NULL; /* execution retains its actual parent */
    CHECK(lb_process_run(p,100,10)==LB_PROCESS_YIELDED);
    CHECK(lb_process_collect(p,0)==LB_PROCESS_YIELDED);
    CHECK(lb_process_run(p,100,10)==LB_PROCESS_YIELDED);
    CHECK(lb_code_unload(peer)==LB_CODE_BUSY); lb_process_destroy(p);
    p=start(space,peer,"module_info",NULL,0); run(p,LB_PROCESS_DONE); lb_process_destroy(p);
    CHECK(lb_code_unload(peer)==LB_CODE_OK); /* includes self imports: no cycle/UAF */
    CHECK(lb_code_unload(first)==LB_CODE_OK && lb_code_space_destroy(space)==LB_CODE_OK);
    CHECK(!m.live && !m.bytes); /* last physical space release also reclaimed the Engine */
    puts("CORE_LINKED_CODE_OK imports_retain_code=true self_import_retirement=true bounded_loop=true heap_safepoints=true runtime_failure_retry=true engine_owner_drop=true");
}
static void many_test(const char *path)
{
    Memory m={0}; LbSystemAllocator cb={&m,allocate,release}; LbEngine *engine=NULL;
    LbCodeSpace *space=NULL; LbCodeModule *module=NULL; LbBeamError error;
    size_t size,calls,i,live,bytes,atoms,words; unsigned char *data=read_image(path,&size);
    CHECK(lb_engine_create(&cb,&engine)==LB_ENGINE_OK && lb_code_space_create(engine,&space)==LB_CODE_OK);
    calls=m.calls; CHECK(lb_code_load(space,data,size,&module,&error)==LB_CODE_OK); calls=m.calls-calls;
    CHECK(lb_code_module_function_count(module)==130 && lb_code_module_words(module,&words) && words>1024);
    for(i=0;i<128;++i) {
        char name[20]; LbProcess *p;
        CHECK(snprintf(name,sizeof(name),"f%zu",i)>0); p=start(space,module,name,NULL,0); run(p,LB_PROCESS_DONE);
        CHECK(lb_process_result(p)==make_small((Sint)i)); lb_process_destroy(p);
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
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine);
    CHECK(!m.live && !m.bytes); free(data);
    printf("CORE_CODE_GROWTH_OK functions=130 words=%zu failure_prefixes=%zu\n",words,calls);
}
static void reject_test(const char *path)
{
    Memory m={0}; LbSystemAllocator cb={&m,allocate,release}; LbEngine *engine=NULL;
    LbCodeSpace *space=NULL; LbCodeModule *module=NULL; LbBeamError error;
    size_t size,live,bytes,atoms; unsigned char *data=read_image(path,&size); LbCodeStatus status;
    CHECK(lb_engine_create(&cb,&engine)==LB_ENGINE_OK && lb_code_space_create(engine,&space)==LB_CODE_OK);
    live=m.live; bytes=m.bytes; atoms=lb_atoms_count(lb_code_space_atoms(space));
    status=lb_code_load(space,data,size,&module,&error);
    CHECK(status==LB_CODE_UNRESOLVED || status==LB_CODE_UNSUPPORTED);
    CHECK(!module && m.live==live && m.bytes==bytes && lb_atoms_count(lb_code_space_atoms(space))==atoms);
    CHECK(lb_code_space_destroy(space)==LB_CODE_OK); lb_engine_release(engine);
    CHECK(!m.live && !m.bytes);
    free(data); puts("CORE_CODE_REJECTION_OK unsupported_imports_or_profile=true atom_rollback=true");
}
static void component_test(void)
{
    LbEngine *engine=NULL; LbCodeSpace *space=NULL; LbAtomTransaction *tx=NULL; LbBeamBytes names[2],view;
    Eterm terms[2],found; size_t count; unsigned char digest[16];
    CHECK(lb_engine_create(NULL,&engine)==LB_ENGINE_OK && lb_code_space_create(engine,&space)==LB_CODE_OK);
    count=lb_atoms_count(lb_code_space_atoms(space));
    names[0]=(LbBeamBytes){(const unsigned char *)"provisional-one",15}; names[1]=names[0];
    CHECK(lb_atoms_prepare_names(lb_code_space_atoms(space),names,2,terms,&tx)==LB_ATOM_OK && terms[0]==terms[1]);
    CHECK(lb_atoms_count(lb_code_space_atoms(space))==count);
    CHECK(lb_atoms_find(lb_code_space_atoms(space),names[0].data,names[0].size,&found)==LB_ATOM_NOT_FOUND);
    CHECK(lb_atoms_transaction_name(tx,terms[0],&view)==LB_ATOM_OK && view.size==15);
    CHECK(lb_code_space_destroy(space)==LB_CODE_BUSY);
    CHECK(lb_atoms_intern(lb_code_space_atoms(space),"other",5,&found)==LB_ATOM_BUSY);
    lb_atoms_abort(tx);
    CHECK(lb_atoms_prepare_names(lb_code_space_atoms(space),names,2,terms,&tx)==LB_ATOM_OK); lb_atoms_commit(tx);
    CHECK(lb_atoms_count(lb_code_space_atoms(space))==count+1);
    CHECK(lb_atoms_find(lb_code_space_atoms(space),names[0].data,names[0].size,&found)==LB_ATOM_OK && found==terms[0]);
    erts_md5((const unsigned char *)"abc",3,digest);
    CHECK(!memcmp(digest,"\x90\x01\x50\x98\x3c\xd2\x4f\xb0\xd6\x96\x3f\x7d\x28\xe1\x7f\x72",16));
    CHECK(lb_code_space_destroy(space)==LB_CODE_OK && lb_engine_shutdown(engine)==LB_ENGINE_OK);
    lb_engine_release(engine);
    puts("CORE_CODE_COMPONENT_OK provisional_atoms=true md5=true execution_not_tested_without_fixture=true");
}
int main(int argc,char **argv)
{
    component_test();
    if(argc==3 && !strcmp(argv[1],"--many")) { many_test(argv[2]); return 0; }
    if(argc==3 && !strcmp(argv[1],"--reject")) { reject_test(argv[2]); return 0; }
    if(argc>=2) file_test(argv[1]);
    if(argc==3) linked_test(argv[1],argv[2]);
    CHECK(argc>=1 && argc<=3); return 0;
}
