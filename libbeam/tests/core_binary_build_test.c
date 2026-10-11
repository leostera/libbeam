/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#include "process_internal.h"
#include "binary_build.h"
#include "flatmap.h"
#include <stdio.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"CHECK %s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while(0)
typedef struct { size_t calls,fail,live,bytes; } Memory;
typedef union { max_align_t alignment; size_t bytes; } Header;
static void *allocate(void *ctx,size_t size)
{
    Memory *m=ctx; if(++m->calls==m->fail) return NULL; Header *h=malloc(sizeof(*h)+size); if(!h) return NULL;
    h->bytes=size; ++m->live; m->bytes+=size; return h+1;
}
static void release(void *ctx,void *ptr)
{
    Memory *m=ctx; Header *h=(Header *)ptr-1; CHECK(m->live && m->bytes>=h->bytes); --m->live; m->bytes-=h->bytes; free(h);
}
static LbCodeModule *load(LbCodeSpace *space,const char *path)
{
    FILE *f=fopen(path,"rb"); CHECK(f && !fseek(f,0,SEEK_END)); long size=ftell(f); CHECK(size>0 && !fseek(f,0,SEEK_SET));
    unsigned char *data=malloc((size_t)size); CHECK(data && fread(data,1,(size_t)size,f)==(size_t)size && !fclose(f));
    LbCodeModule *module=NULL; LbBeamError error; LbCodeStatus status=lb_code_load(space,data,(size_t)size,&module,&error);
    if(status!=LB_CODE_OK) fprintf(stderr,"load=%d stage=%s offset=%zu\n",status,error.stage,error.offset);
    CHECK(status==LB_CODE_OK); memset(data,0xa5,(size_t)size); free(data); return module;
}
static LbCodeEntry *entry(LbCodeSpace *space,LbCodeModule *module,const char *name,size_t arity)
{
    Eterm atom; LbCodeEntry *e; CHECK(lb_atoms_find(space->atoms,name,strlen(name),&atom)==LB_ATOM_OK);
    CHECK(lb_code_entry_acquire(space,lb_code_module_name(module),atom,arity,&e)==LB_CODE_OK); return e;
}
static LbProcess *process(LbCodeEntry *entry,const Eterm *args,size_t arity)
{
    LbProcess *p; CHECK(lb_process_create(entry,args,arity,256,&p)==LB_PROCESS_READY); return p;
}
static void binary(LbProcess *p,size_t reg,const unsigned char *bytes,size_t count)
{
    CHECK(lb_heap_reserve(p,lb_bitstring_heap_words(count*8),MAX_REG));
    CHECK(lb_bitstring_build(p->entry_module->space->domain,&p->off_heap,&p->htop,bytes,count*8,0,&p->reg[reg])==LB_ALLOC_OK);
}
static LbProcessStatus finish(LbProcess *p)
{
    for(size_t i=0;i<100;++i) { LbProcessStatus s=lb_process_run(p,10000,128); if(s!=LB_PROCESS_YIELDED) return s; }
    CHECK(0); return LB_PROCESS_INVALID;
}
static void stop_at_build(LbProcess *p)
{
    for(size_t i=0;i<100 && *p->i!=op_i_bs_create_bin_jIWdW;++i) CHECK(lb_process_run(p,1,128)==LB_PROCESS_YIELDED);
    CHECK(*p->i==op_i_bs_create_bin_jIWdW);
}
static void print_binary(const char *tag,LbProcess *p,int output)
{
    CHECK(finish(p)==LB_PROCESS_DONE); LbBitstringView view; CHECK(lb_bitstring_view(lb_process_result(p),&view) && !view.bit_offset);
    if(output) { printf("%s:%zu:",tag,view.bit_size); for(size_t i=0;i<(view.bit_size+7)/8;++i) printf("%02X",view.data[i]); putchar('\n'); }
    CHECK(lb_process_collect(p,0)==LB_PROCESS_DONE); lb_process_destroy(p);
}
static void vectors(LbCodeSpace *space,LbCodeModule *module,int output)
{
    LbCodeEntry *e=entry(space,module,"integer",2); const Sint values[]={-129,-1,0,1,511}; char tag[64];
    for(size_t i=0;i<5;++i) for(size_t width=0;width<130;++width) {
        Eterm args[]={make_small(values[i]),make_small(width)}; LbProcess *p=process(e,args,2);
        snprintf(tag,sizeof(tag),"integer:%lld:%zu",(long long)values[i],width); print_binary(tag,p,output);
    }
    lb_code_entry_release(e); e=entry(space,module,"floating",2);
    for(size_t width=16;width<=64;width*=2) {
        Eterm args[]={NIL,make_small(width)}; LbProcess *p=process(e,args,2); double value=1.1;
        p->htop[0]=_make_header(1,FLOAT_SUBTAG); memcpy(p->htop+1,&value,8); p->reg[0]=make_boxed(p->htop); p->htop+=2;
        snprintf(tag,sizeof(tag),"float:%zu",width); print_binary(tag,p,output);
    }
    lb_code_entry_release(e);
    const char *unicode[]={"utf8","utf16","utf32"}; const Sint scalars[]={0,127,128,2047,2048,0x1f642};
    for(size_t kind=0;kind<3;++kind) {
        e=entry(space,module,unicode[kind],1);
        for(size_t i=0;i<6;++i) { Eterm arg=make_small(scalars[i]); snprintf(tag,sizeof(tag),"%s:%lld",unicode[kind],(long long)scalars[i]); print_binary(tag,process(e,&arg,1),output); }
        lb_code_entry_release(e);
    }
    unsigned char bytes[65]; for(size_t i=0;i<65;++i) bytes[i]=(unsigned char)i;
    e=entry(space,module,"binary",2); const size_t widths[]={0,1,7,8,9,519,520};
    for(size_t i=0;i<7;++i) { Eterm args[]={NIL,make_small(widths[i])}; LbProcess *p=process(e,args,2); binary(p,0,bytes,65);
        snprintf(tag,sizeof(tag),"binary:%zu",widths[i]); print_binary(tag,p,output); }
    lb_code_entry_release(e);
    const char *names[]={"decorate","append","first"};
    for(size_t i=0;i<3;++i) { e=entry(space,module,names[i],1); Eterm arg=NIL; LbProcess *p=process(e,&arg,1); binary(p,0,bytes,65); print_binary(names[i],p,output); lb_code_entry_release(e); }
    e=entry(space,module,"mixed",5); Eterm args[]={make_small(19),make_small(3),make_small(0x1f642),NIL,make_small(-7)};
    LbProcess *p=process(e,args,5); binary(p,3,bytes,65); print_binary("mixed",p,output); lb_code_entry_release(e);
    e=entry(space,module,"literal",1); Eterm width=make_small(72); print_binary("literal",process(e,&width,1),output); lb_code_entry_release(e);
}
static void errors(LbCodeSpace *space,LbCodeModule *module)
{
    const char *names[]={"integer","floating","binary","utf8","utf16","utf32"};
    const Eterm operations[]={am_integer,am_float,am_binary,am_utf8,am_utf16,am_utf32};
    for(size_t i=0;i<6;++i) {
        size_t arity=i<3?2:1; LbCodeEntry *e=entry(space,module,names[i],arity); Eterm args[]={am_false,make_small(i==1?7:8)};
        LbProcess *p=process(e,args,arity); CHECK(finish(p)==LB_PROCESS_EXCEPTION && lb_process_exception(p)==am_badarg);
        Eterm info=lb_process_exception_info(p); CHECK(is_boxed(info) && boxed_val(info)[0]==LB_FLATMAP_HEADER);
        Eterm *cause=tuple_val(boxed_val(info)[3]); CHECK(arityval(cause[0])==4 && cause[2]==operations[i]);
        CHECK(cause[3]==(i==1?am_invalid:am_type)); if(i==1) CHECK(cause[4]==make_small(7));
        CHECK(lb_process_collect(p,0)==LB_PROCESS_EXCEPTION); lb_process_destroy(p); lb_code_entry_release(e);
    }
    LbCodeEntry *e=entry(space,module,"integer",2); Eterm args[]={make_small(0),NIL}; LbProcess *p=process(e,args,2);
    p->htop[0]=_make_header(2,POS_BIG_SUBTAG); p->htop[1]=0; p->htop[2]=1; p->reg[1]=make_boxed(p->htop); p->htop+=3;
    CHECK(finish(p)==LB_PROCESS_EXCEPTION && lb_process_exception(p)==am_system_limit);
    CHECK(lb_process_exception_info(p)!=THE_NON_VALUE); lb_process_destroy(p); lb_code_entry_release(e);
    e=entry(space,module,"ordered",2); Eterm ordered[]={am_false,make_small(-1)}; p=process(e,ordered,2);
    CHECK(finish(p)==LB_PROCESS_EXCEPTION); Eterm *cause=tuple_val(boxed_val(lb_process_exception_info(p))[3]);
    CHECK(cause[1]==make_small(2) && cause[2]==am_integer && cause[3]==am_size);
    puts("CORE_BINARY_BUILD_ORDER_OK native=2:integer:size backend_difference_not_normalized=true");
    lb_process_destroy(p); lb_code_entry_release(e);
}
static void controls(LbCodeSpace *space,LbCodeModule *module,Memory *m)
{
    unsigned char bytes[65]; memset(bytes,0x5c,65); const char *names[]={"private","guard","y","private"};
    for(size_t i=0;i<4;++i) {
        LbCodeEntry *e=entry(space,module,names[i],1); Eterm arg=NIL; LbProcess *p=process(e,&arg,1); binary(p,0,bytes,65);
        stop_at_build(p); if(i==3) CHECK(lb_bitstring_pin(p->reg[0]));
        p->heap_reserved=(size_t)(p->stop-p->htop)-S_RESERVED;
        CHECK(finish(p)==LB_PROCESS_DONE); LbBitstringView view; CHECK(lb_bitstring_view(lb_process_result(p),&view));
        CHECK(view.bit_size==(i==2?66:65)*8 && !view.bit_offset && !memcmp(view.data,bytes,65)); if(i==2) CHECK(view.data[65]==33);
        CHECK(lb_process_collect(p,0)==LB_PROCESS_DONE); lb_process_destroy(p); lb_code_entry_release(e);
    }
    LbCodeEntry *e=entry(space,module,"limit",1); Eterm arg=NIL; LbProcess *p=process(e,&arg,1); stop_at_build(p); size_t calls=m->calls;
    CHECK(finish(p)==LB_PROCESS_LIMIT && m->calls==calls && lb_process_exception(p)==THE_NON_VALUE); lb_process_destroy(p); lb_code_entry_release(e);
    e=entry(space,module,"private_alias",1); p=process(e,&arg,1); stop_at_build(p); Eterm original=p->reg[0];
    CHECK(finish(p)==LB_PROCESS_REFUSED && p->reg[0]==original && p->reg[1]==original && lb_process_exception(p)==THE_NON_VALUE);
    LbBitstringView view; CHECK(lb_bitstring_view(original,&view) && !view.bit_size); lb_process_destroy(p); lb_code_entry_release(e);
    e=entry(space,module,"alias",1); p=process(e,&arg,1); CHECK(finish(p)==LB_PROCESS_DONE);
    CHECK(lb_bitstring_view(lb_process_result(p),&view) && view.bit_size==10 && !view.bit_offset && view.data[0]==0x9c && view.data[1]==0xc0);
    lb_process_destroy(p); lb_code_entry_release(e);
    e=entry(space,module,"loop",1); p=process(e,&arg,1); stop_at_build(p); const BeamInstr *pc=p->i;
    CHECK(lb_process_run(p,100,4)==LB_PROCESS_YIELDED && p->i==pc+5+pc[4]);
    CHECK(lb_bitstring_view(p->reg[0],&view) && view.bit_size==65536*8);
    calls=m->calls; CHECK(lb_process_run(p,1,128)==LB_PROCESS_YIELDED && m->calls==calls && p->i==pc);
    lb_process_destroy(p); lb_code_entry_release(e);
}
static void failures(LbCodeSpace *space,LbCodeModule *module,Memory *m)
{
    LbCodeEntry *e=entry(space,module,"decorate",1); size_t live=m->live,balance=m->bytes; unsigned char bytes[65]; memset(bytes,0x59,65);
    for(size_t fail=1;fail<=2;++fail) {
        Eterm arg=NIL; LbProcess *p=process(e,&arg,1); binary(p,0,bytes,65); stop_at_build(p);
        p->heap_reserved=(size_t)(p->stop-p->htop)-S_RESERVED; Eterm original=p->reg[0]; Eterm *heap=p->heap;
        m->fail=m->calls+fail; CHECK(finish(p)==LB_PROCESS_NO_MEMORY && p->reg[0]==original && p->heap==heap);
        CHECK(lb_process_result(p)==THE_NON_VALUE); m->fail=0; lb_process_destroy(p); CHECK(m->live==live && m->bytes==balance);
        p=process(e,&arg,1); binary(p,0,bytes,65); CHECK(finish(p)==LB_PROCESS_DONE); lb_process_destroy(p); CHECK(m->live==live && m->bytes==balance);
    }
    lb_code_entry_release(e);
    /* Allocate/write a real offheap result, then fail the float segment. Its
     * error factory may collect; partial backing must never become X0. */
    e=entry(space,module,"mixed",5);
    for(size_t fail=0;fail<2;++fail) {
        Eterm args[]={make_small(19),am_false,make_small(65),NIL,make_small(1)}; LbProcess *p;
        CHECK(lb_process_create(e,args,5,S_RESERVED+1+2*LB_REFC_BITS_WORDS,&p)==LB_PROCESS_READY);
        binary(p,3,bytes,65); stop_at_build(p); if(fail) m->fail=m->calls+2;
        CHECK(finish(p)==(fail?LB_PROCESS_NO_MEMORY:LB_PROCESS_EXCEPTION) && p->reg[0]==args[0]);
        if(!fail) CHECK(p->off_heap.first && !p->off_heap.first->next); /* abandoned result was swept */
        m->fail=0; lb_process_destroy(p);
    }
    lb_code_entry_release(e);
    e=entry(space,module,"tuple",1); Eterm arg=NIL; LbProcess *p=process(e,&arg,1); binary(p,0,bytes,65);
    stop_at_build(p); p->heap_reserved=(size_t)(p->stop-p->htop)-S_RESERVED;
    CHECK(finish(p)==LB_PROCESS_DONE); Eterm *tuple=tuple_val(lb_process_result(p)); CHECK(arityval(tuple[0])==2);
    LbBitstringView copy,original; CHECK(lb_bitstring_view(tuple[1],&copy) && lb_bitstring_view(tuple[2],&original));
    CHECK(copy.bit_size==70*8 && original.bit_size==65*8 && !memcmp(copy.data,"copy:",5));
    CHECK(!memcmp(copy.data+5,bytes,65) && !memcmp(original.data,bytes,65));
    CHECK(lb_process_collect(p,0)==LB_PROCESS_DONE); lb_process_destroy(p); lb_code_entry_release(e);
}
int main(int argc,char **argv)
{
    CHECK(lb_instruction_supported(op_i_bs_create_bin_jIWdW));
    if(argc==1) { puts("CORE_BINARY_BUILD_GATE_OK whole_case=true"); return 0; }
    int output=argc==4 && !strcmp(argv[1],"--reference"); CHECK(argc==3 || output);
    Memory m={0}; LbSystemAllocator allocator={&m,allocate,release}; LbEngine *engine; LbCodeSpace *space;
    CHECK(lb_engine_create(&allocator,&engine)==LB_ENGINE_OK && lb_code_space_create(engine,&space)==LB_CODE_OK);
    LbCodeModule *module=load(space,argv[argc-2]),*control=load(space,argv[argc-1]);
    vectors(space,module,output); if(!output) { errors(space,module); controls(space,control,&m); failures(space,module,&m); }
    CHECK(lb_code_unload(control)==LB_CODE_OK && lb_code_unload(module)==LB_CODE_OK);
    CHECK(lb_code_space_destroy(space)==LB_CODE_OK && lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine); CHECK(!m.live && !m.bytes);
    if(!output) puts("CORE_BINARY_BUILD_EXECUTION_OK native_words=true whole_dispatch=true actual_writers=true private_append=true guard_failure=true y_gc=true infrastructure_refusal=true failure_prefixes=2 physical_retirement=true");
    return 0;
}
