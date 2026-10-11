/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1996-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Payload construction from beam_emu.c:erts_prepare_bs_construct_fail_info.
 * Heap fragments become explicit fallible collection with a temporary root.
 */
#include "binary_error.h"
#include "flatmap.h"
#include "process_internal.h"
static Eterm operation(LbBinarySegmentKind kind)
{
    switch(kind) {
    case LB_BSC_APPEND:case LB_BSC_PRIVATE_APPEND:case LB_BSC_BINARY:
    case LB_BSC_BINARY_FIXED_SIZE:case LB_BSC_BINARY_ALL:return am_binary;
    case LB_BSC_FLOAT:case LB_BSC_FLOAT_FIXED_SIZE:return am_float;
    case LB_BSC_INTEGER:case LB_BSC_INTEGER_FIXED_SIZE:return am_integer;
    case LB_BSC_STRING:return am_string;
    case LB_BSC_UTF8:return am_utf8;
    case LB_BSC_UTF16:return am_utf16;
    case LB_BSC_UTF32:return am_utf32;
    default:return am_none;
    }
}
LbAllocStatus lb_binary_error_payload(LbProcess *p,LbBinarySegmentKind kind,Uint segment,
                                     Eterm info,Eterm value,Eterm *out)
{
    size_t need; Eterm *hp,cause,result;
    if(!p || !out || segment>(Uint)MAX_SMALL || !is_atom(info) ||
       atom_val(info)>=lb_atoms_count(p->entry_module->space->atoms)) return LB_ALLOC_INVALID;
    if(!lb_size_add(LB_MAP3_WORDS+5,p->heap_reserved,&need)) return LB_ALLOC_NO_MEMORY;
    if(p->construction_root!=NIL) abort();
    p->construction_root=value;
    if(!lb_heap_reserve(p,need,MAX_REG)) { p->construction_root=NIL; return LB_ALLOC_NO_MEMORY; }
    value=p->construction_root; p->construction_root=NIL;
    hp=p->htop;
    hp[0]=make_arityval(4); hp[1]=make_small(segment); hp[2]=operation(kind); hp[3]=info; hp[4]=value;
    cause=make_tuple(hp); hp+=5;
    /* Native MAP3 keys are already ordered by atom names, not local atom IDs. */
    result=lb_map3(hp,am_cause,cause,am_function,am_format_bs_fail,am_module,am_erl_erts_errors);
    p->htop=hp+LB_MAP3_WORDS; *out=result; return LB_ALLOC_OK;
}
