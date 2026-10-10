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
static Eterm atom(LbCodeSpace *space,const char *name)
{
    Eterm term; CHECK(lb_atoms_find(space->atoms,name,strlen(name),&term)==LB_ATOM_OK); return term;
}
static LbProcess *start(LbCodeSpace *space,LbCodeModule *module,const char *function)
{
    unsigned char input[65]; LbCodeEntry *entry; LbProcess *process;
    memset(input,0x97,sizeof(input));
    CHECK(lb_code_entry_acquire(space,module->name,atom(space,function),1,&entry)==LB_CODE_OK);
    CHECK(lb_process_create_binary(entry,input,sizeof(input),5,&process)==LB_PROCESS_READY);
    lb_code_entry_release(entry); CHECK(lb_code_unload(module)==LB_CODE_BUSY); return process;
}
static void binary(Eterm value)
{
    LbBitstringView view; size_t i;
    CHECK(lb_bitstring_view(value,&view) && !view.bit_offset && view.bit_size==65*8);
    for(i=0;i<65;++i) CHECK(view.data[i]==0x97);
}
static void semantics(LbCodeSpace *space,LbCodeModule *module)
{
    const char *functions[]={"identity","gc","tail","pair"}; size_t f,i;
    for(f=0;f<sizeof(functions)/sizeof(functions[0]);++f) {
        LbProcess *p=start(space,module,functions[f]);
        LbProcessStatus status=LB_PROCESS_READY; size_t before=lb_process_collections(p);
        for(i=0;i<1000 && (status==LB_PROCESS_READY || status==LB_PROCESS_YIELDED);++i) {
            status=lb_process_run(p,1,4);
            if(status==LB_PROCESS_YIELDED) CHECK(lb_process_collect(p,0)==LB_PROCESS_YIELDED);
        }
        CHECK(status==LB_PROCESS_DONE && lb_process_collections(p)>before);
        if(f==3) {
            Eterm value=lb_process_result(p); CHECK(is_tuple(value) && arityval(*tuple_val(value))==2);
            binary(tuple_val(value)[1]); binary(tuple_val(value)[2]);
        } else binary(lb_process_result(p));
        lb_process_destroy(p);
    }
    {
        LbProcess *p=start(space,module,"loop"); int private_yield=0;
        for(i=0;i<32;++i) {
            CHECK(lb_process_run(p,1000,4)==LB_PROCESS_YIELDED);
            if(p->current==&p->local_mfa) {
                CHECK(p->current->function==atom(space,"again") && p->current->arity==1);
                private_yield=1;
            }
            CHECK(lb_process_collect(p,0)==LB_PROCESS_YIELDED);
        }
        CHECK(private_yield); lb_process_destroy(p);
    }
}
static void suspended_frame_failure(LbCodeSpace *space,LbCodeModule *module,Memory *memory)
{
    LbProcess *p=start(space,module,"gc"); size_t i; Eterm *old_heap,saved;
    for(i=0;i<100;++i) {
        LbBitstringView view;
        CHECK(lb_process_run(p,1,100)==LB_PROCESS_YIELDED);
        if(p->hend-p->stop>2 && p->stop[0]!=NIL && lb_bitstring_view(p->stop[1],&view)) break;
    }
    CHECK(i<100); old_heap=p->heap; saved=p->stop[1]; memory->fail=memory->calls+1;
    CHECK(lb_process_collect(p,100)==LB_PROCESS_NO_MEMORY);
    CHECK(p->heap==old_heap && p->stop[1]==saved && p->status==LB_PROCESS_YIELDED);
    memory->fail=0; CHECK(lb_process_collect(p,100)==LB_PROCESS_YIELDED);
    for(i=0;i<100 && p->status==LB_PROCESS_YIELDED;++i) lb_process_run(p,1,4);
    CHECK(p->status==LB_PROCESS_DONE); binary(lb_process_result(p)); lb_process_destroy(p);
}
static uint32_t be32(const unsigned char *p)
{ return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }
static size_t code_offset(const unsigned char *data,size_t size)
{
    size_t pos;
    for(pos=12;pos+8<=size;pos+=8+((be32(data+pos+4)+3u)&~3u))
        if(!memcmp(data+pos,"Code",4)) return pos+8+4+be32(data+pos+8);
    CHECK(0); return 0;
}
static void malformed(LbEngine *engine,unsigned char *data,size_t size)
{
    LbCodeSpace *space; LbBeamProgram *program; LbBeamError error; const LbBeamOp *op;
    size_t only=SIZE_MAX,last=SIZE_MAX,base=code_offset(data,size),i;
    CHECK(lb_code_space_create(engine,&space)==LB_CODE_OK);
    CHECK(lb_beam_program_prepare(engine->domain,space->atoms,data,size,&program,&error)==LB_BEAM_OK);
    for(op=lb_beam_program_ops(program);op;op=op->next) {
        if(op->op==genop_call_only_2 && only==SIZE_MAX) only=base+op->offset;
        if(op->op==genop_call_last_3 && last==SIZE_MAX) last=base+op->offset;
    }
    CHECK(only<size-3 && last<size-4);
    CHECK(data[only+1]==0x10 && data[only+2]==0x45 && data[last+3]==0x10);
    lb_beam_program_destroy(program); CHECK(lb_code_space_destroy(space)==LB_CODE_OK);
    for(i=0;i<4;++i) {
        size_t where=i==0?only+1:i==1?only+2:i==2?only:last+3;
        unsigned char saved=data[where],replacement=i==0?0:i==1?0x35:i==2?genop_call_2:0x20;
        LbCodeModule *module=NULL;
        CHECK(lb_code_space_create(engine,&space)==LB_CODE_OK); data[where]=replacement;
        CHECK(lb_code_load(space,data,size,&module,&error)==LB_CODE_FORMAT && !module);
        CHECK(!strcmp(error.stage,"register/stack/heap admission")); data[where]=saved;
        CHECK(lb_code_load(space,data,size,&module,&error)==LB_CODE_OK);
        CHECK(lb_code_unload(module)==LB_CODE_OK && lb_code_space_destroy(space)==LB_CODE_OK);
    }
}
static void failures(LbEngine *engine,Memory *memory,const unsigned char *data,size_t size)
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
    printf("CORE_LOCAL_FAILURES_OK load_prefixes=%zu retry_each=true invalid_call_targets=true\n",calls);
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
    semantics(space,module); suspended_frame_failure(space,module,&memory);
    CHECK(lb_code_unload(module)==LB_CODE_OK && lb_code_space_destroy(space)==LB_CODE_OK);
    malformed(engine,data,(size_t)length); failures(engine,&memory,data,(size_t)length);
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine); free(data);
    CHECK(!memory.live && !memory.bytes);
    puts("CORE_LOCAL_EXECUTION_OK local_calls=true private_entry_yields=true y_roots=true forced_gc_each_dispatch=true suspended_frame_oom_retry=true exact_retirement=true");
    return 0;
}
