/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 2020-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Adapted from beam_file.c. See docs/rfds/0003-loader-program.md.
 */
#include "beam_reader.h"
int lb_reader_bytes(LbBeamReader *r, size_t n, const unsigned char **out)
{
    if (!r->data && r->size) return 0;
    if (r->pos > r->size || n > r->size-r->pos) return 0;
    *out=r->data ? r->data+r->pos : NULL; r->pos+=n; return 1;
}
int lb_reader_u8(LbBeamReader *r, unsigned *out)
{
    const unsigned char *p;
    if (!lb_reader_bytes(r,1,&p)) return 0;
    *out=*p; return 1;
}
int lb_reader_u32(LbBeamReader *r, uint32_t *out)
{
    const unsigned char *p;
    if (!lb_reader_bytes(r,4,&p)) return 0;
    *out=((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; return 1;
}
static int tagged(LbBeamReader *r, LbTaggedNumber *v, unsigned depth)
{
    unsigned code, extra;
    size_t count, i;
    Uint value=0;
    const unsigned char *p;
    if (depth>16 || !lb_reader_u8(r,&code)) return 0;
    *v=(LbTaggedNumber){0}; v->tag=code&7;
    if (!(code&8)) { v->word=code>>4; return 1; }
    if (!(code&16)) {
        if (!lb_reader_u8(r,&extra)) return 0;
        v->word=((code>>5)<<8)|extra; return 1;
    }
    code>>=5;
    if (code<7) count=code+2;
    else {
        LbTaggedNumber prefix;
        if (!tagged(r,&prefix,depth+1) || prefix.tag!=TAG_u || prefix.size ||
            prefix.word<0 || prefix.word>=INT32_MAX-9) return 0;
        count=(size_t)prefix.word+9;
    }
    if (!lb_reader_bytes(r,count,&p)) return 0;
    if (count<=sizeof(Uint)) {
        for (i=0;i<count;++i) value=(value<<8)|p[i];
        if (v->tag==TAG_i) {
            /* Unsigned accumulation/sign extension avoids upstream's signed
             * left-shift UB for negative compact numbers. */
            if ((p[0]&128) && count<sizeof(Uint)) value|=UINTPTR_MAX<<(count*8);
            v->word=(Sint)value;
            if (v->word>=MIN_SMALL && v->word<=MAX_SMALL) return 1;
        } else if (value<=INTPTR_MAX) { v->word=(Sint)value; return 1; }
    }
    if (v->tag!=TAG_i) v->tag=TAG_o;
    v->bytes=p; v->size=count; v->word=0;
    return 1;
}
int lb_reader_tagged(LbBeamReader *r, LbTaggedNumber *v) { return tagged(r,v,0); }
