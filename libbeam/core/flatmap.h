/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 2014-2025. All Rights Reserved.
 * Copyright Ericsson AB 2000-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Native flatmap construction and shared flatmap/HAMT shape helpers.
 */
#ifndef LIBBEAM_CORE_FLATMAP_H
#define LIBBEAM_CORE_FLATMAP_H
#include "term.h"
#include <stdlib.h>
typedef struct { Eterm thing_word; Uint size; Eterm keys; } LbFlatMap;
#define LB_FLATMAP_HEADER _make_header(1u<<2,MAP_SUBTAG)
#define LB_FLATMAP_WORDS 3u
#define LB_MAP_HEADER_TYPE(header) (((header)>>_HEADER_ARITY_OFFS)&3u)
#define LB_MAP_HEADER_VALUE(header) (((header)>>16)&0xffffu)
#define LB_MAP_HEAD_ARRAY_TYPE 2u
#define LB_MAP_HEAD_BITMAP_TYPE 3u
#define LB_MAP_NODE_BITMAP_TYPE 1u
#define LB_MAP_HEADER_IS_HAMT(header) ((header)!=LB_FLATMAP_HEADER && \
    (((header)&_HEADER_SUBTAG_MASK)==MAP_SUBTAG))
#define LB_FLATMAP_MAX_SIZE 32u
#define LB_MAP3_WORDS 10u
_Static_assert(sizeof(LbFlatMap)==3*sizeof(Eterm), "native flatmap prefix");
static inline int lb_is_flatmap(Eterm term)
{ return is_boxed(term) && boxed_val(term)[0]==LB_FLATMAP_HEADER; }
_Static_assert(offsetof(LbFlatMap,keys)==2*sizeof(Eterm), "native flatmap keys root");
/* Valid owned flatmaps only. Keys are a tuple of exactly size unique,
 * term-ordered keys. This helper excludes HAMTs; generic traversal uses
 * lb_map_object_words and its companion child-access helpers below. */
static inline size_t lb_flatmap_words(const Eterm *p)
{
    if(p[0]!=LB_FLATMAP_HEADER || p[1]>LB_FLATMAP_MAX_SIZE) abort();
    return LB_FLATMAP_WORDS+(size_t)p[1];
}
static inline unsigned lb_map_bitmap_count(unsigned bitmap)
{
    unsigned count=0; for(;bitmap;bitmap&=bitmap-1) ++count; return count;
}
static inline int lb_map_is_hamt_root_header(Eterm header)
{
    unsigned type=LB_MAP_HEADER_TYPE(header);
    return LB_MAP_HEADER_IS_HAMT(header) &&
        (type==LB_MAP_HEAD_ARRAY_TYPE || type==LB_MAP_HEAD_BITMAP_TYPE);
}
static inline int lb_map_is_hamt_node_header(Eterm header)
{
    return LB_MAP_HEADER_IS_HAMT(header) && LB_MAP_HEADER_TYPE(header)==LB_MAP_NODE_BITMAP_TYPE;
}
static inline size_t lb_map_object_words(const Eterm *p)
{
    Eterm header=p[0]; unsigned type=LB_MAP_HEADER_TYPE(header),bits=LB_MAP_HEADER_VALUE(header);
    if(header==LB_FLATMAP_HEADER) return lb_flatmap_words(p);
    if(!LB_MAP_HEADER_IS_HAMT(header)) abort();
    if(type==LB_MAP_HEAD_ARRAY_TYPE) return 18;
    if(type==LB_MAP_HEAD_BITMAP_TYPE) return 2u+lb_map_bitmap_count(bits);
    if(type==LB_MAP_NODE_BITMAP_TYPE) return 1u+lb_map_bitmap_count(bits);
    abort();
}
static inline size_t lb_map_object_child_offset(const Eterm *p)
{
    if(p[0]==LB_FLATMAP_HEADER || lb_map_is_hamt_root_header(p[0])) return 2;
    if(lb_map_is_hamt_node_header(p[0])) return 1;
    abort();
}
static inline size_t lb_map_object_child_count(const Eterm *p)
{
    if(p[0]==LB_FLATMAP_HEADER) return (size_t)p[1]+1;
    if(LB_MAP_HEADER_TYPE(p[0])==LB_MAP_HEAD_ARRAY_TYPE && lb_map_is_hamt_root_header(p[0])) return 16;
    if(lb_map_is_hamt_root_header(p[0]) || lb_map_is_hamt_node_header(p[0]))
        return lb_map_bitmap_count(LB_MAP_HEADER_VALUE(p[0]));
    abort();
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
