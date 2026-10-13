/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#define _GNU_SOURCE
#define _DARWIN_C_SOURCE
#include "process_internal.h"
#include "big.h"
#include <fenv.h>
#include <dlfcn.h>
#include <stdio.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"CHECK %s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while(0)
static int environment_failure;
int fegetenv(fenv_t *environment)
{
    int (*real_get)(fenv_t *); void *symbol=dlsym(RTLD_NEXT,"fegetenv");
    CHECK(symbol && sizeof(symbol)==sizeof(real_get)); memcpy(&real_get,&symbol,sizeof(real_get));
    if(environment_failure==1) { environment_failure=0; return 1; }
    return real_get(environment);
}
int fesetenv(const fenv_t *environment)
{
    int (*real_set)(const fenv_t *); void *symbol=dlsym(RTLD_NEXT,"fesetenv");
    CHECK(symbol && sizeof(symbol)==sizeof(real_set)); memcpy(&real_set,&symbol,sizeof(real_set));
    if((environment_failure==2 && environment==FE_DFL_ENV) || (environment_failure==3 && environment!=FE_DFL_ENV)) {
        environment_failure=0; return 1;
    }
    return real_set(environment);
}
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
static Eterm atom(LbCodeSpace *space,const char *name)
{
    Eterm value; CHECK(lb_atoms_find(space->atoms,name,strlen(name),&value)==LB_ATOM_OK); return value;
}
static LbProcess *start(LbCodeSpace *space,LbCodeModule *module,const char *function,size_t arity,size_t words)
{
    LbCodeEntry *entry; LbProcess *p; Eterm args[2]={NIL,NIL};
    CHECK(lb_code_entry_acquire(space,module->name,atom(space,function),(unsigned)arity,&entry)==LB_CODE_OK);
    CHECK(lb_process_create(entry,args,arity,words,&p)==LB_PROCESS_READY); lb_code_entry_release(entry); return p;
}
static LbProcessStatus run(LbProcess *p,int collect)
{
    size_t i;
    for(i=0;i<1000 && (p->status==LB_PROCESS_READY || p->status==LB_PROCESS_YIELDED);++i) {
        lb_process_run(p,1,4);
        if(collect && p->status==LB_PROCESS_YIELDED) CHECK(lb_process_collect(p,0)==LB_PROCESS_YIELDED);
    }
    CHECK(i<1000); return p->status;
}
static void binary(Eterm value,const char *text)
{
    LbBitstringView view;
    CHECK(lb_bitstring_view(value,&view) && !view.bit_offset && view.bit_size==strlen(text)*8 && !memcmp(view.data,text,strlen(text)));
}
static Eterm big(LbProcess *p,Uint digit,int negative)
{
    Eterm *hp=p->htop;
    CHECK(p->stop-p->htop>=2+S_RESERVED);
    hp[0]=_make_header(1,negative?NEG_BIG_SUBTAG:POS_BIG_SUBTAG); hp[1]=digit; p->htop+=2; return make_boxed(hp);
}
static Eterm floating(LbProcess *p,Uint bits)
{
    Eterm *hp=p->htop;
    CHECK(p->stop-p->htop>=2+S_RESERVED);
    hp[0]=_make_header(1,FLOAT_SUBTAG); hp[1]=bits; p->htop+=2; return make_boxed(hp);
}
static void semantics(LbCodeSpace *space,LbCodeModule *module)
{
    LbProcess *p; Eterm value; size_t i;
    p=start(space,module,"add",2,5); p->reg[0]=make_small(40); p->reg[1]=make_small(2);
    CHECK(run(p,1)==LB_PROCESS_DONE && lb_process_result(p)==make_small(42)); lb_process_destroy(p);
    for(i=0;i<2;++i) {
        p=start(space,module,i?"decrement":"increment",1,5); p->reg[0]=make_small(i?MIN_SMALL:MAX_SMALL);
        CHECK(run(p,1)==LB_PROCESS_DONE); value=lb_process_result(p);
        CHECK(lb_is_big(value) && lb_big_size(value)==1 && boxed_val(value)[1]==(Uint)MAX_SMALL+(i?2:1));
        CHECK(((*boxed_val(value)&_TAG_HEADER_MASK)==NEG_BIG_SUBTAG)==(int)i); lb_process_destroy(p);
    }
    p=start(space,module,"add",2,7); p->reg[0]=big(p,UINTPTR_MAX,0); p->reg[1]=make_small(1);
    CHECK(run(p,1)==LB_PROCESS_DONE); value=lb_process_result(p);
    CHECK(lb_is_big(value) && lb_big_size(value)==2 && boxed_val(value)[1]==0 && boxed_val(value)[2]==1); lb_process_destroy(p);
    p=start(space,module,"add",2,10); p->reg[0]=big(p,UINTPTR_MAX,0); p->reg[1]=make_small(1);
    p->heap_reserved=3; /* physically available credit for a subsequent tuple */
    CHECK(lb_process_run(p,1,4)==LB_PROCESS_YIELDED && p->collections>0 && p->heap_reserved==3);
    CHECK((size_t)(p->stop-p->htop)>=p->heap_reserved+S_RESERVED);
    CHECK(run(p,1)==LB_PROCESS_DONE); lb_process_destroy(p);
    p=start(space,module,"add",2,9); p->reg[0]=big(p,UINTPTR_MAX,0); p->reg[1]=big(p,UINTPTR_MAX,1);
    CHECK(run(p,1)==LB_PROCESS_DONE && lb_process_result(p)==make_small(0)); lb_process_destroy(p);
    for(i=0;i<3;++i) {
        p=start(space,module,"guarded",2,5); p->reg[0]=i==2?NIL:make_small(40); p->reg[1]=make_small(i?1:2);
        CHECK(run(p,1)==LB_PROCESS_DONE); binary(lb_process_result(p),i?"no":"yes"); lb_process_destroy(p);
    }
    p=start(space,module,"add",2,5); p->reg[1]=make_small(1);
    CHECK(run(p,0)==LB_PROCESS_EXCEPTION && lb_process_exception(p)==am_badarith); lb_process_destroy(p);
    p=start(space,module,"big",1,5); CHECK(run(p,1)==LB_PROCESS_DONE); value=lb_process_result(p);
    CHECK(lb_is_big(value) && boxed_val(value)[1]==UINT64_C(2305843009213693950)); lb_process_destroy(p);
}
static void saved_destination_gc(LbCodeSpace *space,LbCodeModule *module)
{
    LbProcess *p=start(space,module,"saved",2,7); size_t i,before; Eterm value;
    p->reg[0]=big(p,UINTPTR_MAX,0); p->reg[1]=make_small(1);
    for(i=0;i<1000 && *p->i!=op_i_plus_yyjd;++i) CHECK(lb_process_run(p,1,4)==LB_PROCESS_YIELDED);
    CHECK(i<1000);
    /* Valid unreachable heap words create pressure at this exact arithmetic
     * safepoint, rather than replacing its allocator or computed result. */
    while(p->stop-p->htop>S_RESERVED) *p->htop++=NIL;
    before=p->collections;
    CHECK(lb_process_run(p,1,4)==LB_PROCESS_YIELDED && p->collections>before);
    CHECK(run(p,1)==LB_PROCESS_DONE); value=lb_process_result(p);
    CHECK(lb_is_big(value) && lb_big_size(value)==2 && boxed_val(value)[1]==0 && boxed_val(value)[2]==1);
    lb_process_destroy(p);
}
static void floats(LbCodeSpace *space,LbCodeModule *module)
{
    fenv_t saved; LbProcess *p; Eterm value; size_t i;
    CHECK(!fegetenv(&saved) && !fesetenv(FE_DFL_ENV) && !fesetround(FE_UPWARD));
    CHECK(!feraiseexcept(FE_DIVBYZERO));
    p=start(space,module,"float",1,5); CHECK(run(p,1)==LB_PROCESS_DONE); value=lb_process_result(p);
    CHECK((*boxed_val(value)&_TAG_HEADER_MASK)==FLOAT_SUBTAG && boxed_val(value)[1]==UINT64_C(0x43b0000000000001));
    CHECK(fegetround()==FE_UPWARD && (fetestexcept(FE_DIVBYZERO)&FE_DIVBYZERO)); lb_process_destroy(p);
    p=start(space,module,"add",2,9);
    p->reg[0]=floating(p,UINT64_C(0x4330000000000000)); p->reg[1]=floating(p,UINT64_C(0x3fe0000000000000));
    CHECK(run(p,1)==LB_PROCESS_DONE && boxed_val(lb_process_result(p))[1]==UINT64_C(0x4330000000000000)); lb_process_destroy(p);
    for(i=0;i<2;++i) {
        p=start(space,module,"add",2,9);
        p->reg[0]=floating(p,UINT64_C(0x8000000000000000)); p->reg[1]=floating(p,i?UINT64_C(0x8000000000000000):0);
        CHECK(run(p,1)==LB_PROCESS_DONE && boxed_val(lb_process_result(p))[1]==(i?UINT64_C(0x8000000000000000):0)); lb_process_destroy(p);
    }
    p=start(space,module,"add",2,9);
    p->reg[0]=floating(p,UINT64_C(0x7fefffffffffffff)); p->reg[1]=floating(p,UINT64_C(0x7fefffffffffffff));
    CHECK(run(p,0)==LB_PROCESS_EXCEPTION && lb_process_exception(p)==am_badarith); lb_process_destroy(p);
    CHECK(fegetround()==FE_UPWARD && !fesetenv(&saved));
}
static void exceptional_values(LbCodeSpace *space,LbCodeModule *module)
{
    size_t i,f; LbProcess *p;
    for(f=0;f<2;++f) {
        p=start(space,module,f?"guarded":"add",2,LB_BIG_ARITY_MAX+6);
        p->reg[0]=make_boxed(p->htop); *p->htop++=_make_header(LB_BIG_ARITY_MAX,POS_BIG_SUBTAG);
        for(i=0;i<LB_BIG_ARITY_MAX;++i) *p->htop++=UINTPTR_MAX;
        p->reg[1]=make_small(1);
        CHECK(run(p,0)==(f?LB_PROCESS_DONE:LB_PROCESS_EXCEPTION));
        if(f) binary(lb_process_result(p),"no"); else CHECK(lb_process_exception(p)==am_system_limit);
        lb_process_destroy(p);
    }
    p=start(space,module,"add",2,64);
    p->reg[0]=make_boxed(p->htop); *p->htop++=_make_header(17,POS_BIG_SUBTAG);
    for(i=0;i<16;++i) *p->htop++=0;
    *p->htop++=1; p->reg[1]=floating(p,0);
    CHECK(run(p,0)==LB_PROCESS_EXCEPTION && lb_process_exception(p)==am_badarith); lb_process_destroy(p);
    for(i=1;i<=3;++i) {
        fenv_t saved;
        CHECK(!fegetenv(&saved));
        p=start(space,module,"guarded",2,16); p->reg[0]=floating(p,0); p->reg[1]=make_small(42);
        environment_failure=(int)i;
        CHECK(run(p,0)==LB_PROCESS_ENVIRONMENT && !environment_failure);
        CHECK(lb_process_result(p)==THE_NON_VALUE && p->exception_value==NIL);
        CHECK(!fesetenv(&saved)); lb_process_destroy(p);
    }
}
static void execution_failures(LbCodeSpace *space,LbCodeModule *module,Memory *memory)
{
    size_t i,f,live,bytes; Eterm root; Eterm *heap;
    for(f=0;f<2;++f) for(i=1;i<=2;++i) {
        LbProcess *p=start(space,module,f?"guarded":"add",2,7);
        p->reg[0]=big(p,UINTPTR_MAX,0); p->reg[1]=make_small(1);
        live=memory->live; bytes=memory->bytes; root=p->reg[0]; heap=p->heap;
        memory->fail=memory->calls+i;
        CHECK(run(p,0)==LB_PROCESS_NO_MEMORY && lb_process_result(p)==THE_NON_VALUE);
        CHECK(memory->live==live && memory->bytes==bytes && p->reg[0]==root && p->heap==heap);
        memory->fail=0; lb_process_destroy(p);
        p=start(space,module,f?"guarded":"add",2,7); p->reg[0]=big(p,UINTPTR_MAX,0); p->reg[1]=make_small(1);
        CHECK(run(p,0)==LB_PROCESS_DONE); lb_process_destroy(p);
    }
}
static uint32_t be32(const unsigned char *p)
{ return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }
static size_t code_base(const unsigned char *data,size_t size)
{
    size_t pos;
    for(pos=12;pos+8<=size;pos+=8+((be32(data+pos+4)+3u)&~3u))
        if(!memcmp(data+pos,"Code",4)) return pos+8+4+be32(data+pos+8);
    CHECK(0); return 0;
}
static void native_calls(LbEngine *engine,Memory *memory,unsigned char *data,size_t size)
{
    LbCodeSpace *space; LbBeamProgram *program; LbBeamError error; const LbBeamOp *op;
    size_t offsets[8],n=0,i,base=code_base(data,size); unsigned char saved[8]; LbCodeModule *module;
    CHECK(lb_code_space_create(engine,&space)==LB_CODE_OK);
    CHECK(lb_beam_program_prepare(engine->domain,space->atoms,data,size,&program,&error)==LB_BEAM_OK);
    for(op=lb_beam_program_ops(program);op;op=op->next) {
        LbBeamImport import;
        if((op->op!=genop_call_ext_2 && op->op!=genop_call_ext_only_2) || op->a[0].val!=2) continue;
        CHECK(lb_beam_image_import(lb_beam_program_image(program),(uint32_t)op->a[1].val,&import)==LB_BEAM_OK);
        if(import.module!=1) continue;
        CHECK(n<8 && op->a[1].val<16);
        offsets[n]=base+op->offset+2; saved[n]=data[offsets[n]];
        CHECK(saved[n]==(unsigned char)(op->a[1].val<<4)); data[offsets[n++]]=0; /* existing +/2 import */
    }
    CHECK(n==4); lb_beam_program_destroy(program);
    CHECK(lb_code_load(space,data,size,&module,&error)==LB_CODE_OK);
    for(i=0;i<2;++i) {
        LbProcess *p=start(space,module,i?"float":"big",1,5);
        CHECK(run(p,1)==LB_PROCESS_DONE && p->current->module==am_erlang && p->current->function==am_Plus);
        CHECK(boxed_val(lb_process_result(p))[1]==(i?UINT64_C(0x43b0000000000001):UINT64_C(2305843009213693950)));
        lb_process_destroy(p);
    }
    for(i=0;i<3;++i) {
        LbProcess *p=start(space,module,"external",2,7);
        p->reg[0]=big(p,UINTPTR_MAX,0); p->reg[1]=make_small(1);
        size_t steps;
        for(steps=0;steps<100 && *p->i!=op_call_bif_W;++steps) CHECK(lb_process_run(p,1,4)==LB_PROCESS_YIELDED);
        CHECK(steps<100);
        while(p->stop-p->htop>S_RESERVED) *p->htop++=NIL;
        memory->fail=i?memory->calls+i:0;
        CHECK(run(p,0)==(i?LB_PROCESS_NO_MEMORY:LB_PROCESS_DONE)); memory->fail=0;
        CHECK(p->current->module==am_erlang && p->current->function==am_Plus); lb_process_destroy(p);
    }
    { LbProcess *p=start(space,module,"external",2,5);
      CHECK(run(p,0)==LB_PROCESS_EXCEPTION && lb_process_exception(p)==am_badarith); lb_process_destroy(p); }
    CHECK(lb_code_unload(module)==LB_CODE_OK && lb_code_space_destroy(space)==LB_CODE_OK);
    for(i=0;i<n;++i) data[offsets[i]]=saved[i];
}
static void malformed(LbEngine *engine,unsigned char *data,size_t size)
{
    LbCodeSpace *space; LbBeamProgram *program; LbBeamError error; const LbBeamOp *op;
    size_t base=code_base(data,size),index=SIZE_MAX,i; const unsigned offsets[]={2,4,6,1,3};
    const unsigned char replacements[]={0x30,0x23,0x04,0x35,0x10};
    CHECK(base && lb_code_space_create(engine,&space)==LB_CODE_OK);
    CHECK(lb_beam_program_prepare(engine->domain,space->atoms,data,size,&program,&error)==LB_BEAM_OK);
    for(op=lb_beam_program_ops(program);op;op=op->next)
        if(op->op==genop_gc_bif2_6) { index=base+op->offset; break; }
    CHECK(index<size-7 && data[index+1]==5 && data[index+2]==0x20 && data[index+3]==0 && data[index+4]==3 && data[index+6]==3);
    lb_beam_program_destroy(program); CHECK(lb_code_space_destroy(space)==LB_CODE_OK);
    for(i=0;i<5;++i) {
        LbCodeModule *module; unsigned char saved=data[index+offsets[i]];
        CHECK(lb_code_space_create(engine,&space)==LB_CODE_OK); data[index+offsets[i]]=replacements[i];
        CHECK(lb_code_load(space,data,size,&module,&error)==(i==4?LB_CODE_UNSUPPORTED:LB_CODE_FORMAT) && !module);
        data[index+offsets[i]]=saved;
        CHECK(lb_code_load(space,data,size,&module,&error)==LB_CODE_OK);
        CHECK(lb_code_unload(module)==LB_CODE_OK && lb_code_space_destroy(space)==LB_CODE_OK);
    }
}
static void load_failures(LbEngine *engine,Memory *memory,const unsigned char *data,size_t size)
{
    LbCodeSpace *space; LbCodeModule *module; LbBeamError error; size_t i,calls,live,bytes;
    CHECK(lb_code_space_create(engine,&space)==LB_CODE_OK); calls=memory->calls;
    CHECK(lb_code_load(space,data,size,&module,&error)==LB_CODE_OK); calls=memory->calls-calls;
    CHECK(lb_code_unload(module)==LB_CODE_OK && lb_code_space_destroy(space)==LB_CODE_OK);
    for(i=1;i<=calls;++i) {
        CHECK(lb_code_space_create(engine,&space)==LB_CODE_OK); live=memory->live; bytes=memory->bytes;
        memory->fail=memory->calls+i;
        CHECK(lb_code_load(space,data,size,&module,&error)==LB_CODE_NO_MEMORY && !module);
        CHECK(memory->live==live && memory->bytes==bytes); memory->fail=0;
        CHECK(lb_code_load(space,data,size,&module,&error)==LB_CODE_OK);
        CHECK(lb_code_unload(module)==LB_CODE_OK && lb_code_space_destroy(space)==LB_CODE_OK);
    }
    printf("CORE_ADD_LOAD_OK failure_prefixes=%zu retry_each=true\n",calls);
}
int main(int argc,char **argv)
{
    Memory memory={0}; LbSystemAllocator allocator={&memory,allocate,release};
    LbEngine *engine; LbCodeSpace *space; LbCodeModule *module; LbBeamError error;
    FILE *file; long length; unsigned char *data;
    CHECK(argc==2 && (file=fopen(argv[1],"rb")) && !fseek(file,0,SEEK_END)); length=ftell(file);
    CHECK(length>0 && length<8*1024*1024 && !fseek(file,0,SEEK_SET));
    data=malloc((size_t)length); CHECK(data && fread(data,1,(size_t)length,file)==(size_t)length && !fclose(file));
    CHECK(lb_engine_create(&allocator,&engine)==LB_ENGINE_OK && lb_code_space_create(engine,&space)==LB_CODE_OK);
    { LbCodeStatus status=lb_code_load(space,data,(size_t)length,&module,&error);
      if(status!=LB_CODE_OK) fprintf(stderr,"load status=%d stage=%s offset=%zu\n",status,error.stage,error.offset);
      CHECK(status==LB_CODE_OK); }
    semantics(space,module); saved_destination_gc(space,module); floats(space,module); exceptional_values(space,module);
    execution_failures(space,module,&memory);
    CHECK(lb_code_unload(module)==LB_CODE_OK && lb_code_space_destroy(space)==LB_CODE_OK);
    native_calls(engine,&memory,data,(size_t)length);
    malformed(engine,data,(size_t)length); load_failures(engine,&memory,data,(size_t)length);
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine); free(data);
    CHECK(!memory.live && !memory.bytes);
    puts("CORE_ADD_EXECUTION_OK native_plus_increment=true native_bif=true big_carry=true small_normalization=true nearest_float=true fenv_restored=true y_destination_gc=true trim=true forced_gc=true oom_not_guard_false=true");
    return 0;
}
