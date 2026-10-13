/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1996-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Transplants/adaptations: external.c dec_term's intrusive pending-term chain,
 * beam_file.c literal table readers, big.c bytes_to_big, erl_bits.h heap bits.
 * Factory/trapping/distribution contexts are NOT emulated here. Ordinary
 * binary references have real owner-local offheap lifetime; other resource
 * terms are not admitted into the owned preparation arena.
 */
#include "beam_program_internal.h"
#include "work_budget.h"
#include "utf8.h"
#include "flatmap.h"
#include "fun.h"
#include "map_runtime.h"
#include "map_hamt.h"
#include <math.h>
#include <float.h>
#include <stdlib.h>
#include <zlib.h>
#define REQUIRE(x) do { if (!(x)) return lb_pfail(p,LB_BEAM_BAD_FORMAT); } while (0)
static Uint be64(const unsigned char *s) { Uint v=0; unsigned i; for(i=0;i<8;++i) v=(v<<8)|s[i]; return v; }
/* Little-endian magnitude -> genuine BEAM small or word-digit bignum.
 * Same digit packing and small promotion as bytes_to_big; strip zeroes first
 * as dec_term does. 64-bit BIG_ARITY_MAX is the upstream 16-bit bound. */
int lb_pinteger(LbBeamProgram *p, const unsigned char *s, size_t n, int negative, LbEtfStorage storage, Eterm *out)
{
    REQUIRE(storage==LB_ETF_TEMPORARY || storage==LB_ETF_LITERAL);
    if(!lb_work_domain_charge(p->domain,n+lb_work_bulk_units(n))) return lb_pfail(p,LB_BEAM_LIMIT);
    size_t words,i,j;
    Eterm *heap;
    Uint digit=0;
    while(n && !s[n-1]) --n;
    if (!n) { *out=make_small(0); return 1; }
    if(n<=8) {
        for(i=n;i;--i) digit=(digit<<8)|s[i-1];
        if(digit<=(Uint)MAX_SMALL+(unsigned)negative) {
            *out=make_small(negative ? (Uint)0-digit : digit); return 1;
        }
    }
    words=(n+7)/8;
    if(words>65535) return lb_pfail(p,LB_BEAM_LIMIT);
    heap=lb_palloc(p,words+1,sizeof(Eterm)); if(!heap) return 0;
    heap[0]=(words<<_HEADER_ARITY_OFFS)|(negative?NEG_BIG_SUBTAG:POS_BIG_SUBTAG);
    for(i=0;i<words;++i) {
        digit=0;
        for(j=0;j<8 && i*8+j<n;++j) digit|=(Uint)s[i*8+j]<<(j*8);
        heap[i+1]=digit;
    }
    *out=make_boxed(heap)|(storage==LB_ETF_LITERAL?TAG_LITERAL_PTR:0); return 1;
}
int lb_pdynamic(LbBeamProgram *p, Eterm value, Sint *out)
{
    LbDynamicLiteral *entry;
    /* Inline integer literals are the only dynamic literals admitted here. */
    for(entry=p->dynamic;entry;entry=entry->next) {
        Eterm a=entry->value;
        if(a==value || (is_boxed(a) && is_boxed(value) && boxed_val(a)[0]==boxed_val(value)[0] &&
           !memcmp(boxed_val(a),boxed_val(value),(1+(boxed_val(a)[0]>>_HEADER_ARITY_OFFS))*sizeof(Eterm)))) {
            *out=entry->index; return 1;
        }
    }
    entry=lb_palloc(p,1,sizeof(*entry)); if(!entry) return 0;
    entry->value=value; entry->index=~(Sint)p->dynamic_count++;
    entry->next=p->dynamic; p->dynamic=entry; *out=entry->index; return 1;
}
static int u16(LbBeamReader *r, uint32_t *n) {
    const unsigned char *s; if(!lb_reader_bytes(r,2,&s)) return 0;
    *n=((uint32_t)s[0]<<8)|s[1]; return 1;
}
/* The destination slots themselves form the pending stack, as in dec_term.
 * No recursive C decoder, process heap, implicit global empty tuple, or atom
 * interning while decoding. Atom writes are deferred until the final transaction. */
int lb_pdecode_etf(LbBeamProgram *p, LbBeamReader *r, Eterm *root,LbEtfStorage storage)
{
    Eterm *next=root, *objp, *hp;
    const unsigned char *s;
    unsigned tag, byte;
    uint32_t n;
    size_t i;
    LbExternalFunPatch *external_before=p->external_funs;
    REQUIRE(storage==LB_ETF_TEMPORARY || storage==LB_ETF_LITERAL);
    Uint flags=storage==LB_ETF_LITERAL?TAG_LITERAL_PTR:0;
    REQUIRE(lb_reader_u8(r,&tag) && tag==131);
    *root=0;
    while(next) {
        if(!lb_work_domain_charge(p->domain,1)) return lb_pfail(p,LB_BEAM_LIMIT);
        objp=next; next=(Eterm *)(uintptr_t)*objp;
        REQUIRE(lb_reader_u8(r,&tag));
        switch(tag) {
        case 97: /* SMALL_INTEGER_EXT */
            REQUIRE(lb_reader_u8(r,&byte)); *objp=make_small(byte); break;
        case 98: /* INTEGER_EXT */
            REQUIRE(lb_reader_u32(r,&n)); *objp=make_small((int32_t)n); break;
        case 110: case 111: /* SMALL_BIG_EXT, LARGE_BIG_EXT */
            if(tag==110) { REQUIRE(lb_reader_u8(r,&byte)); n=byte; }
            else REQUIRE(lb_reader_u32(r,&n));
            REQUIRE(lb_reader_u8(r,&byte) && byte<=1 && lb_reader_bytes(r,n,&s));
            if(!lb_pinteger(p,s,n,(int)byte,storage,objp)) return 0;
            break;
        case 100: case 115: case 118: case 119: { /* Latin-1/UTF-8 atoms */
            LbBeamBytes name;
            if(tag==100 || tag==118) REQUIRE(u16(r,&n));
            else { REQUIRE(lb_reader_u8(r,&byte)); n=byte; }
            REQUIRE(lb_reader_bytes(r,n,&s));
            if(!lb_work_domain_charge(p->domain,3*(uint64_t)n)) return lb_pfail(p,LB_BEAM_LIMIT);
            name=(LbBeamBytes){s,n};
            if(tag==100 || tag==115) {
                unsigned char *utf8;
                size_t size=0;
                REQUIRE(n<=255);
                if(n) {
                    utf8=lb_palloc(p,n,2); if(!utf8) return 0;
                    for(i=0;i<n;++i) {
                        if(s[i]<128) utf8[size++]=s[i];
                        else { utf8[size++]=(unsigned char)(0xc0|(s[i]>>6)); utf8[size++]=(unsigned char)(0x80|(s[i]&63)); }
                    }
                    name=(LbBeamBytes){utf8,size};
                }
            }
            if(!lb_pname(p,name,objp)) return 0;
            break;
        }
        case 104: case 105: /* tuples */
            if(tag==104) { REQUIRE(lb_reader_u8(r,&byte)); n=byte; }
            else REQUIRE(lb_reader_u32(r,&n));
            REQUIRE(n<=MAX_ARITYVAL && n<=r->size-r->pos);
            if(!lb_work_domain_charge(p->domain,n)) return lb_pfail(p,LB_BEAM_LIMIT);
            hp=lb_palloc(p,n ? (size_t)n+1 : 2,sizeof(Eterm)); if(!hp) return 0;
            hp[0]=n ? make_arityval(n) : make_arityval_zero(); *objp=make_boxed(hp)|flags;
            if(!n) hp[1]=THE_NON_VALUE;
            for(i=n;i;i--) { hp[i]=(Eterm)(uintptr_t)next; next=&hp[i]; }
            break;
        case 116: { /* MAP_EXT: native flatmaps, never HAMT nodes */
            size_t words;
            Eterm *keys;
            LbMapLiteralPatch *map_patch;
            REQUIRE(lb_reader_u32(r,&n));
            if(n>MAX_ARITYVAL) return lb_pfail(p,LB_BEAM_LIMIT);
            REQUIRE((size_t)n*2<=r->size-r->pos);
            REQUIRE(lb_size_add(LB_FLATMAP_WORDS,(size_t)n,&words));
            REQUIRE(lb_size_add(words,n?n+1:2,&words));
            if(!lb_work_domain_charge(p->domain,2*(uint64_t)n)) return lb_pfail(p,LB_BEAM_LIMIT);
            hp=lb_palloc(p,words,sizeof(Eterm)); if(!hp) return 0;
            map_patch=lb_palloc(p,1,sizeof(*map_patch)); if(!map_patch) return 0;
            *map_patch=(LbMapLiteralPatch){p->map_literals,hp,objp,n,storage}; p->map_literals=map_patch;
            keys=hp+LB_FLATMAP_WORDS+n;
            hp[0]=LB_FLATMAP_HEADER; hp[1]=n;
            keys[0]=n?make_arityval(n):make_arityval_zero(); hp[2]=make_boxed(keys)|flags;
            *objp=make_boxed(hp)|flags;
            if(n) {
                for(i=n;i;--i) {
                    size_t index=i-1;
                    hp[LB_FLATMAP_WORDS+index]=(Eterm)(uintptr_t)next;
                    next=&hp[LB_FLATMAP_WORDS+index];
                    keys[index+1]=(Eterm)(uintptr_t)next;
                    next=&keys[index+1];
                }
            } else keys[1]=THE_NON_VALUE;
            break;
        }
        case 113: { /* EXPORT_EXT: an external function literal */
            LbExternalFunPatch *patch;
            hp=lb_palloc(p,LB_FUN_WORDS,sizeof(Eterm)); if(!hp) return 0;
            patch=lb_palloc(p,1,sizeof(*patch)); if(!patch) return 0;
            hp[0]=LB_FUN_HEADER(0,0)|((Uint)1<<LB_FUN_KIND_SHIFT);
            hp[1]=0;
            *objp=make_boxed(hp)|flags;
            patch->fun_words=hp; patch->module=patch->function=patch->arity=THE_NON_VALUE;
            patch->next=p->external_funs; p->external_funs=patch; ++p->external_fun_count;
            patch->arity=(Eterm)(uintptr_t)next; next=&patch->arity;
            patch->function=(Eterm)(uintptr_t)next; next=&patch->function;
            patch->module=(Eterm)(uintptr_t)next; next=&patch->module;
            break;
        }
        case 106: *objp=NIL; break;
        case 108: /* lists, including improper tails */
            REQUIRE(lb_reader_u32(r,&n) && n<r->size-r->pos);
            if(!n) { *objp=(Eterm)(uintptr_t)next; next=objp; break; }
            if(!lb_work_domain_charge(p->domain,n)) return lb_pfail(p,LB_BEAM_LIMIT);
            hp=lb_palloc(p,n,2*sizeof(Eterm)); if(!hp) return 0;
            *objp=make_list(hp)|flags;
            hp[2*(size_t)n-1]=(Eterm)(uintptr_t)next;
            next=&hp[2*(size_t)n-1];
            for(i=n;i;i--) {
                Eterm *cell=hp+2*(i-1);
                cell[0]=(Eterm)(uintptr_t)next;
                if(i<n) cell[1]=make_list(hp+2*i)|flags;
                next=cell;
            }
            break;
        case 107: /* STRING_EXT */
            REQUIRE(u16(r,&n) && lb_reader_bytes(r,n,&s));
            if(!n) { *objp=NIL; break; }
            if(!lb_work_domain_charge(p->domain,n)) return lb_pfail(p,LB_BEAM_LIMIT);
            hp=lb_palloc(p,n,2*sizeof(Eterm)); if(!hp) return 0;
            *objp=make_list(hp)|flags;
            for(i=0;i<n;++i) { hp[i*2]=make_small(s[i]); hp[i*2+1]=i+1==n ? NIL : make_list(hp+2*i+2)|flags; }
            break;
        case 109: case 77: { /* BINARY_EXT, BIT_BINARY_EXT */
            unsigned tail=8;
            size_t bits, words, offheap_bytes=0;
            REQUIRE(lb_reader_u32(r,&n));
            if(tag==77) REQUIRE(lb_reader_u8(r,&tail) && tail>=1 && tail<=8 && n>0);
            REQUIRE(lb_reader_bytes(r,n,&s));
            if(n>LB_MAX_BINARY_BYTES) return lb_pfail(p,LB_BEAM_LIMIT);
            bits=n ? (size_t)(n-1)*8+tail : 0; words=lb_bitstring_heap_words(bits);
            hp=lb_palloc(p,words,sizeof(Eterm)); if(!hp) return 0;
            if(n>LB_ONHEAP_BINARY_LIMIT) {
                offheap_bytes=lb_binary_allocation_size(n);
                if(offheap_bytes>LB_BEAM_MAX_BYTES-p->bytes) return lb_pfail(p,LB_BEAM_LIMIT);
            }
            if(lb_bitstring_build(p->domain,&p->off_heap,&hp,s,bits,flags,objp)!=LB_ALLOC_OK)
                return lb_pfail(p,LB_BEAM_NO_MEMORY);
            p->bytes+=offheap_bytes; break;
        }
        case 70: { /* NEW_FLOAT_EXT */
            Uint bits; double number;
            _Static_assert(sizeof(double)==8 && FLT_RADIX==2 && DBL_MANT_DIG==53 && DBL_MAX_EXP==1024,
                           "IEEE binary64 required");
            REQUIRE(lb_reader_bytes(r,8,&s)); bits=be64(s); memcpy(&number,&bits,8);
            REQUIRE(isfinite(number));
            hp=lb_palloc(p,2,sizeof(Eterm)); if(!hp) return 0;
            hp[0]=(1u<<_HEADER_ARITY_OFFS)|FLOAT_SUBTAG; hp[1]=bits;
            *objp=make_boxed(hp)|flags; break;
        }
        case 67: case 80: case 82: case 88: case 89: case 90: case 99:
        case 101: case 102: case 103: case 112: case 114: case 117: case 120: case 121:
            /* Known unadmitted record/compression/cache/resource/fun families.
             * This is a profile refusal, never a fabricated guest value. */
            return lb_pfail(p,LB_BEAM_UNSUPPORTED);
        default:
            return lb_pfail(p,storage==LB_ETF_TEMPORARY?LB_BEAM_BAD_FORMAT:LB_BEAM_UNSUPPORTED);
        }
    }
    for(LbExternalFunPatch *patch=p->external_funs;patch!=external_before;patch=patch->next) {
        if(!is_small(patch->arity) || signed_val(patch->arity)<0 || signed_val(patch->arity)>255)
            return lb_pfail(p,LB_BEAM_BAD_FORMAT);
        patch->fun_words[0]=LB_FUN_HEADER((Uint)signed_val(patch->arity),0)|((Uint)1<<LB_FUN_KIND_SHIFT);
    }
    return 1;
}
static int decode_term(LbBeamProgram *p,LbBeamReader *r,Eterm *root)
{
    if(!lb_pdecode_etf(p,r,root,LB_ETF_LITERAL)) return 0;
    REQUIRE(r->pos==r->size); return 1;
}
static voidpf zalloc_owned(voidpf context,uInt count,uInt size)
{
    LbBeamProgram *p=context;
    size_t bytes;
    void *result=NULL;
    if(!lb_size_mul(count,size,&bytes) || bytes>LB_BEAM_MAX_BYTES) { lb_pfail(p,LB_BEAM_LIMIT); return NULL; }
    if(lb_alloc_domain_allocate(p->domain,bytes,&result)!=LB_ALLOC_OK) lb_pfail(p,LB_BEAM_NO_MEMORY);
    return result;
}
static void zfree_owned(voidpf context,voidpf memory)
{
    LbBeamProgram *p=context;
    if(memory && lb_alloc_domain_release(p->domain,memory)!=LB_ALLOC_OK) abort();
}
static Eterm *map_literal_allocate(void *context,size_t words)
{
    return lb_palloc(context,words,sizeof(Eterm));
}
int lb_pvalidate_maps(LbBeamProgram *p,LbProcess *meter)
{
    p->error.stage="map literal ordering";
    for(LbMapLiteralPatch *patch=p->map_literals;patch;patch=patch->next) {
        Eterm *keys=tuple_val(patch->words[2])+1;
        Eterm *values=patch->words+LB_FLATMAP_WORDS;
        if(patch->size>LB_FLATMAP_MAX_SIZE) {
            /* Hash traversal is bounded by arena bytes. Reserve quadratic
             * comparison work for qsort and sixteen HAMT levels before build.
             * The preparation arena cap bounds both products below uint64. */
            if(meter && !lb_work_domain_charge(p->domain,
                p->bytes+128*(uint64_t)patch->size*patch->size+32*(uint64_t)patch->size))
                return lb_pfail(p,LB_BEAM_LIMIT);
            size_t bytes; void *scratch=NULL; LbHamtPair *pairs; Eterm result;
            if(!lb_size_mul(patch->size,sizeof(*pairs),&bytes)) return lb_pfail(p,LB_BEAM_LIMIT);
            LbAllocStatus allocated=lb_alloc_domain_allocate(p->domain,bytes,&scratch);
            if(allocated!=LB_ALLOC_OK) return lb_pfail(p,LB_BEAM_NO_MEMORY);
            pairs=scratch;
            for(size_t i=0;i<patch->size;++i) {
                pairs[i].key=keys[i]; pairs[i].value=values[i];
                if(lb_hamt_hash_key(keys[i],&pairs[i].hash)!=LB_HAMT_OK) {
                    if(lb_alloc_domain_release(p->domain,scratch)!=LB_ALLOC_OK) abort();
                    p->error.stage="map literal key hash";
                    return lb_pfail(p,LB_BEAM_UNSUPPORTED);
                }
            }
            LbHamtStatus built=lb_hamt_build(pairs,patch->size,map_literal_allocate,p,
                                              patch->storage==LB_ETF_LITERAL?TAG_LITERAL_PTR:0,&result);
            if(lb_alloc_domain_release(p->domain,scratch)!=LB_ALLOC_OK) abort();
            if(built!=LB_HAMT_OK) {
                if(built==LB_HAMT_UNSUPPORTED) p->error.stage="map literal HAMT collision";
                if(p->error.status==LB_BEAM_OK)
                    lb_pfail(p,built==LB_HAMT_UNSUPPORTED?LB_BEAM_UNSUPPORTED:
                             built==LB_HAMT_NO_MEMORY?LB_BEAM_NO_MEMORY:LB_BEAM_BAD_FORMAT);
                return 0;
            }
            *patch->term_slot=result;
            continue;
        }
        for(size_t i=1;i<patch->size;++i) {
            Eterm key=keys[i],value=values[i]; size_t j=i;
            while(j) {
                int order;
                LbMapStatus status=meter?lb_map_compare_keys(meter,keys[j-1],key,&order):lb_map_compare_literals(keys[j-1],key,&order);
                if(status!=LB_MAP_OK) return lb_pfail(p,status==LB_MAP_WORK_LIMIT?LB_BEAM_LIMIT:LB_BEAM_UNSUPPORTED);
                if(!order) return lb_pfail(p,LB_BEAM_BAD_FORMAT);
                if(order<0) break;
                keys[j]=keys[j-1]; values[j]=values[j-1]; --j;
            }
            keys[j]=key; values[j]=value;
        }
    }
    return 1;
}
int lb_pmodule_info(LbBeamProgram *p)
{
    const uint32_t ids[]={LB_BEAM_ID('A','t','t','r'),LB_BEAM_ID('C','I','n','f')};
    Eterm *dest[]={&p->attributes,&p->compile};
    size_t i;
    p->error.stage="module_info ETF";
    for(i=0;i<2;++i) {
        LbBeamBytes chunk;
        *dest[i]=NIL;
        if(lb_beam_image_chunk(p->image,ids[i],&chunk)==LB_BEAM_OK && chunk.size) {
            LbBeamReader r={chunk.data,chunk.size,0};
            if(!decode_term(p,&r,dest[i])) return 0;
            if(r.pos!=r.size) return lb_pfail(p,LB_BEAM_BAD_FORMAT);
        }
    }
    return 1;
}
int lb_pliterals(LbBeamProgram *p)
{
    LbBeamBytes chunk;
    LbBeamReader r;
    uint32_t unpacked,count,size;
    size_t i;
    const unsigned char *s;
    if(lb_beam_image_chunk(p->image,LB_BEAM_ID('L','i','t','T'),&chunk)==LB_BEAM_NOT_FOUND) return 1;
    p->error.stage="literals";
    r=(LbBeamReader){chunk.data,chunk.size,0};
    REQUIRE(lb_reader_u32(&r,&unpacked));
    if(unpacked) {
        z_stream stream={0}; int result;
        unsigned char *output;
        if(unpacked>LB_BEAM_MAX_BYTES) return lb_pfail(p,LB_BEAM_LIMIT);
        output=lb_palloc(p,unpacked,1); if(!output) return 0;
        stream.zalloc=zalloc_owned; stream.zfree=zfree_owned; stream.opaque=p;
        result=inflateInit(&stream);
        if(result!=Z_OK) return lb_pfail(p,result==Z_MEM_ERROR?LB_BEAM_NO_MEMORY:LB_BEAM_BAD_FORMAT);
        stream.next_in=(Bytef *)(chunk.data+r.pos); stream.avail_in=(uInt)(chunk.size-r.pos);
        stream.next_out=output; stream.avail_out=unpacked;
        result=inflate(&stream,Z_FINISH);
        if(result!=Z_STREAM_END || stream.avail_in || stream.avail_out) {
            inflateEnd(&stream); return lb_pfail(p,result==Z_MEM_ERROR?LB_BEAM_NO_MEMORY:LB_BEAM_BAD_FORMAT);
        }
        REQUIRE(inflateEnd(&stream)==Z_OK);
        r=(LbBeamReader){output,unpacked,0};
    }
    REQUIRE(lb_reader_u32(&r,&count) && count<=(r.size-r.pos)/5);
    if(count) { p->literals=lb_palloc(p,count,sizeof(Eterm)); if(!p->literals) return 0; }
    p->literal_count=count;
    for(i=0;i<count;++i) {
        LbBeamReader term;
        REQUIRE(lb_reader_u32(&r,&size) && lb_reader_bytes(&r,size,&s));
        term=(LbBeamReader){s,size,0};
        if(!decode_term(p,&term,&p->literals[i])) { p->error.offset=(size_t)(s-r.data)+term.pos; return 0; }
    }
    REQUIRE(r.pos==r.size); return 1;
}
