/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#include "binary_size.h"
#include "big.h"
#include "flatmap.h"
#include "process_internal.h"
#include "writable.h"
#include <stdio.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"CHECK %s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while(0)
#define SEG(k,u,n,s,z) {LB_BSC_##k,u,(Uint)(n)<<3,s,z}
static void expect_error(LbBinarySizeResult r,LbBinarySegmentKind kind,Uint segment,Eterm reason,Eterm info,Eterm value)
{
    CHECK(r.status==LB_BINARY_SIZE_GUEST_ERROR && r.as.failure.kind==kind && r.as.failure.segment==segment);
    CHECK(r.as.failure.reason==reason && r.as.failure.info==info && r.as.failure.value==value);
}
static void kinds(void)
{
    unsigned char text[3]={1,2,3}; Eterm x[]={make_small(7),make_small(-1)},y[]={0,make_small(9)};
    LbBinaryRegisters regs={x,2,y,2};
    Uint descriptors[][5]={SEG(APPEND,1,0,am_false,am_all),SEG(PRIVATE_APPEND,1,0,am_false,am_all),
        SEG(BINARY_FIXED_SIZE,0,1,am_false,23),SEG(FLOAT_FIXED_SIZE,0,1,am_false,16),
        SEG(INTEGER_FIXED_SIZE,0,1,am_false,0),SEG(STRING,0,1,(Uint)text,3),
        SEG(BINARY,8,1,am_false,make_loader_x_reg(0)),SEG(FLOAT,8,1,am_false,make_loader_y_reg(1)),
        SEG(INTEGER,1,1,am_false,make_small(0)),SEG(UTF8,0,1,make_small(0x80),NIL),
        SEG(UTF16,0,1,make_small(0x10000),NIL),SEG(UTF32,0,1,make_small(0x10ffff),32)};
    const size_t bits[]={0,0,23,16,0,24,56,72,0,16,32,32};
    for(size_t i=0;i<sizeof(bits)/sizeof(*bits);++i) {
        Uint copy[5]; memcpy(copy,descriptors[i],sizeof(copy));
        LbBinarySizeResult r=lb_binary_size(regs,descriptors[i],5,SIZE_MAX);
        CHECK(r.status==LB_BINARY_SIZE_OK && r.as.bits==bits[i] && !memcmp(copy,descriptors[i],sizeof(copy)));
        if(bits[i]) CHECK(lb_binary_size(regs,descriptors[i],5,bits[i]-1).status==LB_BINARY_SIZE_LIMIT);
    }
    Uint all[]=SEG(BINARY_ALL,8,3,make_loader_x_reg(1),am_all);
    expect_error(lb_binary_size(regs,all,5,SIZE_MAX),LB_BSC_BINARY_ALL,3,am_badarg,am_type,x[1]);
    Eterm storage[3]={_make_header(2,HEAP_BITS_SUBTAG),9,0}; x[1]=make_boxed(storage);
    LbBinarySizeResult r=lb_binary_size(regs,all,5,9); CHECK(r.status==LB_BINARY_SIZE_OK && r.as.bits==9);
    /* Unit divisibility is checked by the writer, not by native sizing. */
    LbBitWriter w; unsigned char output[2]={0}; CHECK(lb_bit_writer_init(&w,output,2)==LB_BITS_OK);
    CHECK(lb_bits_put_binary_all(&w,x[1],8)==LB_BITS_BADARG && !w.offset);
}
static void widths(void)
{
    Eterm positive[]={_make_header(2,POS_BIG_SUBTAG),0,1};
    Eterm negative[]={_make_header(2,NEG_BIG_SUBTAG),0,1};
    Eterm sizes[]={make_small(-1),make_small(0),make_small(MAX_SMALL),make_boxed(positive),make_boxed(negative),NIL,am_true};
    Eterm x[1]; LbBinaryRegisters regs={x,1,NULL,0};
    const LbBinarySegmentKind kinds[]={LB_BSC_BINARY,LB_BSC_FLOAT,LB_BSC_INTEGER};
    for(size_t k=0;k<sizeof(kinds)/sizeof(*kinds);++k) {
        LbBinarySegmentKind kind=kinds[k]; Uint d[]={kind,1,7<<3,am_false,make_loader_x_reg(0)};
        for(size_t i=0;i<sizeof(sizes)/sizeof(*sizes);++i) {
            x[0]=sizes[i]; LbBinarySizeResult r=lb_binary_size(regs,d,5,SIZE_MAX);
            if(i==1 || i==2) CHECK(r.status==LB_BINARY_SIZE_OK && r.as.bits==(Uint)signed_val(x[0]));
            else expect_error(r,(LbBinarySegmentKind)kind,7,i==3?am_system_limit:am_badarg,am_size,x[0]);
        }
    }
    Uint seed=0x813446feab2319;
    for(unsigned i=0;i<10000;++i) {
        seed=seed*6364136223846793005ULL+1; Uint size=seed&(Uint)MAX_SMALL,unit=(seed>>32)%256+1;
        Uint d[]=SEG(INTEGER,unit,1,make_small(0),make_small(size));
        __uint128_t expected=(__uint128_t)size*unit; LbBinarySizeResult r=lb_binary_size((LbBinaryRegisters){0},d,5,SIZE_MAX);
        if(expected>INT64_MAX) expect_error(r,LB_BSC_INTEGER,1,am_system_limit,am_size,make_small(size));
        else CHECK(r.status==LB_BINARY_SIZE_OK && r.as.bits==(size_t)expected);
    }
    Uint many[4][5]={SEG(INTEGER,16,1,make_small(0),make_small(MAX_SMALL)),
        SEG(INTEGER,16,2,make_small(0),make_small(MAX_SMALL)),SEG(INTEGER,16,3,make_small(0),make_small(MAX_SMALL)),
        SEG(INTEGER,1,4,make_small(0),make_small(-1))};
    CHECK(lb_binary_size((LbBinaryRegisters){0},many[0],15,SIZE_MAX).status==LB_BINARY_SIZE_LIMIT);
    expect_error(lb_binary_size((LbBinaryRegisters){0},many[0],20,SIZE_MAX),LB_BSC_INTEGER,4,am_badarg,am_size,make_small(-1));
    expect_error(lb_binary_size((LbBinaryRegisters){0},many[0],20,0),LB_BSC_INTEGER,4,am_badarg,am_size,make_small(-1));
}
static void unicode(void)
{
    for(Uint scalar=0;scalar<=0x110000;++scalar) {
        Uint d[]=SEG(UTF8,0,1,make_small(scalar),NIL); LbBinarySizeResult r;
        r=lb_binary_size((LbBinaryRegisters){0},d,5,SIZE_MAX);
        CHECK(r.status==LB_BINARY_SIZE_OK && r.as.bits==(scalar<=0x7f?8:scalar<=0x7ff?16:scalar<=0xffff?24:32));
        d[0]=LB_BSC_UTF16; r=lb_binary_size((LbBinaryRegisters){0},d,5,SIZE_MAX);
        CHECK(r.status==LB_BINARY_SIZE_OK && r.as.bits==(scalar<=0xffff?16:32));
        d[0]=LB_BSC_UTF32; d[4]=32; r=lb_binary_size((LbBinaryRegisters){0},d,5,SIZE_MAX);
        if(scalar==0x110000 || (scalar>=0xd800 && scalar<=0xdfff)) expect_error(r,LB_BSC_UTF32,1,am_badarg,am_type,d[3]);
        else CHECK(r.status==LB_BINARY_SIZE_OK && r.as.bits==32);
    }
    Uint pair[2][5]={SEG(UTF8,0,1,make_small(-1),NIL),SEG(INTEGER,1,2,make_small(0),make_small(-1))};
    for(unsigned mode=0;mode<3;++mode) {
        pair[0][0]=LB_BSC_UTF8+mode; pair[0][4]=mode==2?32:NIL;
        LbBinarySizeResult r=lb_binary_size((LbBinaryRegisters){0},pair[0],10,0);
        if(mode==2) expect_error(r,LB_BSC_UTF32,1,am_badarg,am_type,make_small(-1));
        else expect_error(r,LB_BSC_INTEGER,2,am_badarg,am_size,make_small(-1));
    }
}
static void malformed(void)
{
    Uint good[]=SEG(INTEGER,1,1,make_small(0),make_small(3));
    CHECK(lb_binary_size((LbBinaryRegisters){0},NULL,5,0).status==LB_BINARY_SIZE_INVALID);
    CHECK(lb_binary_size((LbBinaryRegisters){0},good,0,0).status==LB_BINARY_SIZE_INVALID);
    CHECK(lb_binary_size((LbBinaryRegisters){0},good,6,0).status==LB_BINARY_SIZE_INVALID);
    for(unsigned mode=0;mode<10;++mode) {
        Uint d[5]; memcpy(d,good,sizeof(d)); LbBinaryRegisters regs={0};
        switch(mode) {
        case 0:d[0]=99;break; case 1:d[1]=0;break; case 2:d[1]=257;break;
        case 3:d[2]=1;break; case 4:d[2]=((Uint)MAX_SMALL+1)<<3;break;
        case 5:d[3]=make_loader_x_reg(0);break; case 6:d[4]=make_loader_y_reg(0);break;
        case 7:d[0]=LB_BSC_UTF32;break; case 8:d[0]=LB_BSC_STRING;d[1]=0;d[4]=SIZE_MAX;break;
        case 9:regs.x_count=1;break;
        }
        CHECK(lb_binary_size(regs,d,5,SIZE_MAX).status==LB_BINARY_SIZE_INVALID);
    }
    Eterm x[MAX_REG],y[MAX_REG+1];
    for(size_t i=0;i<MAX_REG;++i) x[i]=y[i]=NIL;
    x[MAX_REG-1]=make_small(5); y[MAX_REG]=make_small(6);
    LbBinaryRegisters registers={x,MAX_REG,y,MAX_REG+1}; Uint edge[]=SEG(INTEGER,1,1,NIL,make_loader_x_reg(MAX_REG-1));
    LbBinarySizeResult boundary=lb_binary_size(registers,edge,5,SIZE_MAX);
    CHECK(boundary.status==LB_BINARY_SIZE_OK && boundary.as.bits==5);
    edge[4]=make_loader_x_reg(MAX_REG); CHECK(lb_binary_size(registers,edge,5,SIZE_MAX).status==LB_BINARY_SIZE_INVALID);
    edge[4]=make_loader_y_reg(MAX_REG); boundary=lb_binary_size(registers,edge,5,SIZE_MAX);
    CHECK(boundary.status==LB_BINARY_SIZE_OK && boundary.as.bits==6);
    --registers.y_count; CHECK(lb_binary_size(registers,edge,5,SIZE_MAX).status==LB_BINARY_SIZE_INVALID);
    edge[4]=make_loader_y_reg(MAX_REG+1); CHECK(lb_binary_size(registers,edge,5,SIZE_MAX).status==LB_BINARY_SIZE_INVALID);
    Uint count=5*(65536/6); Uint *many=malloc(count*sizeof(Uint)); CHECK(many);
    for(Uint i=0;i<count;i+=5) memcpy(many+i,good,sizeof(good));
    LbBinarySizeResult r=lb_binary_size((LbBinaryRegisters){0},many,count,SIZE_MAX);
    CHECK(r.status==LB_BINARY_SIZE_OK && r.as.bits==3*count/5);
    many[5]=LB_BSC_APPEND; CHECK(lb_binary_size((LbBinaryRegisters){0},many,count,SIZE_MAX).status==LB_BINARY_SIZE_INVALID); free(many);
}
typedef struct { size_t calls,fail,live; } Memory;
static void *allocate(void *context,size_t size)
{
    Memory *m=context; if(++m->calls==m->fail) return NULL;
    void *p=malloc(size); if(p) ++m->live; return p;
}
static void release(void *context,void *p) { Memory *m=context; CHECK(m->live); --m->live; memset(p,0xa5,1); free(p); }
static void run_identity(LbProcess *p)
{
    LbProcessStatus status=LB_PROCESS_YIELDED; size_t steps=0;
    while(status==LB_PROCESS_YIELDED && steps++<1000) {
        status=lb_process_run(p,1,100); CHECK(lb_process_collect(p,0)==status);
    }
    CHECK(status==LB_PROCESS_DONE);
}
static void append_bits(unsigned char *out,size_t *offset,const unsigned char *src,size_t bits)
{
    for(size_t i=0;i<bits;++i,++*offset) if((src[i/8]>>(7-i%8))&1) out[*offset/8]|=(unsigned char)(1u<<(7-*offset%8));
}
static void consumer(LbCodeEntry *identity,Memory *m)
{
    unsigned char input[65],expected[76]={0}; for(size_t i=0;i<sizeof(input);++i) input[i]=(unsigned char)(i*71+3);
    static const unsigned char string[]="size",smile[]={0xf0,0x9f,0x99,0x82},ones[]={0xf8},last[]={0,0x98};
    LbProcess *p; CHECK(lb_process_create_binary(identity,input,sizeof(input),8,&p)==LB_PROCESS_READY);
    CHECK(lb_heap_reserve(p,3,MAX_REG)); Eterm continuation=p->stop[0]; --p->stop; p->stop[0]=continuation; p->stop[1]=make_small(13);
    Uint d[5][5]={SEG(STRING,0,1,(Uint)string,4),SEG(UTF8,0,2,make_small(0x1f642),NIL),
        SEG(INTEGER_FIXED_SIZE,0,3,make_small(-1),5),SEG(BINARY_ALL,1,4,make_loader_x_reg(0),am_all),
        SEG(INTEGER,1,5,make_small(19),make_loader_y_reg(1))};
    LbBinaryRegisters regs={p->reg,MAX_REG,p->stop,(size_t)(p->hend-p->stop)}; size_t calls=m->calls;
    LbBinarySizeResult r=lb_binary_size(regs,d[0],25,64*1024*1024*8);
    CHECK(r.status==LB_BINARY_SIZE_OK && r.as.bits==602 && m->calls==calls);
    Eterm result; LbBitWriter writer; size_t collections=p->collections;
    CHECK(lb_binary_begin(p,r.as.bits,4096,&writer,&result)==LB_WRITABLE_OK && p->collections>collections);
    CHECK(lb_bits_put_string(&writer,string,4)==LB_BITS_OK && lb_bits_put_utf8(&writer,make_small(0x1f642))==LB_BITS_OK);
    CHECK(lb_bits_put_integer(&writer,make_small(-1),5,LB_BITS_BIG_ENDIAN)==LB_BITS_OK);
    CHECK(lb_bits_put_binary_all(&writer,p->reg[0],1)==LB_BITS_OK);
    CHECK(lb_bits_put_integer(&writer,make_small(19),(size_t)signed_val(p->stop[1]),LB_BITS_BIG_ENDIAN)==LB_BITS_OK && writer.offset==r.as.bits);
    p->reg[0]=result; run_identity(p); LbBitstringView view; size_t offset=0;
    append_bits(expected,&offset,string,32); append_bits(expected,&offset,smile,32); append_bits(expected,&offset,ones,5);
    append_bits(expected,&offset,input,520); append_bits(expected,&offset,last,13);
    CHECK(lb_bitstring_view(p->reg[0],&view) && view.bit_size==602 && !memcmp(view.data,expected,sizeof(expected)));
    lb_process_destroy(p);
}
static void error_root(LbCodeEntry *identity,Memory *m)
{
    LbProcess *p; Eterm nil=NIL,payload=make_small(17); CHECK(lb_process_create(identity,&nil,1,128,&p)==LB_PROCESS_READY);
    Eterm *big=p->htop; big[0]=_make_header(2,POS_BIG_SUBTAG); big[1]=0; big[2]=1; p->htop+=3; p->reg[0]=make_boxed(big);
    Uint d[]=SEG(INTEGER,1,MAX_SMALL,make_small(0),make_loader_x_reg(0));
    size_t calls=m->calls; LbBinarySizeResult r=lb_binary_size((LbBinaryRegisters){p->reg,MAX_REG,p->stop,1},d,5,SIZE_MAX);
    expect_error(r,LB_BSC_INTEGER,MAX_SMALL,am_system_limit,am_size,p->reg[0]); CHECK(m->calls==calls);
    p->reg[0]=NIL; p->heap_reserved=7;
    while((size_t)(p->stop-p->htop)>=10) { p->htop[0]=make_small(0); p->htop[1]=NIL; p->htop+=2; }
    size_t live=m->live,collections=p->collections; Eterm *heap=p->heap;
    m->fail=m->calls+1; LbBinaryFailure f=r.as.failure;
    CHECK(lb_binary_error_payload(p,f.kind,f.segment,f.info,f.value,&payload)==LB_ALLOC_NO_MEMORY && payload==make_small(17));
    CHECK(m->live==live && p->heap==heap && p->collections==collections && p->construction_root==NIL);
    m->fail=0; CHECK(lb_binary_error_payload(p,f.kind,f.segment,f.info,f.value,&payload)==LB_ALLOC_OK && p->collections>collections);
    CHECK(p->heap_reserved==7 && p->freason==LB_EXC_NULL && p->fvalue==NIL); p->reg[0]=payload;
    run_identity(p); Eterm cause=boxed_val(p->reg[0])[3],value=boxed_val(cause)[4];
    CHECK(lb_is_big(value) && boxed_val(value)[1]==0 && boxed_val(value)[2]==1 && boxed_val(cause)[1]==make_small(MAX_SMALL));
    lb_process_destroy(p);
}
static void execution(const char *path)
{
    FILE *f=fopen(path,"rb"); CHECK(f && !fseek(f,0,SEEK_END)); long length=ftell(f); CHECK(length>0 && !fseek(f,0,SEEK_SET));
    unsigned char *data=malloc((size_t)length); CHECK(data && fread(data,1,(size_t)length,f)==(size_t)length && !fclose(f));
    Memory m={0}; LbSystemAllocator callbacks={&m,allocate,release}; LbEngine *engine; LbCodeSpace *space;
    LbCodeModule *module; LbCodeEntry *entry; LbBeamError error; Eterm function;
    CHECK(lb_engine_create(&callbacks,&engine)==LB_ENGINE_OK && lb_code_space_create(engine,&space)==LB_CODE_OK);
    CHECK(lb_code_load(space,data,(size_t)length,&module,&error)==LB_CODE_OK); memset(data,0xa5,(size_t)length); free(data);
    CHECK(lb_atoms_find(space->atoms,"identity",8,&function)==LB_ATOM_OK && lb_code_entry_acquire(space,lb_code_module_name(module),function,1,&entry)==LB_CODE_OK);
    consumer(entry,&m); error_root(entry,&m); lb_code_entry_release(entry); CHECK(lb_code_unload(module)==LB_CODE_OK);
    CHECK(lb_code_space_destroy(space)==LB_CODE_OK && lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine); CHECK(!m.live);
    puts("CORE_BINARY_SIZE_EXECUTION_OK owned_construction=true reload_x_y=true exact_writer_completion=true identity_execution=true moving_gc=true borrowed_error_root=true payload_oom_retry=true exact_retirement=true guest_construction=false");
}
static void reference(void)
{
    Eterm positive[]={_make_header(2,POS_BIG_SUBTAG),0,1},negative[]={_make_header(2,NEG_BIG_SUBTAG),0,1};
    Eterm big=make_boxed(positive),neg=make_boxed(negative),minus=make_small(-1),zero=make_small(0);
    Uint cases[11][15]={
        {LB_BSC_UTF8,0,8,minus,NIL,LB_BSC_INTEGER,1,16,zero,minus},
        {LB_BSC_UTF16,0,8,minus,NIL,LB_BSC_INTEGER,1,16,zero,minus},
        {LB_BSC_UTF32,0,8,minus,32,LB_BSC_INTEGER,1,16,zero,minus},
        {LB_BSC_INTEGER,1,8,zero,big},
        {LB_BSC_INTEGER,1,8,zero,neg},
        {LB_BSC_INTEGER,1,8,zero,am_false},
        {LB_BSC_INTEGER,256,8,zero,make_small(MAX_SMALL)},
        {LB_BSC_INTEGER,1,8,zero,make_small(MAX_SMALL),LB_BSC_INTEGER,1,16,zero,minus},
        {LB_BSC_INTEGER_FIXED_SIZE,0,8,zero,1,LB_BSC_BINARY_ALL,1,16,am_false,am_all,LB_BSC_INTEGER,1,24,zero,minus},
        {LB_BSC_FLOAT_FIXED_SIZE,0,8,am_false,7,LB_BSC_INTEGER,1,16,zero,minus},
        {LB_BSC_INTEGER,1,8,zero,minus,LB_BSC_UTF32,0,16,minus,32}
    };
    const size_t counts[]={10,10,10,5,5,5,5,10,15,10,10};
    const Eterm values[]={minus,minus,minus,big,neg,am_false,make_small(MAX_SMALL),minus,am_false,minus,minus};
    const char *names[]={"utf8","utf16","utf32","positive_big","negative_big","noninteger","multiply","late","binary","floating","integer_first"};
    for(size_t i=0;i<11;++i) {
        LbBinarySizeResult r=lb_binary_size((LbBinaryRegisters){0},cases[i],counts[i],64*1024*1024*8);
        CHECK(r.status==LB_BINARY_SIZE_GUEST_ERROR && r.as.failure.value==values[i]); LbBinaryFailure f=r.as.failure;
        const char *operation=f.kind==LB_BSC_UTF32?"utf32":f.kind==LB_BSC_BINARY_ALL?"binary":"integer";
        printf("%s:%s:%zu:%s:%s\n",names[i],f.reason==am_system_limit?"system_limit":"badarg",(size_t)f.segment,operation,f.info==am_size?"size":"type");
    }
}
int main(int argc,char **argv)
{
    if(argc==2 && !strcmp(argv[1],"--reference")) { reference(); return 0; }
    kinds(); widths(); unicode(); malformed(); if(argc==2) execution(argv[1]); else CHECK(argc==1);
    CHECK(!lb_instruction_supported(op_i_bs_create_bin_jIWdW));
    puts("CORE_BINARY_SIZE_OK native_flat64=true all_kinds=true unicode_order=true arithmetic_oracle=10000 aggregate_overflow=true late_error_before_limit=true checked_register_spans=true maximum_descriptors=true allocation_free=true opcode_admitted=false");
    return 0;
}
