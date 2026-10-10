/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#include "beam_program.h"
#include "beam_reader.h"
#include "beam_select.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); abort(); } } while(0)
typedef struct { void *p; size_t n; } Allocation;
typedef struct { size_t calls,fail,live,bytes; Allocation allocations[16384]; } Host;
static void *allocate(void *context,size_t n) {
    Host *h=context; void *p;
    if(++h->calls==h->fail) return NULL;
    CHECK(h->live<16384 && (p=malloc(n)));
    h->allocations[h->live++]=(Allocation){p,n}; h->bytes+=n; return p;
}
static void release(void *context,void *p) {
    Host *h=context; size_t i;
    for(i=h->live;i;i--) if(h->allocations[i-1].p==p) {
        h->bytes-=h->allocations[i-1].n; h->allocations[i-1]=h->allocations[--h->live]; free(p); return;
    }
    CHECK(0);
}
static LbAllocDomain *domain(Host *h) {
    LbSystemAllocator a={h,allocate,release}; LbAllocDomain *d;
    CHECK(lb_alloc_domain_create(&a,&d)==LB_ALLOC_OK); return d;
}
typedef struct { unsigned char data[16384]; size_t size; } Buffer;
static void byte(Buffer *b,unsigned n) { CHECK(b->size<sizeof(b->data)); b->data[b->size++]=(unsigned char)n; }
static void u32(Buffer *b,uint32_t v) { byte(b,v>>24); byte(b,v>>16); byte(b,v>>8); byte(b,v); }
static void data(Buffer *b,const void *p,size_t n) { CHECK(n<=sizeof(b->data)-b->size); if(n) memcpy(b->data+b->size,p,n); b->size+=n; }
static void tag(Buffer *b,unsigned t,unsigned v) {
    if(v<16) byte(b,(v<<4)|t);
    else if(v<2048) { byte(b,((v>>8)<<5)|8|t); byte(b,v); }
    else { byte(b,0x18|t); byte(b,v>>8); byte(b,v); }
}
static void chunk(Buffer *b,const char *name,const Buffer *content) {
    data(b,name,4); u32(b,(uint32_t)content->size); data(b,content->data,content->size);
    while(b->size%4) byte(b,0);
}
static Buffer literal(void) {
    Buffer b={0};
    /* {<<"opaque literal payload">>, [a,b,c]} using real ETF tags. */
    byte(&b,131); byte(&b,104); byte(&b,2); byte(&b,109); u32(&b,22); data(&b,"opaque literal payload",22);
    byte(&b,108); u32(&b,3);
    byte(&b,119); byte(&b,1); byte(&b,'a'); byte(&b,119); byte(&b,1); byte(&b,'b');
    byte(&b,119); byte(&b,1); byte(&b,'c'); byte(&b,106); return b;
}
static Buffer make_image(const Buffer *operand,const Buffer *etf,int compressed) {
    Buffer file={0},atoms={0},code={0},empty={0},exports={0},lit={0},plain={0};
    data(&file,"FOR1",4); u32(&file,0); data(&file,"BEAM",4);
    u32(&atoms,2); byte(&atoms,12); data(&atoms,"decoder_test",12); byte(&atoms,5); data(&atoms,"value",5);
    chunk(&file,"AtU8",&atoms);
    u32(&code,16); u32(&code,0); u32(&code,MAX_GENERIC_OPCODE); u32(&code,3); u32(&code,1);
    byte(&code,genop_label_1); tag(&code,TAG_u,1);
    byte(&code,genop_func_info_3); tag(&code,TAG_a,1); tag(&code,TAG_a,2); tag(&code,TAG_u,0);
    byte(&code,genop_label_1); tag(&code,TAG_u,2);
    byte(&code,genop_move_2); data(&code,operand->data,operand->size); tag(&code,TAG_x,0);
    byte(&code,genop_return_0); byte(&code,genop_int_code_end_0);
    chunk(&file,"Code",&code); chunk(&file,"StrT",&empty);
    u32(&empty,0); chunk(&file,"ImpT",&empty);
    u32(&exports,1); u32(&exports,2); u32(&exports,0); u32(&exports,2); chunk(&file,"ExpT",&exports);
    if(etf) {
        u32(&plain,1); u32(&plain,(uint32_t)etf->size); data(&plain,etf->data,etf->size);
        if(compressed) {
            unsigned char out[16384]; uLongf n=sizeof(out);
            CHECK(compress2(out,&n,plain.data,(uLong)plain.size,9)==Z_OK);
            u32(&lit,(uint32_t)plain.size); data(&lit,out,n);
        } else { u32(&lit,0); data(&lit,plain.data,plain.size); }
        chunk(&file,"LitT",&lit);
    }
    { Buffer size={0}; u32(&size,(uint32_t)file.size-8); memcpy(file.data+4,size.data,4); }
    return file;
}
static void name_is(LbAtomTable *t,Eterm atom,const char *name) {
    LbBeamBytes n; CHECK(lb_atoms_name(t,atom,&n)==LB_ATOM_OK && n.size==strlen(name) && !memcmp(n.data,name,n.size));
}
static void value_is_literal(LbBeamProgram *p,LbAtomTable *t) {
    Eterm value,*tuple,*bits,list; unsigned i;
    CHECK(lb_beam_program_literal(p,t,0,&value)==LB_BEAM_OK && is_literal_ptr(value));
    tuple=tuple_val(value); CHECK(arityval(tuple[0])==2);
    bits=boxed_val(tuple[1]); CHECK((bits[0]&_TAG_HEADER_MASK)==HEAP_BITS_SUBTAG && bits[1]==22*8);
    CHECK(!memcmp(bits+2,"opaque literal payload",22));
    list=tuple[2];
    for(i=0;i<3;++i) { char name[2]={(char)('a'+i),0}; CHECK(is_list(list)); name_is(t,CAR(list_val(list)),name); list=CDR(list_val(list)); }
    CHECK(is_nil(list));
}
static void selection(void) {
    LbBeamOp op={0}; LbBeamSelection selected;
    op.op=genop_move_2; op.arity=2; op.a=op.def_args;
    op.a[0]=(LbBeamArg){TAG_i,42}; op.a[1]=(LbBeamArg){TAG_x,0};
    CHECK(lb_beam_select_specific(&op,&selected)==LB_BEAM_OK);
    CHECK(selected.opcode==op_move_cr && selected.r_mask==2 && op.a[1].type==TAG_x);
    op.a[1].val=1;
    CHECK(lb_beam_select_specific(&op,&selected)==LB_BEAM_OK);
    CHECK(selected.opcode==op_move_cx && selected.r_mask==0);
    op.a[0].type=BEAM_NUM_TAGS;
    CHECK(lb_beam_select_specific(&op,&selected)==LB_BEAM_BAD_FORMAT);
    op.op=genop_int_func_start_5; op.arity=5;
    CHECK(lb_beam_select_specific(&op,&selected)==LB_BEAM_UNSUPPORTED); /* requires transform */
    op.op=NUM_GENERIC_OPS;
    CHECK(lb_beam_select_specific(&op,&selected)==LB_BEAM_INVALID_ARGUMENT);
}
static void reader_cases(void) {
    unsigned char bytes[]={0xd9,0x80,0,0,0,0,0,0,0};
    LbBeamReader r={bytes,sizeof(bytes),0}; LbTaggedNumber v;
    CHECK(lb_reader_tagged(&r,&v) && v.tag==TAG_i && v.size==8);
    bytes[0]=0xd8; r.pos=0;
    CHECK(lb_reader_tagged(&r,&v) && v.tag==TAG_o && v.size==8 && v.word==0);
    { unsigned char negative[]={0x19,0xff,0xfe}; r=(LbBeamReader){negative,3,0};
      CHECK(lb_reader_tagged(&r,&v) && v.tag==TAG_i && !v.size && v.word==-2); }
    { unsigned char recursive[40]; memset(recursive,0xf8,sizeof(recursive));
      r=(LbBeamReader){recursive,sizeof(recursive),0}; CHECK(!lb_reader_tagged(&r,&v)); }
}
static void failure_prefixes(const void *data,size_t size,int literal_witness) {
    Host h={0}; LbAllocDomain *d=domain(&h); LbAtomTable *table; LbBeamProgram *p;
    LbBeamError error; size_t before,steps,i,live,bytes,count;
    CHECK(lb_atoms_create(d,8192,&table)==LB_ATOM_OK); before=h.calls;
    CHECK(lb_beam_program_prepare(d,table,data,size,&p,&error)==LB_BEAM_OK);
    steps=h.calls-before; if(literal_witness) value_is_literal(p,table);
    CHECK(lb_atoms_destroy(table)==LB_ATOM_BUSY); lb_beam_program_destroy(p);
    CHECK(lb_atoms_destroy(table)==LB_ATOM_OK);
    CHECK(lb_atoms_create(d,8192,&table)==LB_ATOM_OK);
    live=h.live; bytes=h.bytes; count=lb_atoms_count(table);
    for(i=1;i<=steps;++i) {
        h.fail=h.calls+i;
        CHECK(lb_beam_program_prepare(d,table,data,size,&p,&error)==LB_BEAM_NO_MEMORY && !p);
        CHECK(h.live==live && h.bytes==bytes && lb_atoms_count(table)==count);
        name_is(table,make_atom(0),"false");
    }
    h.fail=0;
    CHECK(lb_beam_program_prepare(d,table,data,size,&p,&error)==LB_BEAM_OK);
    if(literal_witness) value_is_literal(p,table);
    lb_beam_program_destroy(p);
    CHECK(lb_atoms_destroy(table)==LB_ATOM_OK);
    CHECK(lb_alloc_domain_destroy(d)==LB_ALLOC_OK && !h.live && !h.bytes);
}
static void synthetic(void) {
    Host h={0}; LbAllocDomain *d=domain(&h); LbAtomTable *t,*peer;
    LbBeamProgram *p,*other; LbBeamError error; Buffer operand={0},file,etf=literal(); Eterm term;
    const LbBeamOp *op; size_t before,bytes,count,i;
    CHECK(lb_atoms_create(d,8192,&t)==LB_ATOM_OK && lb_atoms_create(d,8192,&peer)==LB_ATOM_OK);
    tag(&operand,TAG_z,4); tag(&operand,TAG_u,0); file=make_image(&operand,&etf,0);
    CHECK(lb_beam_program_prepare(d,t,file.data,file.size,&p,&error)==LB_BEAM_OK);
    CHECK(lb_beam_program_prepare(d,peer,file.data,file.size,&other,&error)==LB_BEAM_OK);
    op=lb_beam_program_ops(p); CHECK(op->op==genop_int_func_start_5 && op->a[1].type==TAG_n && op->a[1].val==NIL);
    name_is(t,(Eterm)op->a[2].val,"decoder_test");
    value_is_literal(p,t);
    CHECK(lb_beam_program_literal(p,peer,0,&term)==LB_BEAM_INVALID_ARGUMENT);
    lb_beam_program_destroy(p); CHECK(lb_atoms_destroy(t)==LB_ATOM_OK);
    value_is_literal(other,peer); lb_beam_program_destroy(other);
    CHECK(lb_atoms_destroy(peer)==LB_ATOM_OK);
    CHECK(lb_atoms_create(d,8192,&t)==LB_ATOM_OK);
    before=h.live; bytes=h.bytes; count=lb_atoms_count(t);
    { LbAtomTable *limited;
      CHECK(lb_atoms_create(d,lb_atoms_predefined_count(),&limited)==LB_ATOM_OK);
      CHECK(lb_beam_program_prepare(d,limited,file.data,file.size,&p,&error)==LB_BEAM_LIMIT && !p);
      CHECK(lb_atoms_count(limited)==lb_atoms_predefined_count() && lb_atoms_destroy(limited)==LB_ATOM_OK);
      CHECK(h.live==before && h.bytes==bytes); }
    /* Malformed/truncated and unsupported literals leave no names behind. */
    for(i=1;i<etf.size;++i) {
        Buffer bad=etf; bad.size=i; file=make_image(&operand,&bad,0);
        CHECK(lb_beam_program_prepare(d,t,file.data,file.size,&p,&error)!=LB_BEAM_OK && !p);
        CHECK(h.live==before && h.bytes==bytes && lb_atoms_count(t)==count);
    }
    { Buffer bad={0}; byte(&bad,131); byte(&bad,103); file=make_image(&operand,&bad,0);
      CHECK(lb_beam_program_prepare(d,t,file.data,file.size,&p,&error)==LB_BEAM_UNSUPPORTED && !p);
      CHECK(h.live==before && h.bytes==bytes && lb_atoms_count(t)==count); }
    /* True bignum marshalling, not integer wrapping or custom literal encoding. */
    operand=(Buffer){0}; byte(&operand,0xd9); byte(&operand,8); for(i=0;i<7;++i) byte(&operand,0);
    file=make_image(&operand,NULL,0);
    CHECK(lb_beam_program_prepare(d,t,file.data,file.size,&p,&error)==LB_BEAM_OK);
    CHECK(lb_beam_program_dynamic_literal_count(p)==1);
    CHECK(lb_beam_program_literal(p,t,-1,&term)==LB_BEAM_OK);
    CHECK(boxed_val(term)[0]==((Eterm)1<<_HEADER_ARITY_OFFS|POS_BIG_SUBTAG) && boxed_val(term)[1]==((Uint)1<<59));
    for(op=lb_beam_program_ops(p);op->op!=genop_move_2;op=op->next) CHECK(op->next);
    CHECK(op->a[0].type==TAG_q && op->a[0].val==-1); lb_beam_program_destroy(p);
    /* Type hints stripped by compiler tooling fall back to untyped registers. */
    operand=(Buffer){0}; tag(&operand,TAG_z,5); tag(&operand,TAG_x,5); tag(&operand,TAG_u,200);
    file=make_image(&operand,NULL,0);
    CHECK(lb_beam_program_prepare(d,t,file.data,file.size,&p,&error)==LB_BEAM_OK && lb_beam_program_type_fallback(p));
    for(op=lb_beam_program_ops(p);op->op!=genop_move_2;op=op->next) CHECK(op->next);
    CHECK(op->a[0].type==TAG_x && op->a[0].val==5); lb_beam_program_destroy(p);
    CHECK(lb_atoms_destroy(t)==LB_ATOM_OK && lb_alloc_domain_destroy(d)==LB_ALLOC_OK && !h.live);
    operand=(Buffer){0}; tag(&operand,TAG_z,4); tag(&operand,TAG_u,0);
    file=make_image(&operand,&etf,0); failure_prefixes(file.data,file.size,1);
    file=make_image(&operand,&etf,1); failure_prefixes(file.data,file.size,1);
}
static void literal_shapes(void) {
    Host h={0}; LbAllocDomain *d=domain(&h); LbAtomTable *t; LbBeamProgram *p; LbBeamError error;
    Buffer operand={0},etf,file; Eterm value; unsigned variant;
    CHECK(lb_atoms_create(d,8192,&t)==LB_ATOM_OK);
    tag(&operand,TAG_z,4); tag(&operand,TAG_u,0);
    for(variant=0;variant<5;++variant) {
        etf=(Buffer){0}; byte(&etf,131);
        switch(variant) {
        case 0: /* [7 | {}] */
            byte(&etf,108); u32(&etf,1); byte(&etf,97); byte(&etf,7); byte(&etf,104); byte(&etf,0); break;
        case 1: byte(&etf,107); byte(&etf,0); byte(&etf,3); data(&etf,"xyz",3); break;
        case 2: byte(&etf,70); byte(&etf,0x3f); byte(&etf,0xf0); for(unsigned i=0;i<6;++i) byte(&etf,0); break;
        case 3: byte(&etf,77); u32(&etf,1); byte(&etf,3); byte(&etf,0xff); break;
        case 4: byte(&etf,110); byte(&etf,0); byte(&etf,0); break;
        }
        file=make_image(&operand,&etf,0);
        CHECK(lb_beam_program_prepare(d,t,file.data,file.size,&p,&error)==LB_BEAM_OK);
        CHECK(lb_beam_program_literal(p,t,0,&value)==LB_BEAM_OK);
        switch(variant) {
        case 0: CHECK(is_list(value) && signed_val(CAR(list_val(value)))==7);
                CHECK(is_tuple(CDR(list_val(value))) && arityval(tuple_val(CDR(list_val(value)))[0])==0); break;
        case 1: CHECK(is_list(value) && signed_val(CAR(list_val(value)))=='x'); break;
        case 2: CHECK(boxed_val(value)[0]==(((Eterm)1<<_HEADER_ARITY_OFFS)|FLOAT_SUBTAG));
                CHECK(boxed_val(value)[1]==UINT64_C(0x3ff0000000000000)); break;
        case 3: CHECK(boxed_val(value)[1]==3 && ((unsigned char *)(boxed_val(value)+2))[0]==0xe0); break;
        case 4: CHECK(value==make_small(0)); break;
        }
        lb_beam_program_destroy(p);
    }
    CHECK(lb_atoms_destroy(t)==LB_ATOM_OK && lb_alloc_domain_destroy(d)==LB_ALLOC_OK && !h.live);
}
static void mutations(void) {
    Host h={0}; LbAllocDomain *d=domain(&h); LbAtomTable *t; LbBeamProgram *p; LbBeamError error;
    Buffer operand={0},term=literal(),original,changed; size_t i; uint32_t rng=0xface0123;
    tag(&operand,TAG_z,4); tag(&operand,TAG_u,0); original=make_image(&operand,&term,1);
    for(i=0;i<384;++i) {
        size_t live,bytes,count;
        CHECK(lb_atoms_create(d,8192,&t)==LB_ATOM_OK);
        live=h.live; bytes=h.bytes; count=lb_atoms_count(t); changed=original;
        rng=rng*1664525+1013904223; changed.data[rng%changed.size]^=(unsigned char)(1u<<(rng>>29));
        if(lb_beam_program_prepare(d,t,changed.data,changed.size,&p,&error)==LB_BEAM_OK) {
            lb_beam_program_destroy(p);
        } else CHECK(!p && h.live==live && h.bytes==bytes && lb_atoms_count(t)==count);
        CHECK(lb_atoms_destroy(t)==LB_ATOM_OK && h.live==1);
    }
    CHECK(lb_alloc_domain_destroy(d)==LB_ALLOC_OK && !h.live);
}
static void actual_file(const char *path) {
    FILE *f=fopen(path,"rb"); long n; void *data,*original;
    Host h={0}; LbAllocDomain *d=domain(&h); LbAtomTable *t; LbBeamProgram *p; LbBeamError error;
    const LbBeamOp *op; size_t functions=0,ops=0;
    CHECK(f && !fseek(f,0,SEEK_END)); n=ftell(f); CHECK(n>0 && !fseek(f,0,SEEK_SET));
    data=malloc((size_t)n); CHECK(data && fread(data,1,(size_t)n,f)==(size_t)n && !fclose(f));
    original=malloc((size_t)n); CHECK(original); memcpy(original,data,(size_t)n);
    CHECK(lb_atoms_create(d,8192,&t)==LB_ATOM_OK);
    if(lb_beam_program_prepare(d,t,data,(size_t)n,&p,&error)!=LB_BEAM_OK) {
        fprintf(stderr,"%s: stage=%s offset=%zu status=%d\n",path,error.stage,error.offset,error.status); abort();
    }
    memset(data,0,(size_t)n); free(data);
    for(op=lb_beam_program_ops(p);op;op=op->next) {
        ++ops;
        if(op->op==genop_int_func_start_5) {
            LbBeamBytes name; CHECK(lb_atoms_name(t,(Eterm)op->a[3].val,&name)==LB_ATOM_OK); ++functions;
            printf("FUNCTION %.*s/%ld\n",(int)name.size,name.data,(long)op->a[4].val);
        }
    }
    CHECK(functions==lb_beam_image_info(lb_beam_program_image(p))->function_count);
    printf("CORE_PROGRAM_FILE_OK functions=%zu operations=%zu literals=%zu lambdas=%zu execution=false\n",
           functions,ops,lb_beam_program_literal_count(p),lb_beam_program_lambda_count(p));
    if(strstr(path,"first_slice.beam")) value_is_literal(p,t);
    lb_beam_program_destroy(p); CHECK(lb_atoms_destroy(t)==LB_ATOM_OK);
    CHECK(lb_alloc_domain_destroy(d)==LB_ALLOC_OK && !h.live && !h.bytes);
    failure_prefixes(original,(size_t)n,0); free(original);
}
int main(int argc,char **argv) {
    CHECK(MAX_GENERIC_OPCODE==193 && gen_opc[genop_move_2].arity==2);
    reader_cases(); selection(); synthetic(); literal_shapes(); mutations();
    if(argc==2) actual_file(argv[1]); else CHECK(argc==1);
    puts("CORE_BEAM_PROGRAM_OK rollback=true owned_literals=true real_operands=true execution=false"); return 0;
}
