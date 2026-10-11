/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 2014-2025. All Rights Reserved.
 * Copyright Ericsson AB 2000-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Native flatmap layout and MAP3 construction, not HAMT or map-op admission.
 */
#ifndef LIBBEAM_CORE_FLATMAP_H
#define LIBBEAM_CORE_FLATMAP_H
#include "term.h"
#include <stdlib.h>
typedef struct { Eterm thing_word; Uint size; Eterm keys; } LbFlatMap;
#define LB_FLATMAP_HEADER _make_header(1u<<2,MAP_SUBTAG)
#define LB_FLATMAP_WORDS 3u
#define LB_MAP3_WORDS 10u
_Static_assert(sizeof(LbFlatMap)==3*sizeof(Eterm), "native flatmap prefix");
_Static_assert(offsetof(LbFlatMap,keys)==2*sizeof(Eterm), "native flatmap keys root");
/* Valid owned flatmaps only; the common Debug/Release small-map profile is 0..3.
 * Keys are a tuple of exactly size unique, native-order keys. Unknown map kinds
 * are invariant violations, never silently treated as flatmaps. */
static inline size_t lb_flatmap_words(const Eterm *p)
{
    if(p[0]!=LB_FLATMAP_HEADER || p[1]>3) abort();
    return LB_FLATMAP_WORDS+p[1];
}
/* Native MAP3: caller reserves ten words and supplies already sorted keys.
 * No allocation or collection; argument terms remain owned by their parents. */
static inline Eterm lb_map3(Eterm *hp,Eterm k1,Eterm v1,Eterm k2,Eterm v2,Eterm k3,Eterm v3)
{
    hp[0]=LB_FLATMAP_HEADER; hp[1]=3;
    hp[2]=TUPLE3(hp+6,k1,k2,k3);
    hp[3]=v1; hp[4]=v2; hp[5]=v3;
    return make_boxed(hp);
}
#endif
