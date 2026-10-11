/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1996-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Native beam_common.c error/class tables and expand_error_value.
 */
#include "exception.h"
#include "process_internal.h"
static const Eterm error_atom[LB_NUMBER_EXIT_CODES]={
    am_internal_error,am_normal,am_internal_error,am_badarg,am_badarith,
    am_badmatch,am_function_clause,am_case_clause,am_if_clause,am_undef,
    am_badfun,am_badarity,am_timeout_value,am_noproc,am_notalive,
    am_system_limit,am_try_clause,am_notsup,am_badmap,am_badkey,
    am_badrecord,am_badfield,am_novalue
};
static const Eterm exception_tag[LB_NUMBER_EXC_TAGS]={am_error,am_exit,am_throw};
int lb_exception_code_valid(Uint code)
{
    return code!=LB_EXC_NULL && code!=LB_TRAP &&
        !(code&~(Uint)(LB_EXC_CLASSBITS|LB_EXC_FLAGBITS|LB_EXC_INDEXBITS)) &&
        LB_GET_EXC_CLASS(code)<LB_NUMBER_EXC_TAGS && LB_GET_EXC_INDEX(code)<LB_NUMBER_EXIT_CODES;
}
Eterm lb_exception_atom(Uint code)
{
    if(!lb_exception_code_valid(code) || !LB_GET_EXC_INDEX(code)) return THE_NON_VALUE;
    return error_atom[LB_GET_EXC_INDEX(code)];
}
Eterm lb_exception_class(Uint code)
{
    return lb_exception_code_valid(code)?exception_tag[LB_GET_EXC_CLASS(code)]:THE_NON_VALUE;
}
LbAllocStatus lb_exception_expand(LbProcess *p,Uint code,Eterm value,Eterm *out)
{
    size_t index,need; Eterm result;
    if(!p || !out || !lb_exception_code_valid(code) || is_non_value(value) ||
       (code&LB_EXF_PANIC) || LB_GET_EXC_INDEX(code)==2) return LB_ALLOC_INVALID;
    index=LB_GET_EXC_INDEX(code);
    switch(index) {
    case LB_GET_EXC_INDEX(LB_EXC_PRIMARY):result=value;break;
    case LB_GET_EXC_INDEX(LB_EXC_BADMATCH):case LB_GET_EXC_INDEX(LB_EXC_CASE_CLAUSE):
    case LB_GET_EXC_INDEX(LB_EXC_TRY_CLAUSE):case LB_GET_EXC_INDEX(LB_EXC_BADFUN):
    case LB_GET_EXC_INDEX(LB_EXC_BADARITY):case LB_GET_EXC_INDEX(LB_EXC_BADMAP):
    case LB_GET_EXC_INDEX(LB_EXC_BADKEY):case LB_GET_EXC_INDEX(LB_EXC_BADRECORD):
    case LB_GET_EXC_INDEX(LB_EXC_BADFIELD):case LB_GET_EXC_INDEX(LB_EXC_NOVALUE):
        if(!lb_size_add(3,p->heap_reserved,&need)) return LB_ALLOC_NO_MEMORY;
        if(p->construction_root!=NIL) abort();
        p->construction_root=value;
        if(!lb_heap_reserve(p,need,MAX_REG)) { p->construction_root=NIL; return LB_ALLOC_NO_MEMORY; }
        value=p->construction_root; p->construction_root=NIL;
        result=TUPLE2(p->htop,error_atom[index],value); p->htop+=3; break;
    default:result=error_atom[index];break;
    }
    *out=result; return LB_ALLOC_OK;
}
