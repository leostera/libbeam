/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#include "process_internal.h"
#include "writable.h"
#include <fenv.h>
#include <stdio.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"CHECK %s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while(0)
typedef struct { size_t calls,fail,live,bytes; } Memory;
typedef union { max_align_t alignment; size_t bytes; } Header;
static void *allocate(void *context,size_t bytes)
{
    Memory *m=context; if(++m->calls==m->fail) return NULL;
    Header *h=malloc(sizeof(*h)+bytes); if(!h) return NULL;
    h->bytes=bytes; ++m->live; m->bytes+=bytes; return h+1;
}
static void release(void *context,void *pointer)
{
    Memory *m=context; Header *h=(Header *)pointer-1;
    CHECK(m->live && m->bytes>=h->bytes); --m->live; m->bytes-=h->bytes; free(h);
}
static uint64_t reference(uint64_t used,uint64_t current)
{
    if(current<LB_BINARY_VHEAP_MIN_WORDS) current=LB_BINARY_VHEAP_MIN_WORDS;
    if(used/3>current/4) {
        while(used/3>current/4) {
            volatile double input=(double)current;
            volatile double product=input*1.618;
            /* Do not execute native C's out-of-range float->Uint64 cast. */
            if(product>=0x1p64) return current;
            uint64_t next=(uint64_t)product; CHECK(next>current); current=next;
        }
        return current;
    }
    if(used<current/4) current>>=1;
    return current<LB_BINARY_VHEAP_MIN_WORDS?LB_BINARY_VHEAP_MIN_WORDS:current;
}
static uint64_t random_word(uint64_t *state)
{
    *state^=*state<<13; *state^=*state>>7; *state^=*state<<17; return *state;
}
static void arithmetic(void)
{
    fenv_t saved; CHECK(!fegetenv(&saved) && !fesetenv(FE_DFL_ENV));
    uint64_t state=UINT64_C(0x123476589abcdef),used[256],current[256],expected[256];
    for(size_t i=0;i<20000;++i) {
        uint64_t u=random_word(&state),c=random_word(&state);
        if(i&1) c&=(UINT64_C(1)<<40)-1;
        CHECK(lb_binary_vheap_next(u,c)==reference(u,c));
        uint64_t incoming=random_word(&state),threshold=random_word(&state);
        CHECK(lb_binary_vheap_pressure(u,incoming,threshold)==((unsigned __int128)u+incoming>=threshold));
    }
    for(unsigned bit=16;bit<64;++bit) for(int delta=-32;delta<=32;++delta) {
        uint64_t c=(UINT64_C(1)<<bit)+(uint64_t)delta;
        CHECK(lb_binary_vheap_next(UINT64_MAX,c)==reference(UINT64_MAX,c));
        for(int edge=-3;edge<=3;++edge) {
            uint64_t u=c/4+(uint64_t)edge;
            CHECK(lb_binary_vheap_next(u,c)==reference(u,c));
            u=(c/4)*3+(uint64_t)edge;
            CHECK(lb_binary_vheap_next(u,c)==reference(u,c));
        }
    }
    /* An exact 1618/1000 implementation would be one word too small here. */
    CHECK(lb_binary_vheap_next(UINT64_C(9007199254740960),UINT64_C(9007199254740960))==UINT64_C(14573648394170874));
    CHECK(lb_binary_vheap_next(UINT64_MAX,UINT64_MAX)==UINT64_MAX);
    CHECK(lb_binary_vheap_next(0,0)==LB_BINARY_VHEAP_MIN_WORDS);
    CHECK(!lb_binary_vheap_pressure(2,2,5) && lb_binary_vheap_pressure(2,3,5));
    CHECK(lb_binary_vheap_pressure(UINT64_MAX,1,UINT64_MAX));
    for(size_t i=0;i<256;++i) {
        used[i]=random_word(&state); current[i]=random_word(&state);
        expected[i]=reference(used[i],current[i]);
    }
    const int modes[]={FE_TONEAREST,FE_UPWARD,FE_DOWNWARD,FE_TOWARDZERO};
    for(size_t mode=0;mode<4;++mode) {
        CHECK(!fesetround(modes[mode]) && !feclearexcept(FE_ALL_EXCEPT) && !feraiseexcept(FE_DIVBYZERO));
        int flags=fetestexcept(FE_ALL_EXCEPT);
        for(size_t i=0;i<256;++i) CHECK(lb_binary_vheap_next(used[i],current[i])==expected[i]);
        CHECK(fegetround()==modes[mode] && fetestexcept(FE_ALL_EXCEPT)==flags);
    }
    CHECK(!fesetenv(&saved));
    puts("CORE_BINARY_VHEAP_ARITHMETIC_OK native_binary64=true random_vectors=20000 rounding_boundaries=true pressure_overflow=true fenv_preserved=true allocation_free=true");
}
static void construction(const LbCodeEntry *entry,Memory *m)
{
    size_t bytes=LB_BINARY_VHEAP_MIN_WORDS*sizeof(Eterm),live=m->live,balance=m->bytes;
    for(size_t fail=1;fail<=2;++fail) {
        LbProcess *p; Eterm input=NIL,result=am_false; LbBitWriter writer={NULL,123,456};
        CHECK(lb_process_create(entry,&input,1,4096,&p)==LB_PROCESS_READY); p->heap_reserved=7;
        Eterm *heap=p->heap; size_t owned=m->live,owned_bytes=m->bytes;
        CHECK(p->bin_vheap_sz==LB_BINARY_VHEAP_MIN_WORDS && (size_t)(p->stop-p->htop)>100);
        m->fail=m->calls+fail;
        CHECK(lb_binary_begin(p,bytes*8,3,&writer,&result)==LB_WRITABLE_NO_MEMORY);
        CHECK(p->heap==heap && !p->collections && p->heap_reserved==7 && p->bin_vheap_sz==LB_BINARY_VHEAP_MIN_WORDS);
        CHECK(result==am_false && !writer.data && writer.capacity==123 && writer.offset==456);
        CHECK(m->live==owned && m->bytes==owned_bytes); m->fail=0;
        CHECK(lb_binary_begin(p,bytes*8,3,&writer,&result)==LB_WRITABLE_OK);
        CHECK(p->collections==1 && p->heap!=heap && p->heap_reserved==7);
        CHECK(lb_bits_put_integer(&writer,make_small(-1),bytes*8,LB_BITS_BIG_ENDIAN)==LB_BITS_OK && writer.offset==bytes*8);
        p->reg[0]=result; CHECK(lb_process_run(p,10000,10000)==LB_PROCESS_DONE);
        uint64_t before=p->bin_vheap_sz,wanted=lb_binary_vheap_next(p->off_heap.overhead,before);
        CHECK(wanted>before); heap=p->heap; m->fail=m->calls+1;
        CHECK(lb_process_collect(p,0)==LB_PROCESS_NO_MEMORY && p->heap==heap && p->bin_vheap_sz==before);
        m->fail=0; CHECK(lb_process_collect(p,0)==LB_PROCESS_DONE && p->bin_vheap_sz==wanted);
        LbBitstringView view; CHECK(lb_bitstring_view(lb_process_result(p),&view) && view.bit_size==bytes*8);
        CHECK(!view.bit_offset); for(size_t i=0;i<bytes;++i) CHECK(view.data[i]==255);
        p->reg[0]=NIL; CHECK(lb_process_collect(p,0)==LB_PROCESS_DONE);
        CHECK(!p->off_heap.first && !p->off_heap.overhead && p->bin_vheap_sz==LB_BINARY_VHEAP_MIN_WORDS);
        lb_process_destroy(p); CHECK(m->live==live && m->bytes==balance);
    }
    /* Spare writable capacity is retained and counted, even with zero apparent
     * length. Pin/migration does not secretly shrink or discard that charge. */
    LbProcess *p; Eterm input=NIL;
    CHECK(lb_process_create(entry,&input,1,4096,&p)==LB_PROCESS_READY);
    CHECK(lb_writable_init(p,bytes,&p->reg[0])==LB_WRITABLE_OK && !p->collections);
    CHECK(p->writable.overhead==LB_BINARY_VHEAP_MIN_WORDS && !p->off_heap.overhead);
    CHECK(lb_process_collect(p,0)==LB_PROCESS_READY);
    uint64_t threshold=p->bin_vheap_sz; CHECK(threshold>LB_BINARY_VHEAP_MIN_WORDS);
    CHECK(lb_bitstring_pin(p->reg[0]) && lb_process_collect(p,0)==LB_PROCESS_READY);
    CHECK(!p->writable.first && p->off_heap.overhead==LB_BINARY_VHEAP_MIN_WORDS);
    CHECK(p->bin_vheap_sz==lb_binary_vheap_next(LB_BINARY_VHEAP_MIN_WORDS,threshold));
    p->reg[0]=NIL; CHECK(lb_process_collect(p,0)==LB_PROCESS_READY);
    CHECK(!p->off_heap.first && p->bin_vheap_sz==LB_BINARY_VHEAP_MIN_WORDS);
    lb_process_destroy(p); CHECK(m->live==live && m->bytes==balance);
    puts("CORE_BINARY_VHEAP_EXECUTION_OK pressure_without_heap_exhaustion=true failure_prefixes=2 threshold_rollback=true same_process_retry=true identity_execution=true moving_gc=true retained_spare_capacity=true physical_retirement=true guest_construction=false");
}
static void fixture(const char *path)
{
    FILE *f=fopen(path,"rb"); CHECK(f && !fseek(f,0,SEEK_END)); long size=ftell(f); CHECK(size>0 && !fseek(f,0,SEEK_SET));
    unsigned char *data=malloc((size_t)size); CHECK(data && fread(data,1,(size_t)size,f)==(size_t)size && !fclose(f));
    Memory m={0}; LbSystemAllocator callbacks={&m,allocate,release}; LbEngine *engine; LbCodeSpace *space; LbCodeModule *module;
    LbCodeEntry *entry; LbBeamError error; Eterm function;
    CHECK(lb_engine_create(&callbacks,&engine)==LB_ENGINE_OK && lb_code_space_create(engine,&space)==LB_CODE_OK);
    CHECK(lb_code_load(space,data,(size_t)size,&module,&error)==LB_CODE_OK); free(data);
    CHECK(lb_atoms_find(space->atoms,"identity",8,&function)==LB_ATOM_OK);
    CHECK(lb_code_entry_acquire(space,lb_code_module_name(module),function,1,&entry)==LB_CODE_OK);
    construction(entry,&m); lb_code_entry_release(entry); CHECK(lb_code_unload(module)==LB_CODE_OK);
    CHECK(lb_code_space_destroy(space)==LB_CODE_OK && lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine);
    CHECK(!m.live && !m.bytes);
}
int main(int argc,char **argv)
{
    arithmetic(); if(argc==2) fixture(argv[1]); else CHECK(argc==1); return 0;
}
