/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 2020-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Adapted from beam_file.c and beam_types.c. No debug/service singleton roots.
 */
#include "beam_program_internal.h"
#define REQUIRE(x) do { if (!(x)) return lb_pfail(p,LB_BEAM_BAD_FORMAT); } while (0)
static int64_t signed64(const unsigned char *s) { Uint v=0; unsigned i; for(i=0;i<8;++i) v=(v<<8)|s[i]; return (int64_t)v; }
int lb_pmetadata(LbBeamProgram *p)
{
    LbBeamBytes chunk;
    LbBeamReader r;
    const LbBeamImageInfo *info=lb_beam_image_info(p->image);
    uint32_t version,count;
    size_t i;
    p->error.stage="types";
    p->type_fallback=1; p->type_count=1;
    if(lb_beam_image_chunk(p->image,LB_BEAM_ID('T','y','p','e'),&chunk)==LB_BEAM_OK) {
        r=(LbBeamReader){chunk.data,chunk.size,0};
        REQUIRE(lb_reader_u32(&r,&version));
        if(version==3 || version==4) {
            REQUIRE(lb_reader_u32(&r,&count) && count>=1 && count<=(r.size-r.pos)/2);
            p->types=lb_palloc(p,count,sizeof(*p->types)); if(!p->types) return 0;
            p->type_count=count; p->type_fallback=0;
            for(i=0;i<count;++i) {
                const unsigned char *s;
                unsigned bits, flags;
                LbBeamType *t=&p->types[i];
                REQUIRE(lb_reader_bytes(&r,2,&s)); bits=((unsigned)s[0]<<8)|s[1];
                if(version==3) {
                    unsigned upper=(bits>>8)&15;
                    if(upper==15) upper=31;
                    bits=((bits<<1)&0xe000)|(upper<<8)|(bits&255);
                }
                REQUIRE(bits!=0); flags=bits&0xe000;
                t->types=(uint16_t)(bits&0x1fff); t->flags=(uint16_t)flags;
                t->min=MAX_SMALL+1; t->max=MIN_SMALL-1; t->unit=1;
                if(flags&0x6000) REQUIRE(t->types&((1<<3)|(1<<5)));
                if(flags&0x2000) { REQUIRE(lb_reader_bytes(&r,8,&s)); t->min=signed64(s); }
                if(flags&0x4000) { REQUIRE(lb_reader_bytes(&r,8,&s)); t->max=signed64(s); }
                if(flags&0x8000) { REQUIRE(t->types&(1<<1)); REQUIRE(lb_reader_bytes(&r,1,&s)); t->unit=(uint16_t)(s[0]+1); }
            }
            REQUIRE(r.pos==r.size && p->types[0].types==0x1fff && p->types[0].min>p->types[0].max);
        }
    }
    if(p->type_fallback) {
        p->types=lb_palloc(p,1,sizeof(*p->types)); if(!p->types) return 0;
        *p->types=(LbBeamType){0x1fff,0,1,MAX_SMALL+1,MIN_SMALL-1};
    }
    /* Line/Dbgi/Attr/CInf payloads stay opaque in the owned image. This is the
     * explicit no-line-info policy, not a fabricated runtime source location.
     * Unknown Type versions use the upstream ANY fallback. */
    p->error.stage="lambdas";
    if(lb_beam_image_chunk(p->image,LB_BEAM_ID('F','u','n','T'),&chunk)==LB_BEAM_NOT_FOUND) return 1;
    r=(LbBeamReader){chunk.data,chunk.size,0};
    REQUIRE(lb_reader_u32(&r,&count) && count==(r.size-r.pos)/24 && (r.size-r.pos)%24==0);
    if(count) { p->lambdas=lb_palloc(p,count,sizeof(*p->lambdas)); if(!p->lambdas) return 0; }
    p->lambda_count=count;
    for(i=0;i<count;++i) {
        LbBeamLambda *lambda=&p->lambdas[i]; uint32_t atom;
        REQUIRE(lb_reader_u32(&r,&atom) && atom>0 && atom<=info->atom_count);
        lambda->function=atom; /* resolved at the final namespace commit */
        REQUIRE(lb_reader_u32(&r,&lambda->arity) && lambda->arity<=255);
        REQUIRE(lb_reader_u32(&r,&lambda->label) && lambda->label>0 && lambda->label<info->label_count);
        REQUIRE(lb_reader_u32(&r,&lambda->index) && lambda->index<count);
        REQUIRE(lb_reader_u32(&r,&lambda->num_free) && lambda->num_free<=lambda->arity);
        REQUIRE(lb_reader_u32(&r,&lambda->old_uniq));
    }
    return 1;
}
