/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#define _GNU_SOURCE 1
#define _DARWIN_C_SOURCE 1
#include "bit_writer.h"
#include "binary.h"
#include "big.h"
#include <dlfcn.h>
#include <fenv.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#if defined(__clang__)
#pragma STDC FENV_ACCESS ON
#endif
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); abort(); } } while(0)
static int fail_get,fail_set;
int fegetenv(fenv_t *env)
{
    int (*real)(fenv_t *)=(int (*)(fenv_t *))dlsym(RTLD_NEXT,"fegetenv"); CHECK(real);
    if(fail_get) { fail_get=0; return -1; } return real(env);
}
int fesetenv(const fenv_t *env)
{
    int (*real)(const fenv_t *)=(int (*)(const fenv_t *))dlsym(RTLD_NEXT,"fesetenv"); CHECK(real);
    if(fail_set && !--fail_set) return -1; return real(env);
}
static Eterm floating(Eterm *storage,double value)
{
    storage[0]=_make_header(1,FLOAT_SUBTAG); memcpy(storage+1,&value,sizeof(value)); return make_boxed(storage);
}
static void copy_oracle(const unsigned char *src,size_t start,unsigned char *dst,size_t off,size_t bits)
{
    size_t i;
    for(i=0;i<bits;++i) {
        unsigned bit=(src[(start+i)/8]>>(7-(start+i)%8))&1,mask=1u<<(7-(off+i)%8);
        dst[(off+i)/8]=(unsigned char)((dst[(off+i)/8]&~mask)|(bit?mask:0));
    }
}
static void binaries(void)
{
    LbAllocDomain *domain; LbOffHeap chain={0}; Eterm words[32],*top=words,term,small;
    unsigned char source[80],actual[80],expected[80],saved[80],untouched[80]; size_t i,so,off,n;
    CHECK(lb_alloc_domain_create(NULL,&domain)==LB_ALLOC_OK);
    for(i=0;i<sizeof(source);++i) source[i]=(unsigned char)(i*173+71);
    CHECK(lb_bitstring_build(domain,&chain,&top,source,sizeof(source)*8,0,&term)==LB_ALLOC_OK);
    CHECK(lb_bitstring_build(domain,&chain,&top,source,37,0,&small)==LB_ALLOC_OK);
    memcpy(saved,source,sizeof(source)); memset(untouched,0xa5,sizeof(untouched));
    for(so=0;so<16;++so) for(off=0;off<16;++off) for(n=0;n<=257;++n) {
        LbSubBits *sub=(LbSubBits *)boxed_val(term); LbBitWriter w;
        sub->start=so; sub->end=so+n;
        memset(actual,0xa5,sizeof(actual)); memset(expected,0xa5,sizeof(expected));
        copy_oracle(source,so,expected,off,n);
        CHECK(lb_bit_writer_init(&w,actual,sizeof(actual))==LB_BITS_OK); w.offset=off;
        CHECK(lb_bits_put_binary(&w,term,n)==LB_BITS_OK && w.offset==off+n);
        CHECK(!memcmp(actual,expected,sizeof(actual)));
        memset(actual,0xa5,sizeof(actual)); w.offset=off;
        CHECK(lb_bits_put_binary_all(&w,term,1)==LB_BITS_OK && !memcmp(actual,expected,sizeof(actual)));
        memset(actual,0xa5,sizeof(actual)); w.offset=off;
        if(n%8) CHECK(lb_bits_put_binary_all(&w,term,8)==LB_BITS_BADARG && w.offset==off && !memcmp(actual,untouched,sizeof(actual)));
        else CHECK(lb_bits_put_binary_all(&w,term,8)==LB_BITS_OK && !memcmp(actual,expected,sizeof(actual)));
    }
    { LbSubBits *sub=(LbSubBits *)boxed_val(term); LbBitWriter w;
      sub->start=0; sub->end=256;
      memset(actual,0xa5,sizeof(actual)); CHECK(lb_bit_writer_init(&w,actual,sizeof(actual))==LB_BITS_OK);
      CHECK(lb_bits_put_binary_all(&w,term,256)==LB_BITS_OK && w.offset==256 && !memcmp(actual,source,32));
      sub->end=255; w.offset=0; memcpy(expected,actual,sizeof(actual));
      CHECK(lb_bits_put_binary_all(&w,term,256)==LB_BITS_BADARG && !w.offset && !memcmp(actual,expected,sizeof(actual))); }
    for(off=0;off<16;++off) {
        LbBitWriter w;
        memset(actual,0xa5,sizeof(actual)); memset(expected,0xa5,sizeof(expected));
        CHECK(lb_bit_writer_init(&w,actual,sizeof(actual))==LB_BITS_OK); w.offset=off;
        CHECK(lb_bits_put_string(&w,source,65)==LB_BITS_OK); copy_oracle(source,0,expected,off,520);
        CHECK(!memcmp(actual,expected,sizeof(actual)));
        memset(actual,0xa5,sizeof(actual)); memset(expected,0xa5,sizeof(expected)); w.offset=off;
        CHECK(lb_bits_put_binary(&w,small,37)==LB_BITS_OK); copy_oracle(source,0,expected,off,37);
        CHECK(!memcmp(actual,expected,sizeof(actual)));
    }
    { LbBitWriter w,old; memset(actual,0xa5,sizeof(actual)); memcpy(expected,actual,sizeof(actual));
      CHECK(lb_bit_writer_init(&w,actual,sizeof(actual))==LB_BITS_OK); old=w;
      CHECK(lb_bits_put_binary(&w,NIL,0)==LB_BITS_BADARG);
      CHECK(lb_bits_put_binary(&w,small,38)==LB_BITS_BADARG);
      CHECK(lb_bits_put_binary_all(&w,small,0)==LB_BITS_INVALID);
      CHECK(lb_bits_put_binary_all(&w,small,257)==LB_BITS_INVALID);
      CHECK(lb_bits_put_binary_all(&w,small,8)==LB_BITS_BADARG);
      CHECK(lb_bits_put_string(&w,NULL,1)==LB_BITS_INVALID);
      CHECK(lb_bits_put_string(&w,source,SIZE_MAX)==LB_BITS_INVALID);
      CHECK(!memcmp(actual,expected,sizeof(actual)) && !memcmp(&w,&old,sizeof(w)));
      w.offset=1; old=w;
      CHECK(lb_bits_put_string(&w,actual,1)==LB_BITS_INVALID);
      CHECK(!memcmp(actual,expected,sizeof(actual)) && !memcmp(&w,&old,sizeof(w)));
      w.offset=0;
      CHECK(lb_bits_put_string(&w,actual,1)==LB_BITS_OK && w.offset==8 && !memcmp(actual,expected,sizeof(actual)));
      CHECK(lb_bits_put_string(&w,NULL,0)==LB_BITS_OK);
      w.offset=w.capacity*8; old=w;
      CHECK(lb_bits_put_string(&w,source,1)==LB_BITS_NO_SPACE);
      CHECK(lb_bits_put_binary(&w,small,1)==LB_BITS_NO_SPACE && !memcmp(&w,&old,sizeof(w)));
    }
    { LbBitstringView view; LbBitWriter w,old; unsigned char before[5];
      CHECK(lb_bitstring_view(small,&view)); memcpy(before,view.data,sizeof(before));
      CHECK(lb_bit_writer_init(&w,(unsigned char *)view.data,sizeof(before))==LB_BITS_OK);
      CHECK(lb_bits_put_binary_all(&w,small,1)==LB_BITS_OK && w.offset==37 && !memcmp(view.data,before,sizeof(before)));
      w.offset=1; old=w;
      CHECK(lb_bits_put_binary(&w,small,36)==LB_BITS_INVALID && !memcmp(&w,&old,sizeof(w)) && !memcmp(view.data,before,sizeof(before)));
    }
    { LbBitstringView view; CHECK(lb_bitstring_view(term,&view));
      CHECK(!memcmp(view.data,source,sizeof(source)) && !memcmp(source,saved,sizeof(source))); }
    lb_offheap_clear(&chain); CHECK(lb_alloc_domain_destroy(domain)==LB_ALLOC_OK);
}
static uint64_t encoded(Eterm value,size_t width)
{
    unsigned char data[8]={0}; LbBitWriter w; LbBitFloatResult result; uint64_t bits=0; size_t i;
    CHECK(lb_bit_writer_init(&w,data,sizeof(data))==LB_BITS_OK);
    result=lb_bits_put_float(&w,value,width,LB_BITS_BIG_ENDIAN);
    CHECK(result.status==LB_BITS_OK && result.error==LB_FLOAT_NONE && w.offset==width);
    for(i=0;i<width/8;++i) bits=(bits<<8)|data[i]; return bits;
}
static double half_value(unsigned bits)
{
    unsigned exponent=bits>>10,fraction=bits&1023;
    return exponent?ldexp(1024.0+fraction,(int)exponent-25):ldexp((double)fraction,-24);
}
static void rounding(void)
{
    Eterm storage[2]; unsigned h,sign; fenv_t previous;
    CHECK(!fegetenv(&previous) && !fesetenv(FE_DFL_ENV));
    for(h=0;h<0x7bff;++h) {
        double low=half_value(h),high=half_value(h+1),middle=(low+high)/2;
        const double values[]={nextafter(middle,0),middle,nextafter(middle,INFINITY)};
        unsigned i;
        for(i=0;i<3;++i) for(sign=0;sign<2;++sign) {
            double value=sign?-values[i]:values[i]; unsigned want=i==0?h:i==2?h+1:(h&1)?h+1:h;
            CHECK(encoded(floating(storage,value),16)==((sign?0x8000u:0)|want));
        }
    }
    { const uint32_t fractions[]={0,1,2,0x3fffff,0x7ffffe}; unsigned exponent,f;
      for(exponent=0;exponent<255;++exponent) for(f=0;f<5;++f) {
          uint32_t low_bits=(exponent<<23)|fractions[f],high_bits=low_bits+1;
          float lo,hi; double middle; unsigned side;
          memcpy(&lo,&low_bits,4); memcpy(&hi,&high_bits,4); middle=((double)lo+(double)hi)/2;
          for(side=0;side<3;++side) for(sign=0;sign<2;++sign) {
              double value=side==0?nextafter(middle,0):side==2?nextafter(middle,INFINITY):middle;
              uint32_t want=side==0?low_bits:side==2?high_bits:(low_bits&1)?high_bits:low_bits;
              CHECK(encoded(floating(storage,sign?-value:value),32)==((sign?UINT32_C(0x80000000):0)|want));
          }
      }
    }
    CHECK(encoded(floating(storage,65520.0),16)==0x7c00);
    CHECK(encoded(floating(storage,nextafter(65520.0,0)),16)==0x7bff);
    CHECK(encoded(floating(storage,-0.0),16)==0x8000);
    CHECK(encoded(floating(storage,-DBL_TRUE_MIN),16)==0x8000);
    CHECK(encoded(floating(storage,0x1.ffffffp127),32)==UINT32_C(0x7f800000));
    CHECK(encoded(floating(storage,nextafter(0x1.ffffffp127,0)),32)==UINT32_C(0x7f7fffff));
    CHECK(encoded(floating(storage,-DBL_MAX),32)==UINT32_C(0xff800000));
    CHECK(encoded(make_small(((Sint)1<<58)+((Sint)1<<34)+1),32)==UINT32_C(0x5c800001));
    CHECK(encoded(lb_small_to_big(((Sint)1<<60)+((Sint)1<<36)+1,storage),32)==UINT32_C(0x5d800000));
    { Eterm words[17]={0}; words[0]=_make_header(16,POS_BIG_SUBTAG); words[16]=UINT64_C(1)<<63;
      CHECK(encoded(make_boxed(words),64)==UINT64_C(0x7fe0000000000000));
      CHECK(encoded(make_boxed(words),32)==UINT32_C(0x7f800000));
      words[0]=_make_header(16,NEG_BIG_SUBTAG); CHECK(encoded(make_boxed(words),16)==0xfc00); }
    CHECK(!fesetenv(&previous));
}
static void environment(void)
{
    Eterm storage[2],huge[18]={0}; unsigned char data[16],saved[16]; LbBitWriter w,old;
    fenv_t previous; LbBitFloatResult result; int failure;
    CHECK(!fegetenv(&previous));
    for(failure=0;failure<4;++failure) {
        CHECK(!fesetenv(FE_DFL_ENV) && !fesetround(FE_UPWARD) && !feraiseexcept(FE_DIVBYZERO));
        memset(data,0xa5,sizeof(data)); memcpy(saved,data,sizeof(data));
        CHECK(lb_bit_writer_init(&w,data,sizeof(data))==LB_BITS_OK); w.offset=3; old=w;
        fail_get=failure==1; fail_set=failure==2?1:failure==3?2:0;
        result=lb_bits_put_float(&w,floating(storage,1.0+0x1p-11),16,LB_BITS_BIG_ENDIAN);
        if(failure) CHECK(result.status==LB_BITS_ENVIRONMENT && !memcmp(data,saved,sizeof(data)) && !memcmp(&w,&old,sizeof(w)));
        else {
            unsigned char expect[16],half[]={0x3c,0}; memcpy(expect,saved,sizeof(expect)); copy_oracle(half,0,expect,3,16);
            CHECK(result.status==LB_BITS_OK && !memcmp(data,expect,sizeof(data)));
            CHECK(fegetround()==FE_UPWARD && fetestexcept(FE_ALL_EXCEPT)==FE_DIVBYZERO);
        }
    }
    CHECK(!fesetenv(&previous));
    memset(data,0xa5,sizeof(data)); memcpy(saved,data,sizeof(data));
    CHECK(lb_bit_writer_init(&w,data,sizeof(data))==LB_BITS_OK); old=w;
    result=lb_bits_put_float(&w,NIL,0,LB_BITS_BIG_ENDIAN); CHECK(result.status==LB_BITS_BADARG && result.error==LB_FLOAT_SIZE);
    result=lb_bits_put_float(&w,NIL,16,LB_BITS_BIG_ENDIAN); CHECK(result.status==LB_BITS_BADARG && result.error==LB_FLOAT_TYPE);
    huge[0]=_make_header(17,POS_BIG_SUBTAG); huge[17]=1;
    result=lb_bits_put_float(&w,make_boxed(huge),64,LB_BITS_BIG_ENDIAN); CHECK(result.status==LB_BITS_BADARG && result.error==LB_FLOAT_NO_FLOAT);
    result=lb_bits_put_float(&w,floating(storage,INFINITY),64,LB_BITS_BIG_ENDIAN); CHECK(result.status==LB_BITS_BADARG && result.error==LB_FLOAT_TYPE);
    CHECK(!memcmp(data,saved,sizeof(data)) && !memcmp(&w,&old,sizeof(w)));
    { Eterm original[2]; Eterm term=floating(storage,1.0); memcpy(original,storage,sizeof(storage));
      CHECK(lb_bit_writer_init(&w,(unsigned char *)storage,sizeof(storage))==LB_BITS_OK);
      CHECK(lb_bits_put_float(&w,term,64,LB_BITS_BIG_ENDIAN).status==LB_BITS_INVALID);
      CHECK(!memcmp(original,storage,sizeof(storage))); }
}
static void alignment(void)
{
    const double values[]={0,-0.0,1.0,-3.141592653589793,DBL_TRUE_MIN,DBL_MAX,65520.0,0x1.ffffffp127};
    long page=sysconf(_SC_PAGESIZE); unsigned char *map; size_t v,offset,width,order; Eterm storage[2];
    CHECK(page>0); map=mmap(NULL,(size_t)page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    CHECK(map!=MAP_FAILED && !mprotect(map+page,(size_t)page,PROT_NONE));
    for(v=0;v<sizeof(values)/sizeof(*values);++v) for(offset=0;offset<16;++offset)
        for(width=16;width<=64;width*=2) for(order=0;order<2;++order) {
            size_t bytes=(offset+width+7)/8,i; unsigned char raw[8],expected[16],*out=map+page-bytes;
            uint64_t bits=encoded(floating(storage,values[v]),width); LbBitWriter w;
            for(i=0;i<width/8;++i) raw[i]=(unsigned char)(bits>>((order?i:width/8-1-i)*8));
            memset(out,0xa5,bytes); memset(expected,0xa5,bytes); copy_oracle(raw,0,expected,offset,width);
            CHECK(lb_bit_writer_init(&w,out,bytes)==LB_BITS_OK); w.offset=offset;
            CHECK(lb_bits_put_float(&w,floating(storage,values[v]),width,(LbBitEndian)order).status==LB_BITS_OK);
            CHECK(w.offset==offset+width && !memcmp(out,expected,bytes));
        }
    CHECK(!munmap(map,(size_t)page*2));
}
static void vectors(void)
{
    const uint64_t raw[]={0,UINT64_C(0x8000000000000000),UINT64_C(0x3ff0000000000000),UINT64_C(0x3ff0020000000001),
        UINT64_C(0xbff0020000000001),1,UINT64_C(0x7fefffffffffffff),UINT64_C(0x40effe0000000000),UINT64_C(0x40effdffffffffff)};
    const size_t widths[]={0,8,16,32,64,128},offsets[]={0,1,7,8,15};
    Eterm floats[9][2],big[2],huge[18]={0},values[14]; size_t i,j,k,e,n;
    for(i=0;i<9;++i) { floats[i][0]=_make_header(1,FLOAT_SUBTAG); memcpy(floats[i]+1,raw+i,8); values[i]=make_boxed(floats[i]); }
    values[9]=make_small(((Sint)1<<58)+((Sint)1<<34)+1);
    values[10]=lb_small_to_big(((Sint)1<<60)+((Sint)1<<36)+1,big);
    huge[0]=_make_header(17,POS_BIG_SUBTAG); huge[17]=1; values[11]=make_boxed(huge);
    values[12]=NIL; values[13]=make_small(-1);
    for(i=0;i<14;++i) for(j=0;j<6;++j) for(k=0;k<5;++k) for(e=0;e<2;++e) {
        LbBitWriter w; unsigned char data[20]; LbBitFloatResult result; memset(data,0xa5,sizeof(data));
        CHECK(lb_bit_writer_init(&w,data,sizeof(data))==LB_BITS_OK); w.offset=offsets[k];
        result=lb_bits_put_float(&w,values[i],widths[j],(LbBitEndian)e);
        if(result.status==LB_BITS_BADARG) puts("badarg");
        else { CHECK(result.status==LB_BITS_OK); for(n=0;n<(w.offset+7)/8;++n) printf("%02x",data[n]); putchar('\n'); }
    }
}
int main(int argc,char **argv)
{
    if(argc==2 && !strcmp(argv[1],"--reference")) { vectors(); return 0; }
    CHECK(argc==1); binaries(); rounding(); environment(); alignment();
    puts("CORE_BIT_SEGMENTS_OK binary_string=true source_offsets=16 destination_offsets=16 lengths=258 half_midpoints=true float32_midpoints=true small_direct_float32=true overflow_infinity_bytes=true fenv_restored=true fenv_failure_unchanged=true guard_pages=true instruction_admission=false");
    return 0;
}
