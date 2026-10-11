/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#define _DEFAULT_SOURCE 1
#define _DARWIN_C_SOURCE 1
#include "bit_writer.h"
#include "big.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); abort(); } } while(0)

static Eterm big(Eterm *storage,Uint low,Uint high,int negative)
{
    storage[0]=_make_header(2,negative?NEG_BIG_SUBTAG:POS_BIG_SUBTAG);
    storage[1]=low; storage[2]=high; return make_boxed(storage);
}
static void set_bit(unsigned char *bytes,size_t bit,unsigned value)
{
    unsigned mask=1u<<(7-bit%8);
    bytes[bit/8]=(unsigned char)((bytes[bit/8]&~mask)|(value?mask:0));
}
/* Independent bit-at-a-time two's-complement oracle, including sign extension
 * beyond the source width and a final short little-endian byte group. */
static void oracle(unsigned char *out,size_t offset,Eterm value,size_t bits,LbBitEndian order)
{
    Uint small,*digits; size_t count,first=0,i,j; int negative;
    if(is_small(value)) {
        Sint n=signed_val(value); negative=n<0; small=negative?0-(Uint)n:(Uint)n;
        digits=&small; count=1;
    } else {
        digits=boxed_val(value)+1; count=lb_big_size(value);
        negative=(*boxed_val(value)&_TAG_HEADER_MASK)==NEG_BIG_SUBTAG;
    }
    if(negative) {
        while(first<count*64 && !((digits[first/64]>>(first%64))&1)) ++first;
    }
    for(i=0;i<bits;++i) {
        unsigned bit;
        if(order==LB_BITS_BIG_ENDIAN) j=bits-1-i;
        else if(i<bits/8*8) j=i/8*8+7-i%8;
        else j=bits/8*8+bits%8-1-i%8;
        bit=j<count*64?(unsigned)((digits[j/64]>>(j%64))&1):0;
        if(negative) bit=j<first?0:j==first?1:!bit;
        set_bit(out,offset+i,bit);
    }
}
static void check_integer(Eterm value)
{
    unsigned char actual[64],expected[64]; size_t offset,bits; int order;
    for(offset=0;offset<16;++offset) for(bits=0;bits<=257;++bits) for(order=0;order<2;++order) {
        LbBitWriter writer;
        memset(actual,0xa5,sizeof(actual)); memset(expected,0xa5,sizeof(expected));
        CHECK(lb_bit_writer_init(&writer,actual,sizeof(actual))==LB_BITS_OK); writer.offset=offset;
        oracle(expected,offset,value,bits,(LbBitEndian)order);
        CHECK(lb_bits_put_integer(&writer,value,bits,(LbBitEndian)order)==LB_BITS_OK);
        CHECK(writer.offset==offset+bits && !memcmp(actual,expected,sizeof(actual)));
    }
}
static void integers(void)
{
    const Sint smalls[]={0,1,-1,127,-128,255,-256,MIN_SMALL,MAX_SMALL};
    Eterm storage[3],saved[3]; size_t i;
    for(i=0;i<sizeof(smalls)/sizeof(*smalls);++i) check_integer(make_small(smalls[i]));
    for(i=0;i<2;++i) {
        Eterm value=big(storage,0,1,(int)i); memcpy(saved,storage,sizeof(storage));
        check_integer(value); CHECK(!memcmp(saved,storage,sizeof(storage)));
        value=big(storage,UINT64_MAX,UINT64_MAX,(int)i); memcpy(saved,storage,sizeof(storage));
        check_integer(value); CHECK(!memcmp(saved,storage,sizeof(storage)));
    }
    /* Independently use modulo-2^128 unsigned values for random signed cases. */
    {
        Uint state=UINT64_C(0x794cb8bd2); size_t trial;
        for(trial=0;trial<10000;++trial) {
            unsigned char actual[32],expected[32]; LbBitWriter writer; unsigned __int128 value;
            size_t offset=trial%16,bits=trial%193,k; Eterm term;
            state=state*UINT64_C(6364136223846793005)+1; storage[1]=state;
            state=state*UINT64_C(6364136223846793005)+1; storage[2]=state|1;
            term=big(storage,storage[1],storage[2],(int)(trial&1));
            value=((unsigned __int128)storage[2]<<64)|storage[1];
            if(trial&1) value=0-value;
            memset(actual,0xa5,sizeof(actual)); memset(expected,0xa5,sizeof(expected));
            for(k=0;k<bits;++k) {
                size_t bit=bits-1-k;
                set_bit(expected,offset+k,bit<128?(unsigned)((value>>bit)&1):(unsigned)(trial&1));
            }
            CHECK(lb_bit_writer_init(&writer,actual,sizeof(actual))==LB_BITS_OK); writer.offset=offset;
            CHECK(lb_bits_put_integer(&writer,term,bits,LB_BITS_BIG_ENDIAN)==LB_BITS_OK);
            CHECK(!memcmp(actual,expected,sizeof(actual)));
        }
    }
}
static void wide(void)
{
    const size_t counts[]={3,257,LB_BIG_ARITY_MAX}; size_t i,sign,order;
    for(i=0;i<sizeof(counts)/sizeof(*counts);++i) {
        size_t n=counts[i],bits=n*64+13,bytes=(bits+7+7)/8,j;
        Eterm *term=malloc((n+1)*sizeof(Eterm)),*saved=malloc((n+1)*sizeof(Eterm));
        unsigned char *actual=malloc(bytes),*expected=malloc(bytes);
        CHECK(term && saved && actual && expected);
        for(j=1;j<=n;++j) term[j]=j==n?1:j%3?UINT64_MAX:0;
        for(sign=0;sign<2;++sign) for(order=0;order<2;++order) {
            LbBitWriter writer; term[0]=_make_header(n,sign?NEG_BIG_SUBTAG:POS_BIG_SUBTAG);
            memcpy(saved,term,(n+1)*sizeof(Eterm)); memset(actual,0xa5,bytes); memset(expected,0xa5,bytes);
            oracle(expected,7,make_boxed(term),bits,(LbBitEndian)order);
            CHECK(lb_bit_writer_init(&writer,actual,bytes)==LB_BITS_OK); writer.offset=7;
            CHECK(lb_bits_put_integer(&writer,make_boxed(term),bits,(LbBitEndian)order)==LB_BITS_OK);
            CHECK(!memcmp(actual,expected,bytes) && !memcmp(term,saved,(n+1)*sizeof(Eterm)));
        }
        free(expected); free(actual); free(saved); free(term);
    }
}
static void refusals(void)
{
    unsigned char bytes[16],saved[16]; LbBitWriter w,old; Eterm term[3];
    memset(bytes,0xa5,sizeof(bytes)); memcpy(saved,bytes,sizeof(bytes));
    CHECK(lb_bit_writer_init(&w,bytes,sizeof(bytes))==LB_BITS_OK); old=w;
    CHECK(lb_bit_writer_init(&w,NULL,1)==LB_BITS_INVALID && !memcmp(&w,&old,sizeof(w)));
    CHECK(lb_bit_writer_init(&w,bytes,SIZE_MAX)==LB_BITS_INVALID && !memcmp(&w,&old,sizeof(w)));
    CHECK(lb_bit_writer_init(&w,(unsigned char *)&w,sizeof(w))==LB_BITS_INVALID && !memcmp(&w,&old,sizeof(w)));
    CHECK(lb_bits_put_integer(&w,NIL,0,LB_BITS_BIG_ENDIAN)==LB_BITS_BADARG);
    CHECK(lb_bits_put_integer(&w,make_small(1),SIZE_MAX,LB_BITS_BIG_ENDIAN)==LB_BITS_NO_SPACE);
    CHECK(lb_bits_put_integer(&w,make_small(1),1,(LbBitEndian)3)==LB_BITS_INVALID);
    CHECK(lb_bits_put_utf8(&w,make_small(-1))==LB_BITS_BADARG);
    CHECK(lb_bits_put_utf16(&w,make_small(0xd800),LB_BITS_BIG_ENDIAN)==LB_BITS_BADARG);
    CHECK(lb_bits_put_utf32(&w,make_small(0x110000),LB_BITS_BIG_ENDIAN)==LB_BITS_BADARG);
    CHECK(!memcmp(bytes,saved,sizeof(bytes)) && !memcmp(&w,&old,sizeof(w)));
    w.offset=sizeof(bytes)*8-7; old=w;
    CHECK(lb_bits_put_integer(&w,make_small(1),8,LB_BITS_LITTLE_ENDIAN)==LB_BITS_NO_SPACE);
    CHECK(lb_bits_put_utf8(&w,make_small(1))==LB_BITS_NO_SPACE);
    CHECK(lb_bits_put_utf16(&w,make_small(1),LB_BITS_BIG_ENDIAN)==LB_BITS_NO_SPACE);
    CHECK(lb_bits_put_utf32(&w,make_small(1),LB_BITS_BIG_ENDIAN)==LB_BITS_NO_SPACE);
    CHECK(!memcmp(bytes,saved,sizeof(bytes)) && !memcmp(&w,&old,sizeof(w)));
    w.offset=SIZE_MAX; old=w;
    CHECK(lb_bits_put_integer(&w,make_small(1),0,LB_BITS_BIG_ENDIAN)==LB_BITS_INVALID && !memcmp(&w,&old,sizeof(w)));
    { Eterm v=big(term,0,1,0),copy[3]; memcpy(copy,term,sizeof(term));
      CHECK(lb_bit_writer_init(&w,(unsigned char *)term,sizeof(term))==LB_BITS_OK); old=w;
      CHECK(lb_bits_put_integer(&w,v,8,LB_BITS_BIG_ENDIAN)==LB_BITS_INVALID);
      CHECK(!memcmp(copy,term,sizeof(term)) && !memcmp(&w,&old,sizeof(w))); }
    CHECK(lb_bit_writer_init(&w,NULL,0)==LB_BITS_OK);
    CHECK(lb_bits_put_integer(&w,make_small(1),0,LB_BITS_BIG_ENDIAN)==LB_BITS_OK);
    CHECK(lb_bits_put_utf8(&w,make_small(1))==LB_BITS_NO_SPACE);
}
static void guard_pages(void)
{
    long page=sysconf(_SC_PAGESIZE); unsigned char *mapping; size_t offset,n,order; Eterm storage[3];
    CHECK(page>0); mapping=mmap(NULL,(size_t)page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    CHECK(mapping!=MAP_FAILED && !mprotect(mapping+page,(size_t)page,PROT_NONE));
    for(offset=0;offset<8;++offset) for(n=0;n<=257;++n) for(order=0;order<2;++order) {
        size_t bytes=(offset+n+7)/8; unsigned char *out=mapping+page-bytes,expected[40];
        Eterm value=big(storage,0,1,1); LbBitWriter w;
        memset(out,0xa5,bytes); memset(expected,0xa5,bytes); oracle(expected,offset,value,n,(LbBitEndian)order);
        CHECK(lb_bit_writer_init(&w,out,bytes)==LB_BITS_OK); w.offset=offset;
        CHECK(lb_bits_put_integer(&w,value,n,(LbBitEndian)order)==LB_BITS_OK && !memcmp(out,expected,bytes));
    }
    CHECK(!munmap(mapping,(size_t)page*2));
}
static unsigned read_byte(const unsigned char *data,size_t offset)
{
    unsigned byte=0; size_t i;
    for(i=0;i<8;++i) byte=(byte<<1)|((data[(offset+i)/8]>>(7-(offset+i)%8))&1);
    return byte;
}
static LbBitWriteStatus unicode_write(LbBitWriter *w,Uint n,unsigned mode)
{
    if(!mode) return lb_bits_put_utf8(w,make_small(n));
    if(mode<=2) return lb_bits_put_utf16(w,make_small(n),(LbBitEndian)(mode-1));
    return lb_bits_put_utf32(w,make_small(n),(LbBitEndian)(mode-3));
}
static void unicode(void)
{
    Uint n;
    for(n=0;n<=0x10ffff;++n) {
        unsigned char data[8],saved[8]; LbBitWriter w; LbBitWriteStatus status;
        size_t offset=n%16,i,count; unsigned mode=(unsigned)(n%5),b[4],decoded;
        memset(data,0xa5,sizeof(data)); memcpy(saved,data,sizeof(data));
        CHECK(lb_bit_writer_init(&w,data,sizeof(data))==LB_BITS_OK); w.offset=offset;
        status=unicode_write(&w,n,mode);
        if(n>=0xd800 && n<=0xdfff) {
            CHECK(status==LB_BITS_BADARG && w.offset==offset && !memcmp(data,saved,sizeof(data))); continue;
        }
        CHECK(status==LB_BITS_OK); count=(w.offset-offset)/8;
        CHECK(count==(mode==0?(n<0x80?1:n<0x800?2:n<0x10000?3:4):mode<=2?(n<0x10000?2:4):4));
        for(i=0;i<count;++i) b[i]=read_byte(data,offset+i*8);
        if(!mode) {
            if(count==1) decoded=b[0];
            else {
                decoded=b[0]&((1u<<(7-count))-1);
                for(i=1;i<count;++i) { CHECK((b[i]&0xc0)==0x80); decoded=(decoded<<6)|(b[i]&0x3f); }
            }
        } else if(mode<=2) {
            unsigned first=mode==1?(b[0]<<8)|b[1]:(b[1]<<8)|b[0];
            if(count==2) decoded=first;
            else {
                unsigned second=mode==1?(b[2]<<8)|b[3]:(b[3]<<8)|b[2];
                CHECK(count==4 && first>=0xd800 && first<=0xdbff && second>=0xdc00 && second<=0xdfff);
                decoded=0x10000+((first-0xd800)<<10)+(second-0xdc00);
            }
        } else {
            CHECK(count==4);
            decoded=mode==3?(b[0]<<24)|(b[1]<<16)|(b[2]<<8)|b[3]:(b[3]<<24)|(b[2]<<16)|(b[1]<<8)|b[0];
        }
        CHECK(decoded==n);
        for(i=0;i<64;++i) if(i<offset || i>=w.offset)
            CHECK(((data[i/8]>>(7-i%8))&1)==((saved[i/8]>>(7-i%8))&1));
    }
    /* Exact-sized Unicode destinations, including nonzero byte-aligned starts. */
    {
        const Uint values[]={0,0x7f,0x80,0x7ff,0x800,0xd7ff,0xe000,0xffff,0x10000,0x10ffff};
        long page=sysconf(_SC_PAGESIZE); unsigned char *map; size_t i,offset; unsigned mode;
        CHECK(page>0); map=mmap(NULL,(size_t)page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
        CHECK(map!=MAP_FAILED && !mprotect(map+page,(size_t)page,PROT_NONE));
        for(i=0;i<sizeof(values)/sizeof(*values);++i) for(offset=0;offset<16;++offset) for(mode=0;mode<5;++mode) {
            Uint value=values[i]; LbBitWriter w; size_t bits,bytes;
            if(!mode) bits=value<0x80?8:value<0x800?16:value<0x10000?24:32;
            else if(mode<=2) bits=value<0x10000?16:32;
            else bits=32;
            bytes=(offset+bits+7)/8; memset(map+page-bytes,0xa5,bytes);
            CHECK(lb_bit_writer_init(&w,map+page-bytes,bytes)==LB_BITS_OK); w.offset=offset;
            CHECK(unicode_write(&w,value,mode)==LB_BITS_OK && w.offset==offset+bits);
        }
        CHECK(!munmap(map,(size_t)page*2));
    }
}
static void print_bytes(const unsigned char *bytes,size_t n)
{
    size_t i; for(i=0;i<n;++i) printf("%02x",bytes[i]); putchar('\n');
}
static void reference_vectors(void)
{
    Eterm a[3],b[3],values[]={make_small(0),make_small(-1),make_small(MAX_SMALL),make_small(MIN_SMALL),big(a,0,1,0),big(b,1,1,1)};
    const size_t widths[]={0,1,7,8,9,63,64,65,127,128,129,257},offsets[]={0,1,7,8,15};
    const Sint scalars[]={0,0x7f,0x80,0x7ff,0x800,0xd7ff,0xe000,0xffff,0x10000,0x10ffff,-1,0xd800,0xdfff,0x110000};
    size_t i,j,k,order;
    for(i=0;i<sizeof(values)/sizeof(*values);++i) for(j=0;j<sizeof(widths)/sizeof(*widths);++j)
        for(k=0;k<sizeof(offsets)/sizeof(*offsets);++k) for(order=0;order<2;++order) {
            unsigned char bytes[40]; LbBitWriter w; memset(bytes,0xa5,sizeof(bytes));
            CHECK(lb_bit_writer_init(&w,bytes,sizeof(bytes))==LB_BITS_OK); w.offset=offsets[k];
            CHECK(lb_bits_put_integer(&w,values[i],widths[j],(LbBitEndian)order)==LB_BITS_OK);
            print_bytes(bytes,(w.offset+7)/8);
        }
    for(i=0;i<sizeof(scalars)/sizeof(*scalars);++i) for(k=0;k<sizeof(offsets)/sizeof(*offsets);++k)
        for(j=0;j<5;++j) {
            unsigned char bytes[8]; LbBitWriter w; LbBitWriteStatus status; memset(bytes,0xa5,sizeof(bytes));
            CHECK(lb_bit_writer_init(&w,bytes,sizeof(bytes))==LB_BITS_OK); w.offset=offsets[k];
            if(!j) status=lb_bits_put_utf8(&w,make_small(scalars[i]));
            else if(j<=2) status=lb_bits_put_utf16(&w,make_small(scalars[i]),(LbBitEndian)(j-1));
            else status=lb_bits_put_utf32(&w,make_small(scalars[i]),(LbBitEndian)(j-3));
            if(status==LB_BITS_BADARG) { CHECK(w.offset==offsets[k]); puts("badarg"); }
            else { CHECK(status==LB_BITS_OK); print_bytes(bytes,(w.offset+7)/8); }
        }
}
int main(int argc,char **argv)
{
    if(argc==2 && !strcmp(argv[1],"--reference")) { reference_vectors(); return 0; }
    CHECK(argc==1); integers(); wide(); refusals(); guard_pages(); unicode();
    puts("CORE_BIT_WRITER_OK integer_offsets=16 integer_lengths=258 oracle_128bit=10000 maximum_limbs=true endian=true unicode_scalar_candidates=1114112 rotating_encodings=true immutable_inputs=true outside_bits=true refusals_unchanged=true exact_buffer_guard_pages=true execution=false");
    return 0;
}
