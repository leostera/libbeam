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
    Eterm value; CHECK(lb_atoms_find(space->atoms,name,strlen(name),&value)==LB_ATOM_OK); return value;
}
static LbProcess *start(LbCodeSpace *space,LbCodeModule *module,const char *function)
{
    LbCodeEntry *entry; LbProcess *p; Eterm argument=NIL;
    CHECK(lb_code_entry_acquire(space,module->name,atom(space,function),1,&entry)==LB_CODE_OK);
    CHECK(lb_process_create(entry,&argument,1,64,&p)==LB_PROCESS_READY); lb_code_entry_release(entry); return p;
}
static Eterm run(LbProcess *p)
{
    size_t i;
    for(i=0;i<1000 && (p->status==LB_PROCESS_READY || p->status==LB_PROCESS_YIELDED);++i) {
        lb_process_run(p,1,4);
        if(p->status==LB_PROCESS_YIELDED) CHECK(lb_process_collect(p,0)==LB_PROCESS_YIELDED);
    }
    CHECK(p->status==LB_PROCESS_DONE && p->collections); return lb_process_result(p);
}
static void bytes(Eterm term,const char *text)
{
    LbBitstringView view;
    CHECK(lb_bitstring_view(term,&view) && !view.bit_offset && view.bit_size==strlen(text)*8 && !memcmp(view.data,text,strlen(text)));
}
static void semantics(LbCodeSpace *space,LbCodeModule *module)
{
    LbProcess *p; Eterm value; size_t i,j;
    for(i=0;i<2;++i) {
        p=start(space,module,i?"tagged_saved":"tagged"); p->reg[0]=TUPLE2(p->htop,atom(space,"tag"),make_small(42)); p->htop+=3;
        CHECK(run(p)==make_small(42)); lb_process_destroy(p);
    }
    p=start(space,module,"tuple"); p->reg[0]=TUPLE2(p->htop,make_small(1),make_small(2)); p->htop+=3;
    value=run(p); CHECK(is_tuple(value) && arityval(*tuple_val(value))==2 && tuple_val(value)[1]==make_small(2) && tuple_val(value)[2]==make_small(1));
    lb_process_destroy(p);
    p=start(space,module,"list"); p->reg[0]=CONS(p->htop,make_small(7),make_small(8)); p->htop+=2;
    value=run(p); CHECK(is_tuple(value) && arityval(*tuple_val(value))==2 && tuple_val(value)[1]==make_small(7) && tuple_val(value)[2]==make_small(8));
    lb_process_destroy(p);
    for(i=0;i<2;++i) {
        p=start(space,module,i?"head_saved":"head"); p->reg[0]=CONS(p->htop,make_small(9),NIL); p->htop+=2;
        CHECK(run(p)==make_small(9)); lb_process_destroy(p);
    }
    p=start(space,module,"build"); p->reg[0]=make_small(7); value=run(p);
    CHECK(is_list(value) && CAR(list_val(value))==make_small(7)); value=CDR(list_val(value));
    CHECK(is_list(value) && CAR(list_val(value))==make_small(7) && CDR(list_val(value))==NIL); lb_process_destroy(p);
    for(i=0;i<2;++i) for(j=0;j<5;++j) {
        Eterm elements[3]={NIL,NIL,NIL};
        p=start(space,module,i?"two_arities":"arities");
        if(j<4) { CHECK(lb_term_tuple(p->htop,64,elements,j,&p->reg[0])); p->htop+=j?j+1:2; }
        value=run(p);
        bytes(value,j==0?"0":j==1&&!i?"1":j==2?"2":"other"); lb_process_destroy(p);
    }
    {
        const char *functions[]={"tagged","tuple","list","head"};
        const char *answers[]={"not-tagged","not-tuple","not-list","not-list"};
        for(i=0;i<4;++i) { p=start(space,module,functions[i]); bytes(run(p),answers[i]); lb_process_destroy(p); }
    }
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
static void arity_tables(LbEngine *engine,unsigned char *data,size_t size)
{
    LbCodeSpace *space; LbBeamProgram *program; LbBeamError error; const LbBeamOp *op;
    size_t base=code_offset(data,size),end=0,header=0,mode; unsigned char saved[9];
    CHECK(lb_code_space_create(engine,&space)==LB_CODE_OK);
    CHECK(lb_beam_program_prepare(engine->domain,space->atoms,data,size,&program,&error)==LB_BEAM_OK);
    for(op=lb_beam_program_ops(program);op;op=op->next) {
        if(op->op==genop_int_func_start_5) header=(size_t)op->a[0].val;
        if(op->op==genop_select_tuple_arity_3 && op->a[2].val==6) {
            CHECK(op->next && op->next->op==genop_label_1); end=base+op->next->offset; break;
        }
    }
    CHECK(end>=9 && end<size && header>15 && header<256);
    memcpy(saved,data+end-9,9);
    CHECK(saved[0]==0 && saved[3]==0x10 && saved[6]==0x20 && saved[1]==0x0d && saved[4]==0x0d && saved[7]==0x0d);
    lb_beam_program_destroy(program); CHECK(lb_code_space_destroy(space)==LB_CODE_OK);
    for(mode=0;mode<2;++mode) {
        LbCodeModule *module; LbProcess *p; size_t i;
        if(!mode) { memcpy(data+end-9,saved+6,3); memcpy(data+end-3,saved,3); }
        else data[end-1]=(unsigned char)header; /* real backward packed-table error target */
        CHECK(lb_code_space_create(engine,&space)==LB_CODE_OK);
        CHECK(lb_code_load(space,data,size,&module,&error)==LB_CODE_OK);
        if(!mode) semantics(space,module);
        else {
            p=start(space,module,"arities"); p->reg[0]=TUPLE2(p->htop,NIL,NIL); p->htop+=3;
            for(i=0;i<100 && (p->status==LB_PROCESS_READY || p->status==LB_PROCESS_YIELDED);++i) {
                lb_process_run(p,1,4);
                if(p->status==LB_PROCESS_YIELDED) CHECK(lb_process_collect(p,0)==LB_PROCESS_YIELDED);
            }
            CHECK(p->status==LB_PROCESS_EXCEPTION && lb_process_exception(p)==atom(space,"function_clause"));
            lb_process_destroy(p);
        }
        CHECK(lb_code_unload(module)==LB_CODE_OK && lb_code_space_destroy(space)==LB_CODE_OK);
        memcpy(data+end-9,saved,9);
    }
}
static void malformed(LbEngine *engine,unsigned char *data,size_t size)
{
    LbCodeSpace *space; LbBeamProgram *program; LbBeamError error; const LbBeamOp *op;
    size_t get=SIZE_MAX,list=SIZE_MAX,arity=SIZE_MAX,base=code_offset(data,size),i;
    CHECK(lb_code_space_create(engine,&space)==LB_CODE_OK);
    CHECK(lb_beam_program_prepare(engine->domain,space->atoms,data,size,&program,&error)==LB_BEAM_OK);
    for(op=lb_beam_program_ops(program);op;op=op->next) {
        if(op->op==genop_get_tuple_element_3 && get==SIZE_MAX) get=base+op->offset;
        if(op->op==genop_is_nonempty_list_2 && list==SIZE_MAX) list=base+op->offset;
        if(op->op==genop_test_arity_3 && arity==SIZE_MAX) arity=base+op->offset;
    }
    CHECK(get<size-4 && list<size-3 && arity<size-4 && data[get+2]==0x10 && data[arity+3]==0x20);
    lb_beam_program_destroy(program); CHECK(lb_code_space_destroy(space)==LB_CODE_OK);
    for(i=0;i<3;++i) {
        size_t where=i==0?get+2:i==1?list:arity;
        unsigned char saved=data[where],second=data[arity+3]; LbCodeModule *module;
        CHECK(lb_code_space_create(engine,&space)==LB_CODE_OK);
        data[where]=i==0?0x20:i==1?genop_is_nil_2:genop_is_eq_exact_3;
        if(i==2) data[arity+3]=0x21; /* equality with small 2 proves no tuple bound */
        CHECK(lb_code_load(space,data,size,&module,&error)==LB_CODE_FORMAT && !module);
        CHECK(!strcmp(error.stage,"tuple/list shape admission"));
        data[where]=saved; data[arity+3]=second;
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
    printf("CORE_SHAPE_FAILURES_OK load_prefixes=%zu retry_each=true unproved_extraction_refused=true\n",calls);
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
    semantics(space,module);
    CHECK(lb_code_unload(module)==LB_CODE_OK && lb_code_space_destroy(space)==LB_CODE_OK);
    arity_tables(engine,data,(size_t)length);
    malformed(engine,data,(size_t)length); failures(engine,&memory,data,(size_t)length);
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine); free(data);
    CHECK(!memory.live && !memory.bytes);
    puts("CORE_SHAPE_EXECUTION_OK tuple_list_extraction=true native_arity_tables=true unsorted_keys=true signed_table_targets=true forced_gc_each_dispatch=true physical_retirement=true");
    return 0;
}
