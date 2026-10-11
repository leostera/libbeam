/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#define _DEFAULT_SOURCE 1
#define _DARWIN_C_SOURCE 1
#include "bit_copy.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#define CHECK(test) do { if(!(test)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#test); abort(); } } while(0)

/* Deliberately independent, bit-at-a-time oracle. */
static void oracle(const unsigned char *source,size_t so,unsigned char *destination,size_t d,size_t bits)
{
    size_t i;
    for(i=0;i<bits;++i) {
        unsigned value=(source[(so+i)/8]>>(7-(so+i)%8))&1;
        unsigned mask=1u<<(7-(d+i)%8);
        destination[(d+i)/8]=(unsigned char)((destination[(d+i)/8]&~mask)|(value?mask:0));
    }
}
static void fill(unsigned char *bytes,size_t count,unsigned salt)
{
    size_t i;
    for(i=0;i<count;++i) bytes[i]=(unsigned char)((i*173+salt)^(i>>1));
}
static void offsets(void)
{
    unsigned char source[64],saved[64],actual[64],expected[64]; size_t so,d,n,pattern;
    for(pattern=0;pattern<3;++pattern) {
        fill(source,sizeof(source),(unsigned)(pattern*93)); memcpy(saved,source,sizeof(source));
        for(so=0;so<16;++so) for(d=0;d<16;++d) for(n=0;n<=257;++n) {
            fill(actual,sizeof(actual),(unsigned)(91+pattern)); memcpy(expected,actual,sizeof(actual));
            oracle(source,so,expected,d,n);
            CHECK(lb_bits_copy(source,sizeof(source),so,actual,sizeof(actual),d,n));
            CHECK(!memcmp(actual,expected,sizeof(actual)) && !memcmp(source,saved,sizeof(source)));
        }
    }
    /* Byte-copy fast path and a longer, unaligned copy. */
    {
        size_t size=65539; unsigned char *s=malloc(size),*a=malloc(size),*e=malloc(size);
        CHECK(s && a && e); fill(s,size,71); fill(a,size,99); memcpy(e,a,size);
        oracle(s,11,e,9,65536*8); CHECK(lb_bits_copy(s,size,11,a,size,9,65536*8));
        CHECK(!memcmp(a,e,size));
        CHECK(lb_bits_copy(s,size,0,a,size,0,size*8) && !memcmp(a,s,size));
        free(e); free(a); free(s);
    }
}
static void refused(void)
{
    unsigned char data[32],saved[32],other[32],other_saved[32];
    fill(data,sizeof(data),39); memcpy(saved,data,sizeof(data));
    fill(other,sizeof(other),87); memcpy(other_saved,other,sizeof(other));
    CHECK(lb_bits_copy(NULL,0,0,NULL,0,0,0));
    CHECK(!lb_bits_copy(NULL,1,0,data,sizeof(data),0,1));
    CHECK(!lb_bits_copy(data,sizeof(data),0,NULL,1,0,1));
    CHECK(!lb_bits_copy(data,sizeof(data),0,other,sizeof(other),SIZE_MAX,1));
    CHECK(!lb_bits_copy(data,sizeof(data),SIZE_MAX,other,sizeof(other),0,1));
    CHECK(!lb_bits_copy(other,sizeof(other),0,data,sizeof(data),0,SIZE_MAX));
    CHECK(!lb_bits_copy(other,SIZE_MAX,0,data,sizeof(data),0,1));
    CHECK(!lb_bits_copy(other,sizeof(other),0,data,SIZE_MAX,0,1));
    CHECK(!lb_bits_copy(other,sizeof(other),256,data,sizeof(data),0,1));
    CHECK(!lb_bits_copy(other,sizeof(other),0,data,sizeof(data),256,1));
    CHECK(!lb_bits_copy(other,sizeof(other),257,data,sizeof(data),0,0));
    CHECK(!lb_bits_copy(data,sizeof(data),0,data,sizeof(data),0,8));
    CHECK(!lb_bits_copy(data,sizeof(data),0,data,sizeof(data),1,1));
    CHECK(!lb_bits_copy(data,sizeof(data),0,data,sizeof(data),8,9));
    CHECK(!lb_bits_copy(data,sizeof(data),8,data,sizeof(data),0,9));
    CHECK(!memcmp(data,saved,sizeof(data)) && !memcmp(other,other_saved,sizeof(other)));
    /* Disjoint byte intervals in the same allocation are permitted. */
    CHECK(lb_bits_copy(data,sizeof(data),0,data,sizeof(data),8,8)); CHECK(data[1]==saved[0]);
    CHECK(lb_bits_copy(data,sizeof(data),256,data,sizeof(data),256,0));
}
static void guard_pages(void)
{
    long page=sysconf(_SC_PAGESIZE); unsigned char *source,*destination; size_t so,d,n;
    CHECK(page>0);
    source=mmap(NULL,(size_t)page*3,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);
    destination=mmap(NULL,(size_t)page*3,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);
    CHECK(source!=MAP_FAILED && destination!=MAP_FAILED);
    CHECK(!mprotect(source+page,(size_t)page,PROT_READ|PROT_WRITE));
    CHECK(!mprotect(destination+page,(size_t)page,PROT_READ|PROT_WRITE));
    for(so=0;so<8;++so) for(d=0;d<8;++d) for(n=0;n<=257;++n) {
        size_t sb=(so+n+7)/8,db=(d+n+7)/8;
        unsigned char *s=source+page*2-sb,*out=destination+page*2-db;
        unsigned char expected[40];
        fill(s,sb,187); fill(out,db,85); memcpy(expected,out,db);
        oracle(s,so,expected,d,n);
        CHECK(lb_bits_copy(s,sb,so,out,db,d,n));
        CHECK(!memcmp(out,expected,db));
    }
    CHECK(!munmap(source,(size_t)page*3) && !munmap(destination,(size_t)page*3));
}
int main(void)
{
    offsets(); refused(); guard_pages();
    puts("CORE_BIT_COPY_OK offsets=16 lengths=258 patterns=3 oracle=true outside_bits_preserved=true source_immutable=true invalid_unchanged=true overlap_refused=true exact_payload_guard_pages=true");
    return 0;
}
