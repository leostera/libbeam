/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 2000-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Ordinary flat64 reference representation from erl_term.h; no magic/PID refs.
 */
#ifndef LIBBEAM_CORE_REFERENCE_H
#define LIBBEAM_CORE_REFERENCE_H
#include "term.h"
#include <stdlib.h>
#if !defined(__BYTE_ORDER__) || !defined(__ORDER_LITTLE_ENDIAN__) || !defined(__ORDER_BIG_ENDIAN__)
#error "Reference layout requires compiler byte-order definitions"
#endif
typedef struct {
    Eterm header;
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint32_t marker;
#endif
    uint32_t num[3];
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    uint32_t marker;
#endif
} LbOrdinaryRef;
#define LB_REF_WORDS 3u
#define LB_REF_HEADER _make_header(2,REF_SUBTAG)
_Static_assert(sizeof(LbOrdinaryRef)==LB_REF_WORDS*sizeof(Eterm),"native ordinary reference size");
static inline const LbOrdinaryRef *lb_reference(Eterm value)
{
    if(!is_boxed(value) || (*boxed_val(value)&_HEADER_SUBTAG_MASK)!=REF_SUBTAG) return NULL;
    const LbOrdinaryRef *ref=(const LbOrdinaryRef *)boxed_val(value);
    if(ref->header!=LB_REF_HEADER || ref->marker!=UINT32_MAX) abort();
    return ref;
}
/* Native erts_set_ref_numbers: the low 18 bits occupy number zero, the
 * remaining low-32 bits occupy number one, leaving scheduler/type bits clear.
 * One serialized Engine generator uses scheduler discriminator 1. */
static inline Eterm lb_reference_write(Eterm *hp,uint64_t value)
{
    LbOrdinaryRef *ref=(LbOrdinaryRef *)hp;
    ref->header=LB_REF_HEADER; ref->marker=UINT32_MAX;
    ref->num[0]=(uint32_t)(value&UINT32_C(0x3ffff));
    ref->num[1]=(uint32_t)(value&UINT32_C(0xfffc0000))|1u;
    ref->num[2]=(uint32_t)(value>>32);
    return make_boxed(hp);
}
static inline Eterm lb_reference_from_numbers(Eterm *hp,uint32_t first,uint32_t second,uint32_t third)
{
    LbOrdinaryRef *ref=(LbOrdinaryRef *)hp;
    ref->header=LB_REF_HEADER; ref->marker=UINT32_MAX;
    ref->num[0]=third; ref->num[1]=second; ref->num[2]=first;
    return make_boxed(hp);
}
static inline int lb_is_reference(Eterm value) { return lb_reference(value)!=NULL; }
#endif
