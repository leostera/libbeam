/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1996-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Owned immediate capture/build adaptation of beam_common.c save_stacktrace,
 * gather_stacktrace, build_stacktrace and utils.c erts_build_mfa_item.
 * The existing explicit no-line-info profile makes native loc lists empty;
 * functions, arguments and continuation frames are real, never invented.
 */
#include "stacktrace.h"
#include "process_internal.h"
#include "flatmap.h"
#define TRACE_DEPTH 8u /* pinned error.h DEFAULT_BACKTRACE_SIZE */
typedef struct { LbMFA mfa; } Frame;
static int lookup(LbCodeSpace *space,const BeamInstr *pc,LbMFA *mfa)
{
    Uint address=(Uint)pc;
    for(LbCodeModule *m=space->modules;m;m=m->next) {
        if(!lb_work_domain_charge(space->domain,1)) return 0;
        if(address<(Uint)m->words || address>=(Uint)(m->words+m->word_count)) continue;
        size_t count=lb_code_module_function_count(m);
        for(size_t i=0;i<count;++i) {
            if(!lb_work_domain_charge(space->domain,1)) return 0;
            const BeamInstr *info=(const BeamInstr *)m->words[i];
            Uint end=i+1<count?m->words[i+1]:(Uint)(m->words+m->word_count);
            if(address>=(Uint)info && address<end) {
                memcpy(mfa,info+offsetof(LbCodeInfo,mfa)/sizeof(BeamInstr),sizeof(*mfa)); return 1;
            }
        }
    }
    return 0;
}
void lb_trace_callsite(LbProcess *p,const BeamInstr *from,const BeamInstr *to)
{
    LbCodeSpace *space=p->entry_module->space;
    if(!lb_work_domain_charge(space->domain,LB_NATIVE_COUNT)) return;
    for(size_t i=0;i<LB_NATIVE_COUNT;++i) if(to==space->natives[i].dispatch.addresses[0]) {
        LbMFA mfa;
        if(lookup(space,from,&mfa)) {
            p->native_callsite=from;
            p->native_trace_previous=from[0]==op_i_call_ext_e?from+2:
                from[0]==op_i_move_call_ext_ce?from+3:NULL;
        }
        return;
    }
}
static Eterm arglist(LbProcess *p,Uint arity)
{
    Eterm result=NIL;
    for(Uint i=arity;i>0;--i) { result=CONS(p->htop,p->reg[i-1],result); p->htop+=2; }
    return result;
}
static Eterm map1(Eterm *hp,Eterm key,Eterm value)
{
    hp[0]=LB_FLATMAP_HEADER; hp[1]=1; hp[2]=make_boxed(hp+4); hp[3]=value;
    hp[4]=make_arityval(1); hp[5]=key; return make_boxed(hp);
}
static Eterm item(LbProcess *p,const LbMFA *mfa,Eterm args,Eterm loc)
{
    /* Native erts_build_mfa_item under the admitted no-line-info policy. */
    Eterm third=is_list(args)||is_nil(args)?args:make_small((Sint)mfa->arity);
    Eterm *hp=p->htop;
    hp[0]=make_arityval(4); hp[1]=mfa->module; hp[2]=mfa->function; hp[3]=third; hp[4]=loc;
    Eterm result=make_boxed(hp); p->htop+=5;
    return result;
}
LbProcessStatus lb_error_down_reason(LbProcess *p,Eterm *out)
{
    Frame frames[TRACE_DEPTH]; size_t depth=0; Uint args_arity=0;
    LbCodeSpace *space=p->entry_module->space;
    const BeamInstr *pc=p->i,*previous=NULL;
    int bif=0,copy_args=0,wrapper=!!(p->freason&LB_EXF_ARGLIST);
    Eterm args=am_true,info=NIL;
    if(p->status!=LB_PROCESS_EXCEPTION || LB_GET_EXC_CLASS(p->freason)!=LB_EXTAG_ERROR) return LB_PROCESS_INVALID;
    if(!lb_work_domain_charge(space->domain,LB_NATIVE_COUNT)) return LB_PROCESS_WORK_LIMIT;
    for(size_t i=0;i<LB_NATIVE_COUNT;++i) if(pc==space->natives[i].dispatch.addresses[0]) { bif=1; break; }
    if(bif && p->current->function!=am_error) {
        /* All admitted native imports are erlang BIFs. Other format modules
         * and BIF cause maps require their own positive catalog admission. */
        if(p->current->module!=am_erlang || (p->freason&LB_EXF_HAS_EXT_INFO)) return LB_PROCESS_REFUSED;
        frames[depth++].mfa=*p->current; copy_args=1; args_arity=p->current->arity;
        pc=p->native_callsite;
        if(pc && lookup(space,pc,&frames[depth].mfa)) ++depth;
        previous=p->native_trace_previous?p->native_trace_previous:pc;
    } else {
        if(bif) pc=p->native_callsite;
        if(!pc || !lookup(space,pc,&frames[depth].mfa)) return LB_PROCESS_REFUSED;
        ++depth; previous=bif && p->native_trace_previous?p->native_trace_previous:pc;
        if(LB_GET_EXC_INDEX(p->freason)==LB_GET_EXC_INDEX(LB_EXC_FUNCTION_CLAUSE)) {
            copy_args=1; args_arity=frames[0].mfa.arity;
        }
    }
    /* Native gather_stacktrace: nonduplicate CPs, one-word interpreter
     * adjustment, bounded depth. Trace-return frames cannot be admitted by
     * this runtime, and the host return is not an Erlang continuation. */
    for(Eterm *ptr=p->stop;ptr<p->hend && depth<TRACE_DEPTH;++ptr) {
        if(!lb_work_domain_charge(space->domain,1)) return LB_PROCESS_WORK_LIMIT;
        if((*ptr&7u)!=0 || !*ptr) continue;
        const BeamInstr *cp=(const BeamInstr *)*ptr;
        if(cp==previous || cp==lb_host_return) continue;
        previous=cp;
        if(lookup(space,(const BeamInstr *)((Uint)cp-sizeof(BeamInstr)),&frames[depth].mfa)) ++depth;
    }
    if(args_arity>255) return LB_PROCESS_INVALID;
    /* All allocations precede borrowed moving payloads; publication is last.
     * Raw fvalue and the expanded reason are permanent moving process roots.
     * Frame storage above contains only atom words/arity, not heap pointers. */
    size_t need=depth*7+args_arity*2+6+5+3;
    if(!lb_work_domain_charge(space->domain,args_arity+depth+lb_work_bulk_units(need))) return LB_PROCESS_WORK_LIMIT;
    if(!lb_heap_reserve(p,need,MAX_REG)) return LB_PROCESS_NO_MEMORY;
    if(wrapper) {
        Eterm *tuple=tuple_val(p->fvalue);
        args=tuple[2];
        if(p->freason&LB_EXF_HAS_EXT_INFO) info=tuple[3];
    } else if(copy_args) args=arglist(p,args_arity);
    if(bif && p->current->function!=am_error) {
        info=map1(p->htop,am_module,am_erl_erts_errors); p->htop+=6;
    } else if((p->freason&LB_EXF_HAS_EXT_INFO) && !wrapper) info=p->fvalue;
    Eterm loc=NIL;
    if(info!=NIL) {
        Eterm pair=TUPLE2(p->htop,am_error_info,info); p->htop+=3;
        loc=CONS(p->htop,pair,NIL); p->htop+=2;
    }
    Eterm trace=NIL;
    for(size_t i=depth;i>0;--i) {
        Eterm frame=item(p,&frames[i-1].mfa,i==1?args:am_true,i==1?loc:NIL);
        trace=CONS(p->htop,frame,trace); p->htop+=2;
    }
    Eterm result=TUPLE2(p->htop,p->exception_value,trace); p->htop+=3;
    *out=result; return LB_PROCESS_READY;
}
