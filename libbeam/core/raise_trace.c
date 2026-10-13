/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1996-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Adapted from bif.c raise_3 and error.h StackTrace. The supplied symbolic
 * trace is normalized before publication; its native negative-bignum payload
 * contains no code pointers. Catch/unwind admission remains separate.
 */
#include "raise_trace.h"
#include "process_internal.h"
#define TRACE_DEPTH 8u
/* Exact fixed prefix of native StackTrace, excluding its trace[1] array. */
typedef struct {
    Eterm header,freason;
    const BeamInstr *pc;
    const LbMFA *current;
    int depth,max_depth;
} SuppliedTrace;
#define TRACE_WORDS (sizeof(SuppliedTrace)/sizeof(Eterm))
_Static_assert(sizeof(SuppliedTrace)==5*sizeof(Eterm),"native 64-bit StackTrace prefix");
int lb_raise_trace_unpack(Eterm opaque,LbRaisedTrace *out)
{
    if(!out || !is_tuple(opaque)) return 0;
    Eterm *tuple=tuple_val(opaque);
    if(arityval(tuple[0])!=3 || !is_boxed(tuple[1]) || tuple[3]!=NIL ||
       (!is_list(tuple[2]) && !is_nil(tuple[2]))) return 0;
    Eterm *words=boxed_val(tuple[1]);
    if(words[0]!=_make_header(TRACE_WORDS-1,NEG_BIG_SUBTAG)) return 0;
    const SuppliedTrace *saved=(const SuppliedTrace *)words;
    if(saved->pc || saved->current || saved->depth || saved->max_depth) return 0;
    Uint reason=saved->freason;
    if(reason!=(LB_EXC_ERROR&~LB_EXF_SAVETRACE) &&
       reason!=(LB_EXC_EXIT&~LB_EXF_SAVETRACE) &&
       reason!=(LB_EXC_THROWN&~LB_EXF_SAVETRACE)) return 0;
    *out=(LbRaisedTrace){reason,tuple[2]}; return 1;
}
static int charge(LbProcess *p,uint64_t units)
{
    if(!lb_work_domain_charge(p->entry_module->space->domain,units)) return 0;
    if(units>UINTPTR_MAX-p->last_native_cost) p->last_native_cost=UINTPTR_MAX;
    else p->last_native_cost+=(Uint)units;
    return 1;
}
static int any_fun(Eterm value)
{ return is_boxed(value) && ((*boxed_val(value)&_HEADER_SUBTAG_MASK)==FUN_SUBTAG); }
static int frame_valid(Eterm frame)
{
    Eterm location=NIL;
    if(!is_tuple(frame)) return 0;
    Eterm *tuple=tuple_val(frame);
    switch(arityval(tuple[0])) {
    case 2:
        if(!any_fun(tuple[1])) return 0;
        break;
    case 3:
        if(any_fun(tuple[1])) location=tuple[3];
        else if(!is_atom(tuple[1]) || !is_atom(tuple[2])) return 0;
        break;
    case 4:
        if(!is_atom(tuple[1]) || !is_atom(tuple[2])) return 0;
        location=tuple[4]; break;
    default:return 0;
    }
    /* OTP intentionally does not validate Args/Arity or location elements. */
    return is_list(location) || is_nil(location);
}
LbRaiseTraceStatus lb_raise_trace_prepare(LbProcess *p,Eterm class,Eterm trace,Eterm *out)
{
    Uint reason; size_t depth=0;
    if(!p || !out || p->construction_root!=NIL) return LB_RAISE_TRACE_INVALID;
    if(class==am_error) reason=LB_EXC_ERROR;
    else if(class==am_exit) reason=LB_EXC_EXIT;
    else if(class==am_throw) reason=LB_EXC_THROWN;
    else return LB_RAISE_TRACE_BADARG;
    Eterm tail=trace;
    while(is_list(tail)) {
        if(!charge(p,1)) return LB_RAISE_TRACE_WORK_LIMIT;
        Eterm *cell=list_val(tail);
        if(!frame_valid(cell[0])) return LB_RAISE_TRACE_BADARG;
        if(depth<TRACE_DEPTH) ++depth;
        tail=cell[1];
    }
    if(!is_nil(tail)) return LB_RAISE_TRACE_BADARG;
    size_t words=TRACE_WORDS+4+depth*7,need;
    if(!lb_size_add(words,p->heap_reserved,&need)) return LB_RAISE_TRACE_MEMORY_LIMIT;
    if(!charge(p,depth+lb_work_bulk_units(words))) return LB_RAISE_TRACE_WORK_LIMIT;
    p->construction_root=trace;
    if(!lb_heap_reserve(p,need,MAX_REG)) {
        p->construction_root=NIL;
        LbAllocStatus failure=lb_alloc_domain_failure(p->entry_module->space->domain);
        if(failure==LB_ALLOC_WORK_LIMIT) return LB_RAISE_TRACE_WORK_LIMIT;
        if(failure==LB_ALLOC_LIMIT) return LB_RAISE_TRACE_MEMORY_LIMIT;
        return LB_RAISE_TRACE_NO_MEMORY;
    }
    tail=p->construction_root; p->construction_root=NIL;
    Eterm *start=p->htop,*hp=start;
    SuppliedTrace *saved=(SuppliedTrace *)hp;
    *saved=(SuppliedTrace){.header=_make_header(TRACE_WORDS-1,NEG_BIG_SUBTAG),
                          .freason=reason&~LB_EXF_SAVETRACE};
    hp+=TRACE_WORDS;
    Eterm symbolic=NIL,*link=&symbolic;
    for(size_t i=0;i<depth;++i) {
        Eterm *cell=list_val(tail),frame=cell[0],*tuple=tuple_val(frame);
        Uint arity=arityval(tuple[0]);
        if(arity==2) { frame=TUPLE3(hp,tuple[1],tuple[2],NIL); hp+=4; }
        else if(arity==3 && is_atom(tuple[1])) {
            hp[0]=make_arityval(4); hp[1]=tuple[1]; hp[2]=tuple[2]; hp[3]=tuple[3]; hp[4]=NIL;
            frame=make_tuple(hp); hp+=5;
        }
        *link=CONS(hp,frame,NIL); link=hp+1; hp+=2; tail=cell[1];
    }
    Eterm result=TUPLE3(hp,make_boxed(start),symbolic,NIL); hp+=4;
    ASSERT((size_t)(hp-start)<=words);
    p->htop=hp; *out=result; return LB_RAISE_TRACE_OK;
}
