/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Must-definition, shape and heap-credit dataflow over the admitted profile.
 * Native extraction instructions may omit checks only after this proof. Compiler
 * type hints are not evidence: facts come from real tests and constructors.
 */
#include "code_internal.h"
#include "fun.h"
#include "flatmap.h"
#include "map_runtime.h"
#include "binary.h"
#define BITS (MAX_REG/64)
#define NO_FRAME SIZE_MAX
#define TUPLE_SHAPE UINT32_C(0x1000000)
#define LIST_SHAPE UINT32_C(0x2000000) /* nonempty cons, safe for get_list */
#define LIST_OR_NIL_SHAPE (LIST_SHAPE|UINT32_C(1))
#define NIL_SHAPE (LIST_SHAPE|UINT32_C(2))
#define APPEND_SHAPE UINT32_C(0x4000000) /* append result; combined with tuple means conditional tuple-arity fact */
#define COPYABLE_APPEND_SHAPE (APPEND_SHAPE|UINT32_C(2)) /* append fact revoked; safe ordinary-append fallback only */
#define APPEND_CONTEXT_SHAPE (APPEND_SHAPE|MATCH_CONTEXT_SHAPE)
#define POSSIBLE_TUPLE_ARITY APPEND_SHAPE
#define KNOWN_NON_TUPLE UINT32_C(1)
#define ATOM_SHAPE UINT32_C(4)
#define MATCH_CONTEXT_SHAPE UINT32_C(0x10000000)
#define POSSIBLE_MATCH_CONTEXT UINT32_C(0x20000000)
#define MAP_SHAPE UINT32_C(0x40000000)
#define BITSTRING_SHAPE UINT32_C(0x80000000)
#define EXCEPTION_CLASS_SHAPE (MAP_SHAPE|UINT32_C(1))
#define EXCEPTION_TRACE_SHAPE (MAP_SHAPE|UINT32_C(2))
#define MATCH_CONTEXT_FLAGS (MATCH_CONTEXT_SHAPE|POSSIBLE_MATCH_CONTEXT)
#define CONSUMED_VALUE UINT32_C(0x8000000) /* initialized GC root, unavailable logical value */
#define ARITY_MASK UINT32_C(0xffffff)
#define COMPACT_ARITY_MASK UINT16_C(0x0fff)
#define COMPACT_KIND_SHIFT 12
#define COMPACT_Y_ARITY_MASK UINT8_C(0x0f)
#define COMPACT_Y_KIND_SHIFT 4

enum CompactShapeKind {
    COMPACT_UNKNOWN, COMPACT_TUPLE, COMPACT_POSSIBLE_TUPLE, COMPACT_LIST,
    COMPACT_APPEND, COMPACT_NON_TUPLE, COMPACT_MATCH_CONTEXT,
    COMPACT_POSSIBLE_MATCH_CONTEXT, COMPACT_CONSUMED, COMPACT_MAP,
    COMPACT_BITSTRING, COMPACT_APPEND_CONTEXT, COMPACT_COPYABLE_APPEND,
    COMPACT_LIST_OR_NIL, COMPACT_NIL, COMPACT_EXCEPTION_COMPONENT, COMPACT_ATOM
};
#define REQUIRE(test) do { if(!(test)) return LB_CODE_FORMAT; } while(0)
_Static_assert(MAX_ARITYVAL==ARITY_MASK,"Shape facts cover native tuple arities");
typedef struct {
    uint64_t x[BITS],y[BITS];
    uint16_t xs[MAX_REG];
    uint8_t ys[MAX_REG]; /* Y facts use a narrower arity: larger tuples lose precision, not safety. */
    size_t frame,frame_allocation,heap;
    int matching,exception_handler;
} Flow;
static uint16_t compact_shape(uint32_t shape)
{
    unsigned kind; Uint arity=shape&ARITY_MASK;
    if(shape==EXCEPTION_CLASS_SHAPE || shape==EXCEPTION_TRACE_SHAPE) kind=COMPACT_EXCEPTION_COMPONENT;
    else if(shape==ATOM_SHAPE) kind=COMPACT_ATOM;
    else if(shape&TUPLE_SHAPE) kind=(shape&POSSIBLE_TUPLE_ARITY)?COMPACT_POSSIBLE_TUPLE:COMPACT_TUPLE;
    else if(shape==LIST_SHAPE) kind=COMPACT_LIST;
    else if(shape==LIST_OR_NIL_SHAPE) kind=COMPACT_LIST_OR_NIL;
    else if(shape==NIL_SHAPE) kind=COMPACT_NIL;
    else if(shape==APPEND_CONTEXT_SHAPE) kind=COMPACT_APPEND_CONTEXT;
    else if(shape==APPEND_SHAPE) kind=COMPACT_APPEND;
    else if(shape==COPYABLE_APPEND_SHAPE) kind=COMPACT_COPYABLE_APPEND;
    else if(shape==KNOWN_NON_TUPLE) kind=COMPACT_NON_TUPLE;
    else if(shape==MATCH_CONTEXT_SHAPE) kind=COMPACT_MATCH_CONTEXT;
    else if(shape==POSSIBLE_MATCH_CONTEXT) kind=COMPACT_POSSIBLE_MATCH_CONTEXT;
    else if(shape==CONSUMED_VALUE) kind=COMPACT_CONSUMED;
    else if(shape==MAP_SHAPE) kind=COMPACT_MAP;
    else if(shape==BITSTRING_SHAPE) kind=COMPACT_BITSTRING;
    else return 0;
    if(arity>COMPACT_ARITY_MASK) arity=0; /* Lose precision; runtime guards remain authoritative. */
    return (uint16_t)((kind<<COMPACT_KIND_SHIFT)|arity);
}
static uint32_t expanded_shape(uint16_t compact)
{
    unsigned kind=compact>>COMPACT_KIND_SHIFT; Uint arity=compact&COMPACT_ARITY_MASK;
    switch(kind) {
    case COMPACT_TUPLE:return TUPLE_SHAPE|(uint32_t)arity;
    case COMPACT_POSSIBLE_TUPLE:return TUPLE_SHAPE|POSSIBLE_TUPLE_ARITY|(uint32_t)arity;
    case COMPACT_LIST:return LIST_SHAPE;
    case COMPACT_LIST_OR_NIL:return LIST_OR_NIL_SHAPE;
    case COMPACT_NIL:return NIL_SHAPE;
    case COMPACT_APPEND:return APPEND_SHAPE;
    case COMPACT_COPYABLE_APPEND:return COPYABLE_APPEND_SHAPE;
    case COMPACT_NON_TUPLE:return KNOWN_NON_TUPLE;
    case COMPACT_MATCH_CONTEXT:return MATCH_CONTEXT_SHAPE;
    case COMPACT_POSSIBLE_MATCH_CONTEXT:return POSSIBLE_MATCH_CONTEXT;
    case COMPACT_CONSUMED:return CONSUMED_VALUE;
    case COMPACT_MAP:return MAP_SHAPE;
    case COMPACT_BITSTRING:return BITSTRING_SHAPE;
    case COMPACT_APPEND_CONTEXT:return APPEND_CONTEXT_SHAPE;
    case COMPACT_EXCEPTION_COMPONENT:return arity==1?EXCEPTION_CLASS_SHAPE:arity==2?EXCEPTION_TRACE_SHAPE:0;
    case COMPACT_ATOM:return ATOM_SHAPE;
    default:return 0;
    }
}
static uint8_t compact_y_shape(uint32_t fact)
{
    uint16_t compact=compact_shape(fact);
    unsigned arity=compact&COMPACT_ARITY_MASK;
    return (uint8_t)(((compact>>COMPACT_KIND_SHIFT)<<COMPACT_Y_KIND_SHIFT)|
                     (arity<=COMPACT_Y_ARITY_MASK?arity:0));
}
static uint32_t expanded_y_shape(uint8_t compact)
{
    uint16_t widened=(uint16_t)(((unsigned)(compact>>COMPACT_Y_KIND_SHIFT)<<COMPACT_KIND_SHIFT)|
                                (compact&COMPACT_Y_ARITY_MASK));
    return expanded_shape(widened);
}
typedef struct { BeamOp *op; Flow in; size_t function; int reached,queued,terminal_x_exception,terminal_y_exception; Uint terminal_y_register; } Node;
typedef struct {
    Node *nodes; size_t *labels,*queue,*parameter_offsets,*parameter_arities,*function_starts;
    uint32_t *return_shapes,*parameter_shapes,*parameter_candidates;
    uint8_t *parameter_states,*function_inferable;
    size_t count,function_count,parameter_count,head,tail,pending,deferred_tuple_uses,deferred_context_uses,deferred_append_uses;
    int defer_tuple_shapes,defer_append_shapes;
} Analysis;
static int function_params_inferable(const LoaderState *,const BeamOp *);
static int parameter_restarts_match(const Analysis *,size_t,size_t);
static int parameter_starts_match_checked(const Analysis *,size_t,size_t);
static int function_parameter(const Analysis *,size_t,BeamOpArg);
static void refine(Flow *,BeamOpArg,uint32_t);
static int defined(const uint64_t *set,size_t index) { return (int)((set[index/64]>>(index%64))&1); }
static int float_register(BeamOpArg arg)
{ return arg.type==TAG_l && arg.val>=0 && (Uint)arg.val<MAX_REG; }
static void define(uint64_t *set,size_t index) { set[index/64]|=UINT64_C(1)<<(index%64); }
static int root_source(LoaderState *st,const Flow *f,BeamOpArg arg)
{
    size_t reg=(Uint)arg.val&REG_MASK;
    switch(arg.type) {
    case TAG_x:return defined(f->x,reg) && expanded_shape(f->xs[reg])!=CONSUMED_VALUE;
    case TAG_y:return f->frame!=NO_FRAME && reg<f->frame && defined(f->y,reg) && expanded_y_shape(f->ys[reg])!=CONSUMED_VALUE;
    case TAG_a:return is_atom((Eterm)arg.val);
    case TAG_i:return IS_SSMALL(arg.val);
    case TAG_n:return 1;
    case TAG_q:{ Eterm term; return lb_beam_program_literal(st->program,st->space->atoms,arg.val,&term)==LB_BEAM_OK; }
    default:return 0;
    }
}
static uint32_t shape(LoaderState *st,const Flow *f,BeamOpArg arg)
{
    Eterm term; size_t reg=(Uint)arg.val&REG_MASK;
    if(arg.type==TAG_x) return expanded_shape(f->xs[reg]);
    if(arg.type==TAG_y) return expanded_y_shape(f->ys[reg]);
    if(arg.type==TAG_n) return NIL_SHAPE;
    if(arg.type==TAG_a) return ATOM_SHAPE;
    if(arg.type==TAG_i) return KNOWN_NON_TUPLE;
    if(arg.type==TAG_q && lb_beam_program_literal(st->program,st->space->atoms,arg.val,&term)==LB_BEAM_OK) {
        if(is_tuple(term)) return TUPLE_SHAPE | (uint32_t)arityval(*tuple_val(term));
        if(is_atom(term)) return ATOM_SHAPE;
        if(term==NIL) return NIL_SHAPE;
        if(is_list(term)) return LIST_SHAPE;
        if(lb_is_flatmap(term)) return MAP_SHAPE;
        LbBitstringView view; if(lb_bitstring_view(term,&view)) return BITSTRING_SHAPE;
        return KNOWN_NON_TUPLE;
    }
    return 0;
}
static int source(LoaderState *st,const Flow *f,BeamOpArg arg)
{ return root_source(st,f,arg) && !(shape(st,f,arg)&MATCH_CONTEXT_FLAGS); }
static int bs_match_flags_valid(LoaderState *st,BeamOpArg flags)
{
    Eterm options;
    if(flags.type==TAG_n) return 1;
    if(flags.type!=TAG_q || flags.val<0 || (Uint)flags.val>=st->program->literal_count ||
       lb_beam_program_literal(st->program,st->space->atoms,flags.val,&options)!=LB_BEAM_OK) return 0;
    while(is_list(options)) {
        LbBeamBytes name; Eterm option=CAR(list_val(options));
        if(!is_atom(option) || lb_atoms_name(st->space->atoms,option,&name)!=LB_ATOM_OK) return 0;
        if(!((name.size==6 && !memcmp(name.data,"signed",6)) ||
             (name.size==6 && !memcmp(name.data,"little",6)) ||
             (name.size==6 && !memcmp(name.data,"native",6)))) return 0;
        options=CDR(list_val(options));
    }
    return is_nil(options);
}
static int bs_match_flags_nonempty(LoaderState *st,BeamOpArg flags)
{
    Eterm options;
    if(flags.type==TAG_n) return 0;
    if(flags.type!=TAG_q || flags.val<0 || (Uint)flags.val>=st->program->literal_count ||
       lb_beam_program_literal(st->program,st->space->atoms,flags.val,&options)!=LB_BEAM_OK) return 0;
    return !is_nil(options);
}
static int bs_match_empty_flags_valid(LoaderState *st,BeamOpArg flags)
{
    Eterm options;
    if(flags.type==TAG_n) return 1;
    return flags.type==TAG_q && flags.val>=0 && (Uint)flags.val<st->program->literal_count &&
        lb_beam_program_literal(st->program,st->space->atoms,flags.val,&options)==LB_BEAM_OK &&
        is_nil(options);
}
static int match_context_source(Analysis *w,LoaderState *st,Flow *f,BeamOpArg arg,size_t function)
{
    if(!root_source(st,f,arg)) return 0;
    uint32_t fact=shape(st,f,arg);
    if(fact==MATCH_CONTEXT_SHAPE || fact==APPEND_CONTEXT_SHAPE) return 1;
    if(w->defer_tuple_shapes && (fact==POSSIBLE_MATCH_CONTEXT ||
       (fact==0 && function_parameter(w,function,arg)))) {
        refine(f,arg,POSSIBLE_MATCH_CONTEXT); ++w->deferred_context_uses; return 1;
    }
    return 0;
}
static int function_parameter(const Analysis *w,size_t function,BeamOpArg arg)
{ return arg.type==TAG_x && ((Uint)arg.val&REG_MASK)<w->parameter_arities[function]; }
static int destination(Flow *f,BeamOpArg arg,uint32_t fact)
{
    size_t reg=(Uint)arg.val&REG_MASK;
    if(arg.type==TAG_x) { define(f->x,reg); f->xs[reg]=compact_shape(fact); return 1; }
    if(arg.type==TAG_y && f->frame!=NO_FRAME && reg<f->frame) { define(f->y,reg); f->ys[reg]=compact_y_shape(fact); return 1; }
    return 0;
}
static void refine(Flow *f,BeamOpArg arg,uint32_t fact)
{
    /* Constants need no mutable fact slot. Their shape is read from the literal. */
    if(arg.type==TAG_x || arg.type==TAG_y) { if(!destination(f,arg,fact)) abort(); }
}
static int roots(const Flow *f,size_t live)
{
    size_t i;
    if(live>MAX_REG) return 0;
    for(i=0;i<live;++i) if(!defined(f->x,i)) return 0;
    if(f->frame!=NO_FRAME) for(i=0;i<f->frame;++i) if(!defined(f->y,i)) return 0;
    return 1;
}
static int arguments(const Flow *f,size_t arity)
{
    if(!roots(f,arity)) return 0;
    for(size_t i=0;i<arity;++i) if(expanded_shape(f->xs[i])==CONSUMED_VALUE || (expanded_shape(f->xs[i])&MATCH_CONTEXT_FLAGS)) return 0;
    return 1;
}
static int local_arguments(const Flow *f,size_t arity)
{
    if(!roots(f,arity)) return 0;
    for(size_t i=0;i<arity;++i) if(expanded_shape(f->xs[i])==CONSUMED_VALUE) return 0;
    return 1;
}
static void trim_live(Flow *f,size_t live)
{
    size_t i;
    for(i=live;i<MAX_REG;++i) { f->x[i/64]&=~(UINT64_C(1)<<(i%64)); f->xs[i]=0; }
}
static int definitely_non_tuple(uint32_t shape)
{
    return !(shape&TUPLE_SHAPE) &&
        (shape==KNOWN_NON_TUPLE || shape==ATOM_SHAPE || shape==LIST_SHAPE || shape==LIST_OR_NIL_SHAPE ||
         shape==NIL_SHAPE || shape==APPEND_SHAPE || shape==COPYABLE_APPEND_SHAPE ||
         shape==MAP_SHAPE || shape==BITSTRING_SHAPE);
}
static uint32_t common_shape(uint32_t a,uint32_t b)
{
    if(a==CONSUMED_VALUE || b==CONSUMED_VALUE) return CONSUMED_VALUE;
    if((a|b)&MATCH_CONTEXT_FLAGS) return a==b?a:POSSIBLE_MATCH_CONTEXT;
    if(a==b) return a;
    if((a==LIST_SHAPE || a==LIST_OR_NIL_SHAPE || a==NIL_SHAPE) &&
       (b==LIST_SHAPE || b==LIST_OR_NIL_SHAPE || b==NIL_SHAPE)) return LIST_OR_NIL_SHAPE;
    if((a==APPEND_SHAPE && b==COPYABLE_APPEND_SHAPE) ||
       (b==APPEND_SHAPE && a==COPYABLE_APPEND_SHAPE)) return COPYABLE_APPEND_SHAPE;
    if((a&TUPLE_SHAPE) && (b&TUPLE_SHAPE)) {
        uint32_t left=a&ARITY_MASK,right=b&ARITY_MASK;
        return TUPLE_SHAPE|((a|b)&POSSIBLE_TUPLE_ARITY)|(left<right?left:right);
    }
    if((a&TUPLE_SHAPE) && definitely_non_tuple(b)) return TUPLE_SHAPE|POSSIBLE_TUPLE_ARITY|(a&ARITY_MASK);
    if((b&TUPLE_SHAPE) && definitely_non_tuple(a)) return TUPLE_SHAPE|POSSIBLE_TUPLE_ARITY|(b&ARITY_MASK);
    if(definitely_non_tuple(a) && definitely_non_tuple(b)) return KNOWN_NON_TUPLE;
    return a==b?a:0;
}
static LbCodeStatus propagate(Analysis *w,size_t target,size_t function,const Flow *flow)
{
    Node *node; size_t r; int changed=0;
    REQUIRE(target<w->count && w->nodes[target].function==function);
    node=&w->nodes[target];
    Flow terminal;
    if(node->terminal_y_exception) {
        Uint reg=node->terminal_y_register;
        REQUIRE(flow->frame!=NO_FRAME && reg<flow->frame && roots(flow,0) && defined(flow->y,reg) &&
                expanded_y_shape(flow->ys[reg])!=CONSUMED_VALUE);
        terminal=*flow; terminal.frame=(size_t)reg+1; terminal.frame_allocation=NO_FRAME; terminal.heap=0;
        for(r=(size_t)reg+1;r<MAX_REG;++r) {
            terminal.y[r/64]&=~(UINT64_C(1)<<(r%64)); terminal.ys[r]=0;
        }
        flow=&terminal;
    } else if(node->terminal_x_exception) {
        /* OTP shares X-only terminal error labels across differently sized
         * frames. Every incoming stack must be GC-safe, but no Y access,
         * deallocation or normal continuation is permitted after this join.
         * Forget only these dead proof facts, never the actual runtime frame. */
        REQUIRE(roots(flow,0));
        terminal=*flow; terminal.frame=terminal.frame_allocation=NO_FRAME; terminal.heap=0;
        memset(terminal.y,0,sizeof(terminal.y)); memset(terminal.ys,0,sizeof(terminal.ys));
        flow=&terminal;
    }
    /* func_info is a terminal exception boundary, never normal argument reentry. */
    if(node->op->op==genop_int_func_start_5) return LB_CODE_OK;
    if(!node->reached) { node->in=*flow; node->reached=1; changed=1; }
    else {
        Flow *old=&node->in;
        REQUIRE(old->frame==flow->frame && old->frame_allocation==flow->frame_allocation &&
                old->matching==flow->matching);
        int exception_handler=old->exception_handler && flow->exception_handler;
        changed|=exception_handler!=old->exception_handler;
        old->exception_handler=exception_handler;
        for(r=0;r<BITS;++r) {
            uint64_t x=old->x[r]&flow->x[r],y=old->y[r]&flow->y[r];
            changed|=x!=old->x[r] || y!=old->y[r]; old->x[r]=x; old->y[r]=y;
        }
        for(r=0;r<MAX_REG;++r) {
            uint32_t x=defined(old->x,r)?common_shape(expanded_shape(old->xs[r]),expanded_shape(flow->xs[r])):0;
            uint32_t y=defined(old->y,r)?common_shape(expanded_y_shape(old->ys[r]),expanded_y_shape(flow->ys[r])):0;
            changed|=x!=expanded_shape(old->xs[r]) || y!=expanded_y_shape(old->ys[r]);
            old->xs[r]=compact_shape(x); old->ys[r]=compact_y_shape(y);
        }
        if(flow->heap<old->heap) { old->heap=flow->heap; changed=1; }
    }
    if(changed && !node->queued) {
        REQUIRE(w->pending<w->count);
        w->queue[w->tail]=target; w->tail=(w->tail+1)%w->count; ++w->pending; node->queued=1;
    }
    return LB_CODE_OK;
}
static int label(const LoaderState *st,BeamOpArg arg)
{
    return arg.type==TAG_f && arg.val>0 && (Uint)arg.val<st->label_count;
}
static LbCodeStatus select_arities(LoaderState *st,Analysis *w,size_t index,Flow *flow,BeamOp *op)
{
    BeamOpArg *a=op->a; size_t r,j; LbCodeStatus status;
    REQUIRE(source(st,flow,a[0]) && label(st,a[1]));
    REQUIRE(a[2].type==TAG_u && a[2].val>=2 && !(a[2].val&1) && (Uint)a[2].val==op->arity-3);
    if(a[2].val>512) return LB_CODE_LIMIT; /* finite proof/sort work, not a guest quota */
    status=propagate(w,w->labels[a[1].val],w->nodes[index].function,flow);
    if(status!=LB_CODE_OK) return status;
    for(r=3;r<op->arity;r+=2) {
        REQUIRE(a[r].type==TAG_u && (Uint)a[r].val<=MAX_ARITYVAL && label(st,a[r+1]));
        for(j=3;j<r;j+=2) REQUIRE(a[r].val!=a[j].val);
        refine(flow,a[0],TUPLE_SHAPE|(uint32_t)a[r].val);
        status=propagate(w,w->labels[a[r+1].val],w->nodes[index].function,flow);
        if(status!=LB_CODE_OK) return status;
    }
    return LB_CODE_OK;
}
static void forget_append_facts(Flow *f,int allow_copy_fallback)
{
    /* Revoking a header never preserves private-append permission. Calls
     * without a direct append argument may retain only a *type* fact:
     * their later operation uses the checked ordinary append path. */
    uint16_t copyable=allow_copy_fallback?compact_shape(COPYABLE_APPEND_SHAPE):0;
    uint8_t y_copyable=allow_copy_fallback?compact_y_shape(COPYABLE_APPEND_SHAPE):0;
    for(size_t i=0;i<MAX_REG;++i) {
        if(expanded_shape(f->xs[i])==APPEND_SHAPE) f->xs[i]=copyable;
        if(expanded_y_shape(f->ys[i])==APPEND_SHAPE) f->ys[i]=y_copyable;
    }
}
static int append_passed_to_call(const Flow *f,size_t arity)
{
    for(size_t i=0;i<arity;++i)
        if(expanded_shape(f->xs[i])==APPEND_SHAPE) return 1;
    return 0;
}
static int same_register(BeamOpArg a,BeamOpArg b)
{
    return (a.type==TAG_x || a.type==TAG_y) && a.type==b.type &&
        ((Uint)a.val&REG_MASK)==((Uint)b.val&REG_MASK);
}
/* Retain a runtime tuple test's proof for copies made immediately before it.
 * This bounded straight-line suffix admits only copies of the same source,
 * line markers and tests of that source. Labels, clobbers, calls and all other
 * operations stop the proof, so no alternate predecessor can bypass a copy. */
static void refine_tuple_copies(Flow *f,const Node *nodes,size_t index,BeamOpArg source,uint32_t fact)
{
    refine(f,source,fact);
    for(unsigned scanned=0;index && scanned<32;++scanned) {
        const BeamOp *prior=nodes[--index].op;
        if(prior->op==genop_line_1) continue;
        if(prior->op==genop_move_2 && same_register(prior->a[0],source)) {
            refine(f,prior->a[1],fact); continue;
        }
        if((prior->op==genop_is_tuple_2 || prior->op==genop_test_arity_3 || prior->op==genop_is_tagged_tuple_4) &&
           same_register(prior->a[1],source)) continue;
        break;
    }
}
static LbCodeStatus binary_flow(LoaderState *st,Analysis *w,size_t function,Flow *f,Flow *failed,BeamOp *op)
{
    BeamOpArg *a=op->a; uint32_t result_shape=BITSTRING_SHAPE; int consuming;
    st->program->error.offset=op->offset;
    REQUIRE(op->arity>=12 && a[5].type==TAG_u && a[5].val>=6 &&
            (Uint)a[5].val==op->arity-6 && a[5].val%6==0 && (Uint)a[5].val<=65536);
    REQUIRE(a[1].type==TAG_u && (Uint)a[1].val<=UINT32_MAX && a[2].type==TAG_u && roots(f,(Uint)a[2].val));
    /* Every X operand must survive the possible collection. Unlike addition,
     * construction reloads these values after allocating, not into scratch. */
    trim_live(f,(Uint)a[2].val);
    /* A callback may revoke writable ownership. Re-encode only this
     * formerly-proven source as ordinary append on the final strict pass;
     * its native implementation copies volatile views and checks binary type. */
    if((!w->defer_append_shapes || !w->function_inferable[function]) &&
       a[6].type==TAG_a && a[6].val==am_private_append) {
        uint32_t source_shape=shape(st,f,a[10]);
        if(source_shape!=APPEND_SHAPE && source_shape!=APPEND_CONTEXT_SHAPE)
            a[6].val=am_append; /* Keep the optimization only with proven writable provenance. */
    }
    consuming=a[6].type==TAG_a && a[6].val==am_private_append;
    for(size_t i=6;i<op->arity;i+=6) {
        BeamOpArg *s=a+i;
        REQUIRE(s[0].type==TAG_a);
        switch((Eterm)s[0].val) {
        case am_append:case am_private_append:REQUIRE(i==6);break;
        case am_binary:case am_integer:case am_float:case am_utf8:case am_utf16:case am_utf32:break;
        case am_string:continue; /* raw string offsets/sizes, checked by generator */
        default:return LB_CODE_FORMAT;
        }
        REQUIRE(source(st,f,s[4]) && source(st,f,s[5]));
        if(consuming && i>6) REQUIRE(!same_register(a[10],s[4]) && !same_register(a[10],s[5]));
    }
    if(consuming) {
        uint32_t fact=shape(st,f,a[10]);
        int proven=fact==APPEND_SHAPE || fact==APPEND_CONTEXT_SHAPE;
        st->program->error.stage="private append source/consumption proof";
        REQUIRE((a[10].type==TAG_x || a[10].type==TAG_y) &&
                (proven || (w->defer_append_shapes && w->function_inferable[function])));
        if(!proven) ++w->deferred_append_uses;
        REQUIRE(destination(f,a[10],CONSUMED_VALUE));
        /* A provisional pass may discover private callee parameter facts.
         * Never infer writable ownership from its unproved result: the final
         * strict pass must establish the source before code can be emitted. */
        result_shape=proven?APPEND_SHAPE:BITSTRING_SHAPE;
    } else if(a[6].val==am_append && shape(st,f,a[10])==APPEND_SHAPE) {
        result_shape=APPEND_SHAPE; /* zero extension also preserves this representation */
    } else if(a[6].val==am_append && shape(st,f,a[10])==COPYABLE_APPEND_SHAPE) {
        result_shape=COPYABLE_APPEND_SHAPE; /* retain type only, never private ownership */
    }
    /* Appending can revoke a header reached through aliases. Even other
     * append facts are conservatively forgotten; only the new result gets one. */
    if(a[6].val==am_append || consuming) forget_append_facts(f,0);
    /* Any failure can follow private consumption, but never destination
     * publication. Poison also covers early failure with the original value
     * still in its initialized slot. Promise no heap credit on that edge. */
    *failed=*f; failed->heap=0;
    REQUIRE(destination(f,a[4],result_shape)); f->heap=(Uint)a[1].val;
    return LB_CODE_OK;
}
static int native_always_raises(const BifEntry *native)
{
    if(!native || native->module!=am_erlang) return 0;
    if(native->name==am_error) return native->arity>=1 && native->arity<=3;
    if(native->name==am_exit || native->name==am_throw) return native->arity==1;
    return native->name==am_nif_error && (native->arity==1 || native->arity==2);
}
static int native_call_always_raises(const BifEntry *native,const Flow *flow)
{
    return native_always_raises(native) ||
        (native && native->module==am_erlang && native->name==am_raise && native->arity==3 &&
         expanded_shape(flow->xs[0])==EXCEPTION_CLASS_SHAPE &&
         expanded_shape(flow->xs[2])==EXCEPTION_TRACE_SHAPE);
}
static int raw_raise_has_literal_class(const Node *nodes,size_t index)
{
    while(index && nodes[index-1].op->op==genop_line_1) --index;
    if(!index) return 0;
    const BeamOp *move=nodes[index-1].op;
    return move->op==genop_move_2 && move->a[0].type==TAG_a &&
        move->a[1].type==TAG_x && move->a[1].val==0 &&
        ((Eterm)move->a[0].val==am_error || (Eterm)move->a[0].val==am_exit ||
         (Eterm)move->a[0].val==am_throw);
}
static int straight_line_local_always_raises_depth(LoaderState *st,const Analysis *w,size_t target,unsigned depth)
{
    if(depth>=16 || target>=w->count) return 0;
    size_t function=w->nodes[target].function;
    for(size_t i=target;i<w->count && w->nodes[i].function==function;++i) {
        const BeamOp *op=w->nodes[i].op;
        switch(op->op) {
        case genop_label_1: case genop_line_1: case genop_allocate_2:
        case genop_move_2: case genop_test_heap_2: case genop_put_tuple2_2:
        case genop_get_tuple_element_3: case genop_trim_2:
        case genop_call_2: case genop_call_ext_2:
            break;
        case genop_call_only_2: case genop_call_last_3: {
            if(op->arity<2 || op->a[1].type!=TAG_f || op->a[1].val<=0 ||
               (Uint)op->a[1].val>=st->label_count) return 0;
            size_t callee=w->labels[op->a[1].val];
            if(callee==SIZE_MAX || callee==0 || callee>=w->count ||
               w->nodes[callee-1].op->op!=genop_int_func_start_5) return 0;
            return straight_line_local_always_raises_depth(st,w,callee,depth+1);
        }
        case genop_call_ext_only_2: case genop_call_ext_last_3:
            return op->a[1].type==TAG_u && (Uint)op->a[1].val<st->beam.imports.count &&
                native_always_raises(st->bif_imports[op->a[1].val]);
        default:
            return 0;
        }
    }
    return 0;
}
static int straight_line_local_always_raises(LoaderState *st,const Analysis *w,size_t target)
{ return straight_line_local_always_raises_depth(st,w,target,0); }
static uint32_t native_return_shape(LoaderState *st,size_t import)
{
    if(import>=st->beam.imports.count || !st->bif_imports) return 0;
    const BifEntry *native=st->bif_imports[import];
    if(native==&st->space->engine->bifs[BIF_integer_to_list_1] ||
       native==&st->space->engine->bifs[BIF_integer_to_list_2]) return LIST_SHAPE;
    if(native==&st->space->engine->bifs[BIF_list_to_tuple_1] ||
       native==&st->space->engine->bifs[BIF_make_tuple_2]) return TUPLE_SHAPE;
    if(native==&st->space->engine->bifs[BIF_spawn_opt_4] ||
       native==&st->space->engine->bifs[BIF_SIZE+22u]) return TUPLE_SHAPE|2;
    return 0;
}
/* A bounded structural proof, not a compiler annotation or speculative recursive
 * summary. Starting at the actual local entry, only inert labels/line metadata
 * may precede a native tail call. Any branch, operation or other return path
 * leaves the result unknown. The callee itself still passes full verification. */
static uint32_t forwarding_return_shape(LoaderState *st,const Analysis *w,size_t target)
{
    size_t function=w->nodes[target].function;
    for(size_t i=target;i<w->count && w->nodes[i].function==function;++i) {
        const BeamOp *op=w->nodes[i].op;
        if(op->op==genop_label_1 || op->op==genop_line_1) continue;
        if(op->op!=genop_call_ext_only_2 || op->arity!=2 || op->a[0].type!=TAG_u || op->a[1].type!=TAG_u) return 0;
        size_t import=(Uint)op->a[1].val;
        if(import>=st->beam.imports.count || (Uint)op->a[0].val!=st->beam.imports.entries[import].arity) return 0;
        return native_return_shape(st,import);
    }
    return 0;
}
/* The low arity bits are the minimum tuple size guaranteed across returns;
 * the possible bit records non-tuples or larger tuple alternatives. */
static uint32_t local_return_tuple_shape(const Analysis *w,size_t target)
{
    size_t function=w->nodes[target].function; uint32_t arity=0; int saw_tuple=0, saw_other=0;
    for(size_t i=0;i<w->count;++i) {
        if(!w->nodes[i].reached || w->nodes[i].function!=function ||
           w->nodes[i].op->op!=genop_return_0) continue;
        uint32_t fact=expanded_shape(w->nodes[i].in.xs[0]);
        if(fact&TUPLE_SHAPE) {
            uint32_t current=fact&ARITY_MASK;
            if(fact&POSSIBLE_TUPLE_ARITY) saw_other=1;
            arity=saw_tuple && arity<current?arity:current; saw_tuple=1;
        } else if(definitely_non_tuple(fact)) saw_other=1;
        else return 0;
    }
    if(!saw_tuple) return 0;
    return TUPLE_SHAPE|(saw_other?POSSIBLE_TUPLE_ARITY:0)|arity;
}
static int position_check_after(const Node *nodes,size_t count,size_t index,BeamOpArg context)
{
    for(size_t i=index+1;i<count && nodes[i].function==nodes[index].function;++i) {
        unsigned op=nodes[i].op->op;
        if(op==genop_label_1 || op==genop_line_1) continue;
        return op==genop_bs_get_position_3 && nodes[i].op->a[0].type==context.type &&
               nodes[i].op->a[0].val==context.val;
    }
    return 0;
}
static int immediate_setelement_result(const LoaderState *st,const Node *nodes,size_t index)
{
    size_t previous=index;
    while(previous && nodes[previous-1].op->op==genop_line_1) --previous;
    if(!previous) return 0;
    const BeamOp *op=nodes[previous-1].op;
    if(op->op!=genop_call_ext_2 || op->a[0].type!=TAG_u || op->a[0].val!=3 ||
       op->a[1].type!=TAG_u || (Uint)op->a[1].val>=st->beam.imports.count || !st->bif_imports)
        return 0;
    return st->bif_imports[op->a[1].val]==&st->space->engine->bifs[BIF_setelement_3];
}
static uint32_t setelement_result_shape(LoaderState *st,const Flow *flow,const Node *nodes,size_t index)
{
    uint32_t tuple_shape=shape(st,flow,(BeamOpArg){TAG_x,1});
    uint32_t minimum=tuple_shape&ARITY_MASK; Sint position=0; size_t previous=index;
    while(previous && nodes[previous-1].op->op==genop_line_1) --previous;
    if(previous) {
        const BeamOp *move=nodes[previous-1].op;
        if(move->op==genop_move_2 && move->a[0].type==TAG_i &&
           move->a[1].type==TAG_x && move->a[1].val==0) position=move->a[0].val;
    }
    if((tuple_shape&TUPLE_SHAPE) && !(tuple_shape&POSSIBLE_TUPLE_ARITY)) return tuple_shape;
    if(position<1 || (Uint)position>MAX_ARITYVAL) {
        return tuple_shape&TUPLE_SHAPE?TUPLE_SHAPE|POSSIBLE_TUPLE_ARITY|minimum:0;
    }
    if((Uint)position>minimum) minimum=(Uint)position;
    return TUPLE_SHAPE|POSSIBLE_TUPLE_ARITY|minimum;
}
static int plain_bif_allowed(const LbEngine *engine,const BifEntry *native,size_t arity)
{
    static const unsigned unary[]={BIF_is_function_1,BIF_map_size_1,BIF_is_integer_1,BIF_hd_1,BIF_tl_1,BIF_is_list_1,BIF_not_1,BIF_is_atom_1,BIF_is_binary_1,
#define LB_TYPE_PREDICATE(name, predicate) BIF_##name##_1,
#include "type_predicates.def"
#undef LB_TYPE_PREDICATE
        BIF_tuple_size_1,BIF_node_1,BIF_splus_1,BIF_bit_size_1};
    static const unsigned binary[]={BIF_is_function_2,BIF_map_get_2,BIF_element_2,BIF_max_2,BIF_min_2,BIF_is_map_key_2,BIF_seq_2,BIF_seqeq_2,BIF_sneq_2,BIF_sneqeq_2,BIF_slt_2,BIF_sle_2,BIF_sgt_2,BIF_sge_2,BIF_and_2,BIF_or_2,BIF_xor_2,BIF_is_record_2};
    static const unsigned ternary[]={BIF_is_integer_3,BIF_is_record_3};
    if(!native || native->kind!=BIF_KIND_GUARD) return 0;
    const unsigned *ids; size_t count;
    if(arity==1) { ids=unary; count=sizeof(unary)/sizeof(*unary); }
    else if(arity==2) { ids=binary; count=sizeof(binary)/sizeof(*binary); }
    else if(arity==3) { ids=ternary; count=sizeof(ternary)/sizeof(*ternary); }
    else return 0;
    for(size_t i=0;i<count;++i) if(native==&engine->bifs[ids[i]]) return 1;
    return 0;
}
static LbCodeStatus verify_flow(LoaderState *st,Analysis *w)
{
    LbBeamProgram *p=st->program; BeamOp *op;
    Node *nodes=w->nodes; size_t *labels=w->labels,n=w->count,i,function=0,steps=0;
    memset(nodes,0,n*sizeof(*nodes)); w->head=w->tail=w->pending=w->deferred_tuple_uses=w->deferred_context_uses=w->deferred_append_uses=0;
    for(i=0;i<st->label_count;++i) labels[i]=SIZE_MAX;
    for(op=p->ops,i=0;op;op=op->next,++i) {
        p->error.stage="operation arity"; p->error.offset=op->offset;
        REQUIRE(op->op<NUM_GENERIC_OPS);
        switch(op->op) {
        case genop_int_func_start_5:case genop_int_func_end_2:case genop_int_code_end_0:case genop_i_nif_padding_0:case genop_label_1:case genop_line_1:case genop_nif_start_0:
        case genop_on_load_0:break;
        case genop_move_2:case genop_swap_2:case genop_test_heap_2:case genop_put_tuple2_2:case genop_update_record_5:case genop_return_0:case genop_send_0:
        case genop_loop_rec_2:case genop_loop_rec_end_1:case genop_remove_message_0:
        case genop_wait_1:case genop_wait_timeout_2:case genop_timeout_0:
        case genop_recv_marker_reserve_1:case genop_recv_marker_bind_2:
        case genop_recv_marker_clear_1:case genop_recv_marker_use_1:
        case genop_call_2:case genop_call_only_2:case genop_call_last_3:
        case genop_call_fun_1:case genop_call_fun2_3:
        case genop_call_ext_2:case genop_call_ext_only_2:case genop_call_ext_last_3:
        case genop_apply_1:case genop_apply_last_2:
        case genop_allocate_2:case genop_allocate_heap_3:case genop_init_yregs_1:case genop_deallocate_1:case genop_trim_2:
        case genop_jump_1:case genop_is_lt_3:case genop_is_ge_3:case genop_is_eq_3:case genop_is_ne_3:case genop_is_eq_exact_3:case genop_is_ne_exact_3:case genop_is_nil_2:case genop_is_pid_2:case genop_is_reference_2:case genop_is_atom_2:
        case genop_is_binary_2:case genop_is_list_2:case genop_is_integer_2:case genop_is_map_2:case genop_is_function_2:case genop_is_function2_3:
        case genop_is_float_2:case genop_is_number_2:case genop_is_port_2:case genop_is_boolean_2:case genop_is_bitstr_2:
        case genop_is_tuple_2:case genop_is_any_native_record_2:case genop_test_arity_3:case genop_is_tagged_tuple_4:case genop_select_tuple_arity_3:case genop_select_val_3:
        case genop_is_nonempty_list_2:case genop_get_tuple_element_3:case genop_set_tuple_element_3:case genop_get_list_3:
        case genop_get_hd_2:case genop_get_tl_2:case genop_put_list_3:case genop_gc_bif1_5:case genop_gc_bif2_6:
        case genop_bs_match_3:case genop_bs_match_string_4:case genop_bs_get_utf8_5:case genop_bs_skip_utf8_4:case genop_bs_get_float2_7:case genop_bs_get_utf16_5:case genop_bs_skip_utf16_4:case genop_bs_get_utf32_5:case genop_bs_skip_utf32_4:case genop_bs_skip_bits2_5:case genop_bs_get_binary2_7:case genop_bs_get_integer2_7:case genop_bs_start_match3_4:case genop_bs_start_match4_4:case genop_bs_get_position_3:case genop_bs_set_position_2:case genop_bs_get_tail_3:case genop_gc_bif3_7:case genop_bs_create_bin_6:case genop_bs_init_writable_0:case genop_bif0_2:case genop_bif1_4:case genop_bif2_5:case genop_bif3_6:case genop_make_fun3_3:
        case genop_new_map_3:case genop_put_map_assoc_5:case genop_put_map_exact_5:
        case genop_get_map_elements_3:case genop_has_map_fields_3:
        case genop_fconv_2:case genop_fmove_2:case genop_fload_2:case genop_fstore_2:case genop_fadd_4:case genop_fsub_4:case genop_i_fsub_3:case genop_fnegate_3:case genop_fmul_4:case genop_i_fmul_3:case genop_fdiv_4:case genop_i_fdiv_3:
        case genop_badmatch_1:case genop_badrecord_1:case genop_case_end_1:case genop_if_end_0:case genop_raw_raise_0:case genop_catch_2:case genop_try_2:case genop_raise_2:case genop_try_end_1:case genop_catch_end_1:
        case genop_try_case_1:case genop_try_case_end_1:case genop_build_stacktrace_0:break;
        default:p->error.stage=gen_opc[op->op].name; return LB_CODE_UNSUPPORTED;
        }
        if(op->op!=genop_select_val_3 && op->op!=genop_bs_match_3 && op->op!=genop_put_tuple2_2 && op->op!=genop_update_record_5 && op->op!=genop_init_yregs_1 && op->op!=genop_select_tuple_arity_3 && op->op!=genop_bs_create_bin_6 && op->op!=genop_make_fun3_3 &&
           op->op!=genop_new_map_3 && op->op!=genop_put_map_assoc_5 && op->op!=genop_put_map_exact_5 &&
           op->op!=genop_get_map_elements_3 && op->op!=genop_has_map_fields_3)
            REQUIRE(op->arity==(unsigned)gen_opc[op->op].arity);
        if(op->op==genop_nif_start_0) {
            /* Native entry metadata, not permission to install or call a NIF. */
            p->error.stage="nif_start entry metadata";
            REQUIRE(function>0); /* Native metadata may follow allocation/init prologues. */
        }
        nodes[i].op=op;
        if(op->op==genop_int_func_start_5) {
            size_t r,arity=(Uint)op->a[4].val;
            ++function; w->function_starts[function]=i;
            w->function_inferable[function]=(uint8_t)function_params_inferable(st,op);
            nodes[i].reached=nodes[i].queued=1; nodes[i].in.frame=nodes[i].in.frame_allocation=NO_FRAME;
            labels[op->a[0].val]=i;
            for(r=0;r<arity;++r) {
                define(nodes[i].in.x,r);
                nodes[i].in.xs[r]=compact_shape(w->parameter_shapes[w->parameter_offsets[function]+r]);
            }
            w->queue[w->tail++]=i; ++w->pending;
        }
        nodes[i].function=function;
        if(op->op==genop_label_1) labels[op->a[0].val]=i;
    }
    /* Compute metadata-only terminal tails once, rather than rescanning a
     * potentially long label chain at every dataflow edge. */
    for(i=n;i>0;--i) {
        Node *node=&nodes[i-1]; op=node->op;
        node->terminal_x_exception=op->op==genop_if_end_0 ||
            ((op->op==genop_badmatch_1 || op->op==genop_case_end_1 ||
              op->op==genop_badrecord_1) && op->a[0].type==TAG_x);
        node->terminal_y_exception=op->op==genop_badmatch_1 && op->a[0].type==TAG_y;
        node->terminal_y_register=node->terminal_y_exception?(Uint)op->a[0].val:0;
        if((op->op==genop_label_1 || op->op==genop_line_1) && i<n && nodes[i].function==node->function) {
            node->terminal_x_exception=nodes[i].terminal_x_exception;
            node->terminal_y_exception=nodes[i].terminal_y_exception;
            node->terminal_y_register=nodes[i].terminal_y_register;
        }
    }
    p->error.stage="lambda target admission";
    for(i=0;i<p->lambda_count;++i) {
        const LbBeamLambda *lambda=&p->lambdas[i]; size_t target=labels[lambda->label];
        REQUIRE(target>0 && target<n && nodes[target-1].op->op==genop_int_func_start_5);
        const BeamOp *header=nodes[target-1].op;
        REQUIRE(header->a[3].type==TAG_a && (Eterm)header->a[3].val==lambda->function);
        REQUIRE(header->a[4].type==TAG_u && (Uint)header->a[4].val==lambda->arity);
    }
    w->tail%=n;
    while(w->pending) {
        size_t index=w->queue[w->head],next=index+1,alternate=SIZE_MAX,r,live,imp;
        Flow f=nodes[index].in,failed;
        const Flow *failure=&nodes[index].in;
        BeamOpArg *a; LbCodeStatus status; uint32_t s,t;
        int terminal=0;
        w->head=(w->head+1)%n; --w->pending; nodes[index].queued=0;
        if(++steps>16777216) { p->error.stage="flow analysis step limit"; return LB_CODE_LIMIT; }
        op=nodes[index].op; a=op->a;
        p->error.stage=gen_opc[op->op].name; p->error.offset=op->offset;
        switch(op->op) {
        case genop_int_func_start_5:case genop_label_1:case genop_line_1:case genop_nif_start_0:case genop_on_load_0:break;
        case genop_i_nif_padding_0:terminal=1;break;
        case genop_fconv_2:
            REQUIRE(source(st,&f,a[0]) && float_register(a[1])); break;
        case genop_fmove_2:
            if(float_register(a[0])) {
                if(f.heap<2) { p->error.stage="float store heap credit"; return LB_CODE_FORMAT; }
                if(!destination(&f,a[1],0)) { p->error.stage="float store destination"; return LB_CODE_FORMAT; }
                f.heap-=2;
            } else {
                if(!source(st,&f,a[0])) { p->error.stage="float load source"; return LB_CODE_FORMAT; }
                if(!float_register(a[1])) { p->error.stage="float load destination"; return LB_CODE_FORMAT; }
            }
            break;
        case genop_fload_2:
            REQUIRE((source(st,&f,a[0]) || root_source(st,&f,a[0])) && float_register(a[1])); break;
        case genop_fadd_4:case genop_fsub_4:case genop_fmul_4:
            REQUIRE(a[0].type==TAG_p && float_register(a[1]) && float_register(a[2]) && float_register(a[3])); break;
        case genop_fnegate_3:
            REQUIRE(a[0].type==TAG_p && float_register(a[1]) && float_register(a[2])); break;
        case genop_i_fsub_3:case genop_i_fmul_3:case genop_i_fdiv_3:
            REQUIRE(float_register(a[0]) && float_register(a[1]) && float_register(a[2])); break;
        case genop_fdiv_4:
            REQUIRE(a[0].type==TAG_p && float_register(a[1]) && float_register(a[2]) && float_register(a[3])); break;
        case genop_fstore_2:
            p->error.stage="float store flow";
            REQUIRE(float_register(a[0]) && f.heap>=2 && destination(&f,a[1],0)); f.heap-=2; break;
        case genop_move_2:
            REQUIRE(root_source(st,&f,a[0])); s=shape(st,&f,a[0]); REQUIRE(destination(&f,a[1],s)); break;
        case genop_swap_2:
            REQUIRE(root_source(st,&f,a[0]) && root_source(st,&f,a[1])); s=shape(st,&f,a[0]); t=shape(st,&f,a[1]);
            REQUIRE(destination(&f,a[0],t) && destination(&f,a[1],s)); break;
        case genop_test_heap_2:
            REQUIRE(a[0].type==TAG_u && a[1].type==TAG_u && a[0].val>=0 && a[1].val>=0);
            live=(Uint)a[1].val; REQUIRE(roots(&f,live)); trim_live(&f,live); f.heap=(Uint)a[0].val; break;
        case genop_allocate_2:case genop_allocate_heap_3:
            r=op->op==genop_allocate_2?1:2;
            REQUIRE(f.frame==NO_FRAME && f.frame_allocation==NO_FRAME && a[0].type==TAG_u && (Uint)a[0].val<MAX_REG && a[r].type==TAG_u);
            live=(Uint)a[r].val; REQUIRE(roots(&f,live)); trim_live(&f,live);
            f.frame=f.frame_allocation=(Uint)a[0].val; memset(f.y,0,sizeof(f.y)); memset(f.ys,0,sizeof(f.ys));
            if(r==2) { REQUIRE(a[1].type==TAG_u); f.heap=(Uint)a[1].val; } else f.heap=0;
            break;
        case genop_init_yregs_1:
            REQUIRE(a[0].type==TAG_u && (Uint)a[0].val==op->arity-1);
            for(r=1;r<op->arity;++r) REQUIRE(a[r].type==TAG_y && destination(&f,a[r],0));
            break;
        case genop_deallocate_1:
            REQUIRE(a[0].type==TAG_u &&
                    (f.frame==(Uint)a[0].val ||
                     (f.frame==NO_FRAME && f.frame_allocation==NO_FRAME && a[0].val==0)));
            f.frame=f.frame_allocation=NO_FRAME; memset(f.y,0,sizeof(f.y)); memset(f.ys,0,sizeof(f.ys)); break;
        case genop_trim_2:
            REQUIRE(f.frame!=NO_FRAME && a[0].type==TAG_u && a[1].type==TAG_u && (Uint)a[0].val<=f.frame);
            REQUIRE((Uint)a[1].val==f.frame-(Uint)a[0].val);
            f.frame=(Uint)a[1].val;
            for(r=0;r<MAX_REG;++r) {
                if(r<f.frame && defined(f.y,r+(Uint)a[0].val)) define(f.y,r);
                else f.y[r/64]&=~(UINT64_C(1)<<(r%64));
                f.ys[r]=r<f.frame?f.ys[r+(Uint)a[0].val]:0;
            }
            break;
        case genop_put_tuple2_2:
            REQUIRE(a[1].type==TAG_u && a[1].val>0 && (Uint)a[1].val<=MAX_ARITYVAL && (Uint)a[1].val==op->arity-2);
            REQUIRE(f.heap>=(Uint)a[1].val+1);
            for(r=2;r<op->arity;++r) REQUIRE(root_source(st,&f,a[r]));
            REQUIRE(destination(&f,a[0],TUPLE_SHAPE|(uint32_t)a[1].val)); f.heap-=(Uint)a[1].val+1; break;
        case genop_update_record_5: {
            Uint size,count,destination_register; uint32_t old_destination_shape;
            p->error.stage="update_record header admission";
            REQUIRE(a[0].type==TAG_a && a[1].type==TAG_u && a[1].val>0 && (Uint)a[1].val<MAX_REG-1);
            p->error.stage="update_record source admission";
            REQUIRE(source(st,&f,a[2]));
            p->error.stage="update_record destination class admission";
            REQUIRE(a[3].type==TAG_x || a[3].type==TAG_y);
            /* Copy updates may target a fresh register; the old destination is
             * only a live value when an update explicitly aliases it. */
            p->error.stage="update_record update-count admission";
            REQUIRE(a[4].type==TAG_u && a[4].val>=2 && !(a[4].val&1) && (Uint)a[4].val<=2*(Uint)a[1].val);
            size=(Uint)a[1].val; count=(Uint)a[4].val;
            p->error.stage="update_record heap admission";
            REQUIRE(op->arity==5+count && f.heap>=size+1);
            /* The instruction performs its own tuple and arity check and
             * raises badrecord when the source does not match. */
            if(a[2].type==TAG_x) REQUIRE(((Uint)a[2].val&REG_MASK)!=MAX_REG-1);
            for(r=5;r<op->arity;r+=2) {
                p->error.stage="update_record field admission";
                REQUIRE(a[r].type==TAG_u && a[r].val>0 && (Uint)a[r].val<=size && source(st,&f,a[r+1]));
                REQUIRE(a[r+1].type!=TAG_x || ((Uint)a[r+1].val&REG_MASK)!=MAX_REG-1);
            }
            destination_register=(Uint)a[3].val&REG_MASK;
            old_destination_shape=a[3].type==TAG_x?expanded_shape(f.xs[destination_register]):
                expanded_y_shape(f.ys[destination_register]);
            p->error.stage="update_record destination admission";
            REQUIRE(!(a[3].type==TAG_x && destination_register==MAX_REG-1));
            define(f.x,MAX_REG-1); f.xs[MAX_REG-1]=compact_shape(old_destination_shape);
            REQUIRE(destination(&f,a[3],TUPLE_SHAPE|size)); f.heap-=size+1;
            break;
        }
        case genop_put_list_3:
            REQUIRE(f.heap>=2 && root_source(st,&f,a[0]) && root_source(st,&f,a[1]));
            REQUIRE(destination(&f,a[2],LIST_SHAPE)); f.heap-=2; break;
        case genop_set_tuple_element_3: {
            uint32_t tuple_shape;
            p->error.stage="set_tuple_element element admission";
            REQUIRE(source(st,&f,a[0]));
            p->error.stage="set_tuple_element fresh tuple proof";
            REQUIRE(a[1].type==TAG_x && a[1].val==0 && immediate_setelement_result(st,nodes,index));
            tuple_shape=shape(st,&f,a[1]);
            p->error.stage="set_tuple_element tuple shape admission";
            REQUIRE(tuple_shape&TUPLE_SHAPE);
            p->error.stage="set_tuple_element position admission";
            /* The opcode offset is zero-based from the first tuple element. */
            REQUIRE(a[2].type==TAG_u && (Uint)a[2].val<(tuple_shape&ARITY_MASK));
            break;
        }
        case genop_get_tuple_element_3:
            REQUIRE(source(st,&f,a[0]) && a[1].type==TAG_u);
            REQUIRE(a[0].type!=TAG_x || ((Uint)a[0].val&REG_MASK)!=MAX_REG-1);
            p->error.stage="tuple element index admission";
            REQUIRE((Uint)a[1].val<MAX_ARITYVAL);
            p->error.stage="tuple element destination admission";
            REQUIRE(destination(&f,a[2],0)); break;
        case genop_get_list_3:case genop_get_hd_2:case genop_get_tl_2:
            /* Every dispatched form checks the cons tag before dereference. */
            REQUIRE(source(st,&f,a[0]));
            p->error.stage="list head destination admission";
            REQUIRE(destination(&f,a[1],0));
            if(op->op==genop_get_list_3) {
                p->error.stage="list tail destination admission";
                REQUIRE(destination(&f,a[2],0));
            }
            break;
        case genop_gc_bif3_7: {
            REQUIRE(a[0].type==TAG_p || (a[0].type==TAG_f && label(st,a[0])));
            REQUIRE(a[1].type==TAG_u && roots(&f,(Uint)a[1].val));
            REQUIRE(a[2].type==TAG_u && (Uint)a[2].val<st->beam.imports.count);
            REQUIRE(st->bif_imports[a[2].val]==&st->space->engine->bifs[BIF_binary_part_3]);
            REQUIRE(source(st,&f,a[3]) && source(st,&f,a[4]) && source(st,&f,a[5]));
            trim_live(&f,(Uint)a[1].val); failed=f; failure=&failed;
            REQUIRE(destination(&f,a[6],BITSTRING_SHAPE)); f.heap=0;
            if(a[0].type==TAG_f) {
                alternate=labels[a[0].val];
                REQUIRE(alternate<n && nodes[alternate].function==nodes[index].function);
            }
            break;
        }
        case genop_gc_bif1_5:case genop_gc_bif2_6: {
            size_t arity=op->op==genop_gc_bif1_5?1:2;
            REQUIRE(a[1].type==TAG_u && a[2].type==TAG_u && (Uint)a[2].val<st->beam.imports.count);
            const BifEntry *native=st->bif_imports[a[2].val];
            if(arity==1) {
                if(native!=&st->space->engine->bifs[BIF_floor_1] &&
                   native!=&st->space->engine->bifs[BIF_ceil_1] &&
                   native!=&st->space->engine->bifs[BIF_trunc_1] &&
                   native!=&st->space->engine->bifs[BIF_round_1] &&
                   native!=&st->space->engine->bifs[BIF_float_1] &&
                   native!=&st->space->engine->bifs[BIF_abs_1] &&
                   native!=&st->space->engine->bifs[BIF_map_size_1] &&
                   native!=&st->space->engine->bifs[BIF_length_1] &&
                   native!=&st->space->engine->bifs[BIF_byte_size_1] &&
                   native!=&st->space->engine->bifs[BIF_bit_size_1] &&
                   native!=&st->space->engine->bifs[BIF_sminus_1] &&
                   native!=&st->space->engine->bifs[BIF_splus_1] &&
                   native!=&st->space->engine->bifs[BIF_bnot_1] &&
                   native!=&st->space->engine->bifs[BIF_list_to_atom_1]) return LB_CODE_UNSUPPORTED;
            } else if(native!=&st->space->engine->bifs[BIF_splus_2] &&
                      native!=&st->space->engine->bifs[BIF_sminus_2] &&
                      native!=&st->space->engine->bifs[BIF_stimes_2] &&
                      native!=&st->space->engine->bifs[BIF_intdiv_2] &&
                      native!=&st->space->engine->bifs[BIF_rem_2] &&
                      native!=&st->space->engine->bifs[BIF_band_2] &&
                      native!=&st->space->engine->bifs[BIF_bor_2] &&
                      native!=&st->space->engine->bifs[BIF_bxor_2] &&
                      native!=&st->space->engine->bifs[BIF_bsl_2] &&
                      native!=&st->space->engine->bifs[BIF_bsr_2] &&
                      native!=&st->space->engine->bifs[BIF_div_2]) return LB_CODE_UNSUPPORTED;
            int match_size_context=arity==1 &&
                (native==&st->space->engine->bifs[BIF_byte_size_1] ||
                 native==&st->space->engine->bifs[BIF_bit_size_1]) &&
                shape(st,&f,a[3])==MATCH_CONTEXT_SHAPE;
            REQUIRE(roots(&f,(Uint)a[1].val) &&
                    (source(st,&f,a[3]) || (match_size_context && root_source(st,&f,a[3]))) &&
                    (arity==1 || source(st,&f,a[4])));
            trim_live(&f,(Uint)a[1].val); failed=f; failure=&failed;
            REQUIRE(destination(&f,a[3+arity],0));
            if(a[0].type==TAG_p) REQUIRE(!a[0].val);
            else {
                REQUIRE(label(st,a[0])); alternate=labels[a[0].val];
                REQUIRE(alternate<n && nodes[alternate].function==nodes[index].function);
            }
            break;
        }
        case genop_make_fun3_3:
            REQUIRE(op->arity>=3 && a[0].type==TAG_u && (Uint)a[0].val<p->lambda_count && a[2].type==TAG_u);
            live=(Uint)a[2].val;
            REQUIRE(live==p->lambdas[a[0].val].num_free && op->arity==3+live);
            for(r=0;r<live;++r) REQUIRE(source(st,&f,a[3+r]));
            if(live) { REQUIRE(f.heap>=LB_FUN_WORDS+live); f.heap-=LB_FUN_WORDS+live; }
            REQUIRE(destination(&f,a[1],0)); break;
        case genop_bif1_4:case genop_bif2_5:case genop_bif3_6: {
            size_t arity=op->op==genop_bif1_4?1:op->op==genop_bif2_5?2:3;
            REQUIRE(a[1].type==TAG_u && (Uint)a[1].val<st->beam.imports.count);
            const BifEntry *native=st->bif_imports[a[1].val];
            if(native==&st->space->engine->bifs[BIF_get_1] && arity==1) {
                /* Compiler get/1 lowers to non-allocating dictionary lookup,
                 * not a guard. Never admit an exceptional guard edge. */
                REQUIRE((a[0].type==TAG_p || a[0].type==TAG_f) && !a[0].val);
            } else if(!plain_bif_allowed(st->space->engine,native,arity)) return LB_CODE_UNSUPPORTED;
            for(r=0;r<arity;++r) REQUIRE(source(st,&f,a[2+r]));
            if(native==&st->space->engine->bifs[BIF_map_get_2]) refine(&f,a[3],MAP_SHAPE);
            if(arity==1 && (native==&st->space->engine->bifs[BIF_hd_1] ||
                            native==&st->space->engine->bifs[BIF_tl_1]))
                refine(&f,a[2],LIST_SHAPE);
            if(a[0].type==TAG_f && a[0].val) {
                REQUIRE(label(st,a[0])); alternate=labels[a[0].val];
                REQUIRE(alternate<n && nodes[alternate].function==nodes[index].function);
            } else REQUIRE((a[0].type==TAG_p || a[0].type==TAG_f) && !a[0].val);
            REQUIRE(destination(&f,a[2+arity],0)); break;
        }
        case genop_new_map_3: {
            size_t size,live;
            REQUIRE(a[0].type==TAG_x || a[0].type==TAG_y);
            REQUIRE(a[1].type==TAG_u && (Uint)a[1].val<=MAX_REG);
            REQUIRE(a[2].type==TAG_u && !(a[2].val&1) && (Uint)a[2].val<=2*LB_FLATMAP_MAX_SIZE);
            size=(Uint)a[2].val; live=(Uint)a[1].val;
            REQUIRE(op->arity==3+size && roots(&f,live)); trim_live(&f,live);
            for(r=3;r<op->arity;++r) REQUIRE(source(st,&f,a[r]));
            REQUIRE(destination(&f,a[0],MAP_SHAPE)); f.heap=0; break;
        }
        case genop_put_map_assoc_5: {
            size_t size,live;
            p->error.stage="put_map_assoc failure label";
            REQUIRE((a[0].type==TAG_f && (!a[0].val || label(st,a[0]))) || (a[0].type==TAG_p && !a[0].val));
            p->error.stage="put_map_assoc source"; REQUIRE(source(st,&f,a[1]));
            p->error.stage="put_map_assoc destination and live count";
            REQUIRE((a[2].type==TAG_x || a[2].type==TAG_y) && a[3].type==TAG_u && (Uint)a[3].val<MAX_REG);
            p->error.stage="put_map_assoc update count type"; REQUIRE(a[4].type==TAG_u);
            p->error.stage="put_map_assoc update count range"; REQUIRE(a[4].val>=0 && (Uint)a[4].val<=2*LB_MAP_MAX_FIELDS);
            p->error.stage="put_map_assoc update count parity"; REQUIRE(!(a[4].val&1));
            size=(Uint)a[4].val; live=(Uint)a[3].val;
            p->error.stage="put_map_assoc update roots";
            REQUIRE(op->arity==5+size && roots(&f,live));
            p->error.stage="put_map_assoc update source";
            for(r=5;r<op->arity;++r) REQUIRE(source(st,&f,a[r]));
            trim_live(&f,live); p->error.stage="put_map_assoc result";
            REQUIRE(destination(&f,a[2],MAP_SHAPE)); f.heap=0; break;
        }
        case genop_put_map_exact_5: {
            size_t size,live;
            REQUIRE(((a[0].type==TAG_f && label(st,a[0])) || (a[0].type==TAG_p && !a[0].val)) &&
                    source(st,&f,a[1]));
            REQUIRE((a[2].type==TAG_x || a[2].type==TAG_y) && a[3].type==TAG_u && (Uint)a[3].val<MAX_REG);
            REQUIRE(a[4].type==TAG_u && !(a[4].val&1) && (Uint)a[4].val<=2*LB_FLATMAP_MAX_SIZE);
            size=(Uint)a[4].val; live=(Uint)a[3].val;
            REQUIRE(op->arity==5+size && roots(&f,live));
            for(r=5;r<op->arity;++r) REQUIRE(source(st,&f,a[r]));
            trim_live(&f,live); failed=f; failed.heap=0; failure=&failed;
            if(a[0].type==TAG_f) {
                alternate=labels[a[0].val]; REQUIRE(alternate<n && nodes[alternate].function==nodes[index].function);
            }
            REQUIRE(destination(&f,a[2],MAP_SHAPE)); f.heap=0; break;
        }
        case genop_get_map_elements_3: {
            size_t size;
            REQUIRE(label(st,a[0]) && source(st,&f,a[1]));
            REQUIRE(a[2].type==TAG_u && !(a[2].val&1) && (Uint)a[2].val<=2*LB_FLATMAP_MAX_SIZE);
            size=(Uint)a[2].val; REQUIRE(op->arity==3+size);
            for(r=3;r<op->arity;r+=2) REQUIRE(source(st,&f,a[r]) && destination(&f,a[r+1],0));
            alternate=labels[a[0].val]; REQUIRE(alternate<n && nodes[alternate].function==nodes[index].function);
            break;
        }
        case genop_has_map_fields_3: {
            size_t count;
            REQUIRE(label(st,a[0]) && source(st,&f,a[1]));
            REQUIRE(a[2].type==TAG_u && (Uint)a[2].val<=LB_FLATMAP_MAX_SIZE);
            count=(Uint)a[2].val; REQUIRE(op->arity==3+count);
            for(r=3;r<op->arity;++r) REQUIRE(source(st,&f,a[r]));
            alternate=labels[a[0].val]; REQUIRE(alternate<n && nodes[alternate].function==nodes[index].function);
            refine(&f,a[1],MAP_SHAPE); /* The test proves map shape only on its success edge. */
            if(count) {
                /* Each ignored map value overwrites OTP's reserved scratch X.
                 * Reuse is safe only when its old value is unavailable to guest
                 * code, never when an earlier instruction defined a real X. */
                REQUIRE(!defined(f.x,MAX_REG-1) ||
                        expanded_shape(f.xs[MAX_REG-1])==CONSUMED_VALUE);
                define(f.x,MAX_REG-1); f.xs[MAX_REG-1]=compact_shape(CONSUMED_VALUE);
            }
            break;
        }
        case genop_bif0_2:
            REQUIRE(a[0].type==TAG_u && (Uint)a[0].val<st->beam.imports.count);
            if(st->bif_imports[a[0].val]!=&st->space->engine->bifs[BIF_self_0] &&
               st->bif_imports[a[0].val]!=&st->space->engine->bifs[BIF_node_0]) return LB_CODE_UNSUPPORTED;
            REQUIRE(destination(&f,a[1],0)); break; /* identity storage never moves the term heap */
        case genop_bs_match_string_4:
            REQUIRE(label(st,a[0]) && match_context_source(w,st,&f,a[1],nodes[index].function));
            REQUIRE(a[2].type==TAG_u && a[2].val>=0 && a[3].type==TAG_u && a[3].val>=0);
            alternate=labels[a[0].val]; REQUIRE(alternate<n && nodes[alternate].function==nodes[index].function);
            break; /* String table span is checked when the native code pointer is linked. */
        case genop_bs_get_utf8_5: {
            int has_fail=label(st,a[0]);
            REQUIRE((has_fail || (a[0].type==TAG_p && !a[0].val)) &&
                    match_context_source(w,st,&f,a[1],nodes[index].function));
            REQUIRE(a[2].type==TAG_u && (Uint)a[2].val<=MAX_REG && roots(&f,(Uint)a[2].val));
            REQUIRE(a[3].type==TAG_u && (a[4].type==TAG_x || a[4].type==TAG_y));
            REQUIRE(a[1].type!=TAG_x || ((Uint)a[1].val&REG_MASK)<(Uint)a[2].val);
            if(has_fail) {
                alternate=labels[a[0].val]; REQUIRE(alternate<n && nodes[alternate].function==nodes[index].function);
                failed=f; failure=&failed;
            }
            trim_live(&f,(Uint)a[2].val);
            REQUIRE(destination(&f,a[4],0)); break;
        }
        case genop_bs_skip_utf8_4:case genop_bs_skip_utf32_4: {
            int has_fail=label(st,a[0]);
            REQUIRE((has_fail || (a[0].type==TAG_p && !a[0].val)) &&
                    match_context_source(w,st,&f,a[1],nodes[index].function));
            REQUIRE(a[2].type==TAG_u && (Uint)a[2].val<=MAX_REG && roots(&f,(Uint)a[2].val) &&
                    a[3].type==TAG_u);
            REQUIRE(a[1].type!=TAG_x || ((Uint)a[1].val&REG_MASK)<(Uint)a[2].val);
            if(has_fail) {
                alternate=labels[a[0].val]; REQUIRE(alternate<n && nodes[alternate].function==nodes[index].function);
                failed=f; failure=&failed;
            }
            trim_live(&f,(Uint)a[2].val);
            REQUIRE(!defined(f.x,MAX_REG-1) || expanded_shape(f.xs[MAX_REG-1])==CONSUMED_VALUE);
            REQUIRE(destination(&f,(BeamOpArg){TAG_x,MAX_REG-1},KNOWN_NON_TUPLE));
            define(f.x,MAX_REG-1); f.xs[MAX_REG-1]=compact_shape(CONSUMED_VALUE); break;
        }
        case genop_bs_get_float2_7: {
            REQUIRE(label(st,a[0]) && match_context_source(w,st,&f,a[1],nodes[index].function) &&
                    a[2].type==TAG_u && (Uint)a[2].val<=MAX_REG && roots(&f,(Uint)a[2].val) &&
                    source(st,&f,a[3]) && a[4].type==TAG_u && a[4].val>0 && a[5].type==TAG_u &&
                    (a[6].type==TAG_x || a[6].type==TAG_y));
            if(a[1].type==TAG_x) REQUIRE(((Uint)a[1].val&REG_MASK)<(Uint)a[2].val);
            alternate=labels[a[0].val]; REQUIRE(alternate<n && nodes[alternate].function==nodes[index].function);
            failed=f; failure=&failed; trim_live(&f,(Uint)a[2].val);
            REQUIRE(destination(&f,a[6],KNOWN_NON_TUPLE)); break;
        }
        case genop_bs_get_utf16_5:case genop_bs_skip_utf16_4: {
            int skip=op->op==genop_bs_skip_utf16_4,has_fail=label(st,a[0]);
            REQUIRE((has_fail || (a[0].type==TAG_p && !a[0].val)) &&
                    match_context_source(w,st,&f,a[1],nodes[index].function));
            REQUIRE(a[2].type==TAG_u && a[3].type==TAG_u &&
                    (skip || (a[4].type==TAG_x || a[4].type==TAG_y)));
            if(has_fail) {
                alternate=labels[a[0].val]; REQUIRE(alternate<n && nodes[alternate].function==nodes[index].function);
                failed=f; failure=&failed;
            }
            REQUIRE(destination(&f,skip?(BeamOpArg){TAG_x,MAX_REG-1}:a[4],KNOWN_NON_TUPLE)); break;
        }
        case genop_bs_get_utf32_5: {
            int has_fail=label(st,a[0]);
            REQUIRE((has_fail || (a[0].type==TAG_p && !a[0].val)) &&
                    match_context_source(w,st,&f,a[1],nodes[index].function));
            REQUIRE(a[2].type==TAG_u && (Uint)a[2].val<=MAX_REG && roots(&f,(Uint)a[2].val) &&
                    a[3].type==TAG_u && (a[4].type==TAG_x || a[4].type==TAG_y));
            REQUIRE(a[1].type!=TAG_x || ((Uint)a[1].val&REG_MASK)<(Uint)a[2].val);
            if(has_fail) {
                alternate=labels[a[0].val]; REQUIRE(alternate<n && nodes[alternate].function==nodes[index].function);
                failed=f; failure=&failed;
            }
            trim_live(&f,(Uint)a[2].val);
            REQUIRE(!defined(f.x,MAX_REG-1) || expanded_shape(f.xs[MAX_REG-1])==CONSUMED_VALUE);
            REQUIRE(destination(&f,a[4],KNOWN_NON_TUPLE));
            if(a[4].type!=TAG_x || ((Uint)a[4].val&REG_MASK)!=MAX_REG-1) {
                define(f.x,MAX_REG-1); f.xs[MAX_REG-1]=compact_shape(CONSUMED_VALUE);
            }
            break;
        }
        case genop_bs_skip_bits2_5: {
            REQUIRE(label(st,a[0]) && match_context_source(w,st,&f,a[1],nodes[index].function) &&
                    source(st,&f,a[2]) && a[3].type==TAG_u && a[3].val>0);
            REQUIRE((a[4].type==TAG_n || a[4].type==TAG_q) ||
                    (a[4].type==TAG_u && a[4].val==0));
            alternate=labels[a[0].val];
            REQUIRE(alternate<n && nodes[alternate].function==nodes[index].function);
            failed=f; failure=&failed;
            break;
        }
        case genop_bs_get_integer2_7: {
            int has_fail=label(st,a[0]);
            REQUIRE((has_fail || (a[0].type==TAG_p && !a[0].val)) &&
                    match_context_source(w,st,&f,a[1],nodes[index].function));
            REQUIRE(a[2].type==TAG_u && (Uint)a[2].val<=MAX_REG && roots(&f,(Uint)a[2].val));
            REQUIRE(source(st,&f,a[3]) && a[4].type==TAG_u && a[4].val>0 &&
                    a[5].type==TAG_u && (a[6].type==TAG_x || a[6].type==TAG_y));
            REQUIRE(a[1].type!=TAG_x || ((Uint)a[1].val&REG_MASK)<(Uint)a[2].val);
            if(has_fail) {
                alternate=labels[a[0].val]; REQUIRE(alternate<n && nodes[alternate].function==nodes[index].function);
                failed=f; failure=&failed;
            }
            trim_live(&f,(Uint)a[2].val);
            if(a[3].type==TAG_i)
                REQUIRE(!defined(f.x,MAX_REG-1) || expanded_shape(f.xs[MAX_REG-1])==CONSUMED_VALUE);
            REQUIRE(destination(&f,a[6],0));
            if(a[3].type==TAG_i && !(a[6].type==TAG_x && ((Uint)a[6].val&REG_MASK)==MAX_REG-1)) {
                define(f.x,MAX_REG-1); f.xs[MAX_REG-1]=compact_shape(CONSUMED_VALUE);
            }
            f.heap=0; break;
        }
        case genop_bs_get_binary2_7: {
            REQUIRE(label(st,a[0]) && match_context_source(w,st,&f,a[1],nodes[index].function));
            REQUIRE(a[2].type==TAG_u && (Uint)a[2].val<=MAX_REG && roots(&f,(Uint)a[2].val));
            REQUIRE(source(st,&f,a[3]) && a[4].type==TAG_u && a[4].val>0);
            REQUIRE((a[5].type==TAG_n || a[5].type==TAG_q) ||
                    (a[5].type==TAG_u && a[5].val==0));
            REQUIRE((a[1].type!=TAG_x || ((Uint)a[1].val&REG_MASK)<(Uint)a[2].val) &&
                    (a[6].type==TAG_x || a[6].type==TAG_y));
            alternate=labels[a[0].val];
            REQUIRE(alternate<n && nodes[alternate].function==nodes[index].function);
            failed=f; failure=&failed; trim_live(&f,(Uint)a[2].val);
            REQUIRE(destination(&f,a[6],BITSTRING_SHAPE)); f.heap=0; break;
        }
        case genop_bs_match_3: {
            p->error.stage="bs_match context admission";
            REQUIRE(label(st,a[0]) && match_context_source(w,st,&f,a[1],nodes[index].function));
            p->error.stage="bs_match command-count admission";
            REQUIRE(a[2].type==TAG_u && a[2].val>0 && (Uint)a[2].val==op->arity-3);
            alternate=labels[a[0].val];
            p->error.stage="bs_match failure-target admission";
            REQUIRE(alternate<n && nodes[alternate].function==nodes[index].function);
            failed=f; failure=&failed;
            Uint available=0,packed=0;
            int context_overwritten=0;
            for(unsigned pos=3;pos<op->arity;) {
                p->error.stage="bs_match command admission";
                REQUIRE(a[pos].type==TAG_a);
                Eterm command=(Eterm)a[pos].val;
                if(command==am_ensure_at_least) {
                    p->error.stage="bs_match ensure_at_least admission";
                    REQUIRE(op->arity-pos>=3 && a[pos+1].type==TAG_u && a[pos+2].type==TAG_u);
                    REQUIRE(a[pos+1].val>=0 && a[pos+2].val>0);
                    if(a[pos+1].val>0) {
                        Uint stride=(Uint)a[pos+1].val,unit=(Uint)a[pos+2].val;
                        REQUIRE(stride<=(Uint)-1/unit); available=stride*unit;
                    } /* Zero stride contributes no known bits; the unit is checked by i_bs_test_unit. */
                    pos+=3;
                } else if(command==am_ensure_exactly) {
                    p->error.stage="bs_match ensure_exactly admission";
                    REQUIRE(op->arity-pos>=2 && a[pos+1].type==TAG_u);
                    available=(Uint)a[pos+1].val; pos+=2;
                } else if(command==am_integer) {
                    p->error.stage="bs_match integer shape admission";
                    REQUIRE(op->arity-pos>=6 && a[pos+1].type==TAG_u && roots(&f,(Uint)a[pos+1].val));
                    p->error.stage="bs_match integer flags admission";
                    REQUIRE(bs_match_flags_valid(st,a[pos+2]));
                    p->error.stage="bs_match integer size/unit admission";
                    REQUIRE(a[pos+3].type==TAG_u && a[pos+4].type==TAG_u &&
                            a[pos+3].val>0 && a[pos+4].val>0 &&
                            (Uint)a[pos+3].val<=(Uint)-1/(Uint)a[pos+4].val);
                    Uint bits=(Uint)a[pos+3].val*(Uint)a[pos+4].val;
                    p->error.stage="bs_match integer size admission";
                    /* Immediate extractors have an explicit fail edge, so they
                     * may safely probe beyond a preceding ensure's known minimum. */
                    REQUIRE(bits<=65536 && (bits<=available || bits>=SMALL_BITS ||
                            (bits<SMALL_BITS && bs_match_flags_nonempty(st,a[pos+2]))));
                    if(a[pos+5].type==a[1].type && a[pos+5].val==a[1].val) {
                        p->error.stage="bs_match integer context-alias admission";
                        REQUIRE(pos+6==op->arity);
                    }
                    p->error.stage="bs_match integer destination admission";
                    REQUIRE(destination(&f,a[pos+5],0));
                    context_overwritten|=same_register(a[pos+5],a[1]);
                    available-=bits; pos+=6;
                } else if(command==am_Eq) {
                    p->error.stage="bs_match equality layout admission";
                    REQUIRE(op->arity-pos>=4 && bs_match_empty_flags_valid(st,a[pos+1]) &&
                            a[pos+2].type==TAG_u && a[pos+3].type==TAG_u);
                    Uint bits=(Uint)a[pos+2].val,word_bits=sizeof(Uint)*8;
                    p->error.stage="bs_match equality width admission";
                    REQUIRE(bits>0 && bits<=word_bits && bits<=available && packed<=word_bits-bits &&
                            (bits==word_bits || ((Uint)a[pos+3].val>>bits)==0));
                    available-=bits; packed+=bits; pos+=4;
                } else if(command==am_skip) {
                    p->error.stage="bs_match skip admission";
                    REQUIRE(op->arity-pos>=2 && a[pos+1].type==TAG_u && a[pos+1].val>=0);
                    REQUIRE((Uint)a[pos+1].val<=available); available-=(Uint)a[pos+1].val; pos+=2;
                } else if(command==am_binary) {
                    p->error.stage="bs_match binary admission";
                    REQUIRE(op->arity-pos>=6 && a[pos+1].type==TAG_u && roots(&f,(Uint)a[pos+1].val));
                    REQUIRE(a[pos+2].type==TAG_n && a[pos+3].type==TAG_u && a[pos+3].val>=0 &&
                            a[pos+4].type==TAG_u && a[pos+4].val>0);
                    Uint size=(Uint)a[pos+3].val,unit=(Uint)a[pos+4].val;
                    REQUIRE(size<=(Uint)-1/unit);
                    Uint bits=size*unit;
                    REQUIRE(bits<=65536);
                    if(a[pos+5].type==a[1].type && a[pos+5].val==a[1].val)
                        REQUIRE(pos+6==op->arity);
                    REQUIRE(destination(&f,a[pos+5],BITSTRING_SHAPE));
                    context_overwritten|=same_register(a[pos+5],a[1]);
                    available=bits>available?0:available-bits; pos+=6;
                } else if(command==am_get_tail) {
                    p->error.stage="bs_match get_tail admission";
                    REQUIRE(op->arity-pos>=4 && a[pos+1].type==TAG_u && roots(&f,(Uint)a[pos+1].val));
                    REQUIRE(a[pos+2].type==TAG_u && a[pos+2].val>0);
                    Uint live=(Uint)a[pos+1].val;
                    REQUIRE(a[1].type!=TAG_x || ((Uint)a[1].val&REG_MASK)<live);
                    if(a[pos+3].type==a[1].type && a[pos+3].val==a[1].val) REQUIRE(pos+4==op->arity);
                    trim_live(&f,live);
                    REQUIRE(destination(&f,a[pos+3],BITSTRING_SHAPE));
                    context_overwritten|=same_register(a[pos+3],a[1]);
                    f.heap=0; pos+=4;
                } else return LB_CODE_UNSUPPORTED; /* other complete generator paths have no execution authority */
            }
            if(!context_overwritten) refine(&f,a[1],MATCH_CONTEXT_SHAPE);
            break;
        }
        case genop_bs_start_match4_4:
            if(a[0].type==TAG_a && a[0].val==am_resume) {
                REQUIRE(a[1].type==TAG_u && (Uint)a[1].val<=MAX_REG &&
                        root_source(st,&f,a[2]) &&
                        (a[2].type==TAG_x || a[2].type==TAG_y) &&
                        (a[3].type==TAG_x || a[3].type==TAG_y));
                /* On 64-bit OTP an in-place resume is a no-op. Preserve its
                 * unknown source fact; the later match must prove the cursor.
                 * A separate destination retains the original cursor proof. */
                if(same_register(a[2],a[3])) break;
                uint32_t context=shape(st,&f,a[2]);
                if(context==MATCH_CONTEXT_SHAPE || position_check_after(nodes,n,index,a[3]))
                    context=MATCH_CONTEXT_SHAPE;
                else if(w->defer_tuple_shapes &&
                        (context==0 || context==POSSIBLE_MATCH_CONTEXT) &&
                        (function_parameter(w,nodes[index].function,a[2]) || context==POSSIBLE_MATCH_CONTEXT)) {
                    context=POSSIBLE_MATCH_CONTEXT; ++w->deferred_context_uses;
                } else return LB_CODE_FORMAT;
                REQUIRE(destination(&f,a[3],context));
                break;
            }
            REQUIRE((a[0].type==TAG_a && a[0].val==am_no_fail) ||
                    (a[0].type==TAG_f && label(st,a[0])));
            REQUIRE(root_source(st,&f,a[2]));
            REQUIRE(a[1].type==TAG_u && (Uint)a[1].val<MAX_REG && roots(&f,(Uint)a[1].val));
            if(a[0].type==TAG_f) {
                alternate=labels[a[0].val];
                REQUIRE(alternate<n && nodes[alternate].function==nodes[index].function);
            }
            trim_live(&f,(Uint)a[1].val); f.heap=0;
            REQUIRE(destination(&f,a[3],MATCH_CONTEXT_SHAPE)); break;
        case genop_bs_get_position_3: {
            REQUIRE(match_context_source(w,st,&f,a[0],nodes[index].function));
            int overwrites_context=a[0].type==a[1].type && a[0].val==a[1].val;
            REQUIRE(destination(&f,a[1],0) && a[2].type==TAG_u && roots(&f,(Uint)a[2].val));
            if(!overwrites_context) refine(&f,a[0],MATCH_CONTEXT_SHAPE);
            break;
        }
        case genop_bs_set_position_2:
            REQUIRE(match_context_source(w,st,&f,a[0],nodes[index].function) && source(st,&f,a[1]));
            refine(&f,a[0],MATCH_CONTEXT_SHAPE); break;
        case genop_bs_get_tail_3:
            REQUIRE(match_context_source(w,st,&f,a[0],nodes[index].function));
            REQUIRE(a[2].type==TAG_u && roots(&f,(Uint)a[2].val));
            trim_live(&f,(Uint)a[2].val);
            REQUIRE(destination(&f,a[1],BITSTRING_SHAPE)); f.heap=0;
            if(!(a[0].type==a[1].type && a[0].val==a[1].val) &&
               (a[0].type!=TAG_x || ((Uint)a[0].val&REG_MASK)<(Uint)a[2].val))
                refine(&f,a[0],MATCH_CONTEXT_SHAPE);
            break;
        case genop_bs_start_match3_4: {
            uint32_t source_shape;
            REQUIRE(label(st,a[0]) && root_source(st,&f,a[1]) && a[2].type==TAG_u && (Uint)a[2].val<MAX_REG);
            REQUIRE(roots(&f,(Uint)a[2].val));
            source_shape=shape(st,&f,a[1]);
            alternate=labels[a[0].val];
            REQUIRE(alternate<n && nodes[alternate].function==nodes[index].function);
            /* Failure preserves original operands; success owns a mutable cursor. */
            trim_live(&f,(Uint)a[2].val); f.heap=0;
            REQUIRE(destination(&f,a[3],source_shape==APPEND_SHAPE?
                                APPEND_CONTEXT_SHAPE:MATCH_CONTEXT_SHAPE)); break;
        }
        case genop_bs_init_writable_0:
            REQUIRE(f.frame!=NO_FRAME && arguments(&f,1)); trim_live(&f,1);
            REQUIRE(destination(&f,(BeamOpArg){TAG_x,0},APPEND_SHAPE)); f.heap=0; break;
        case genop_bs_create_bin_6:
            p->error.stage="binary construction flow proof";
            status=binary_flow(st,w,nodes[index].function,&f,&failed,op); if(status!=LB_CODE_OK) return status;
            failure=&failed;
            if(a[0].type==TAG_p) REQUIRE(!a[0].val);
            else {
                REQUIRE(label(st,a[0])); alternate=labels[a[0].val];
                REQUIRE(alternate<n && nodes[alternate].function==nodes[index].function);
            }
            break;
        case genop_send_0:
            REQUIRE(source(st,&f,(BeamOpArg){TAG_x,0}) && source(st,&f,(BeamOpArg){TAG_x,1}));
            REQUIRE(destination(&f,(BeamOpArg){TAG_x,0},expanded_shape(f.xs[1]))); break;
        case genop_return_0:REQUIRE(!f.matching && f.frame==NO_FRAME && source(st,&f,(BeamOpArg){TAG_x,0})); terminal=1;break;
        case genop_call_fun_1:case genop_call_fun2_3:
            r=op->op==genop_call_fun_1?0:1;
            REQUIRE(a[r].type==TAG_u && (Uint)a[r].val<=MAX_ARG && f.frame!=NO_FRAME);
            live=(Uint)a[r].val; REQUIRE(arguments(&f,live));
            REQUIRE(root_source(st,&f,op->op==genop_call_fun_1?(BeamOpArg){TAG_x,(Sint)live}:a[2]));
            forget_append_facts(&f,!append_passed_to_call(&f,live));
            memset(f.x,0,sizeof(f.x)); memset(f.xs,0,sizeof(f.xs)); define(f.x,0); f.heap=0; break;
        case genop_apply_1:case genop_apply_last_2:
            REQUIRE(!f.matching && a[0].type==TAG_u && (Uint)a[0].val<=MAX_ARG);
            live=(Uint)a[0].val;
            for(size_t arg=0;arg<live+2;++arg) REQUIRE(source(st,&f,(BeamOpArg){TAG_x,(Sint)arg}));
            forget_append_facts(&f,0);
            if(op->op==genop_apply_1) {
                REQUIRE(f.frame!=NO_FRAME); memset(f.x,0,sizeof(f.x)); memset(f.xs,0,sizeof(f.xs)); define(f.x,0); f.heap=0;
            } else {
                p->error.stage="tail-call frame admission";
                REQUIRE(a[1].type==TAG_u &&
                        (f.frame==(Uint)a[1].val || f.frame_allocation==(Uint)a[1].val));
                terminal=1;
            }
            break;
        case genop_call_2:case genop_call_only_2:case genop_call_last_3:
        case genop_call_ext_2:case genop_call_ext_only_2:case genop_call_ext_last_3: {
            int local=op->op==genop_call_2 || op->op==genop_call_only_2 || op->op==genop_call_last_3;
            size_t local_target=SIZE_MAX;
            uint32_t returned=0;
            p->error.stage="call operand admission";
            REQUIRE(!f.matching && a[0].type==TAG_u); live=(Uint)a[0].val;
            if(op->op==genop_call_2 || op->op==genop_call_only_2 || op->op==genop_call_last_3) {
                REQUIRE(label(st,a[1])); local_target=labels[a[1].val];
                REQUIRE(local_target>0 && local_target<n && nodes[local_target-1].op->op==genop_int_func_start_5);
                REQUIRE(live==(Uint)nodes[local_target-1].op->a[4].val);
                if(op->op==genop_call_2) {
                    returned=forwarding_return_shape(st,w,local_target);
                    if(!(returned&TUPLE_SHAPE) && nodes[local_target].function<=w->function_count)
                        returned=w->return_shapes[nodes[local_target].function];
                }
            } else {
                p->error.stage="external call import-index admission";
                REQUIRE(a[1].type==TAG_u && (Uint)a[1].val<st->beam.imports.count);
                imp=(Uint)a[1].val;
                p->error.stage="external call arity admission";
                REQUIRE(live==st->beam.imports.entries[imp].arity);
                if(op->op==genop_call_ext_2) {
                    returned=native_return_shape(st,imp);
                    if(st->bif_imports[imp]==&st->space->engine->bifs[BIF_setelement_3])
                        returned=setelement_result_shape(st,&f,nodes,index);
                }
            }
            if(local) REQUIRE(local_arguments(&f,live));
            else {
                p->error.stage="external call live-root admission";
                REQUIRE(roots(&f,live));
                p->error.stage="external call argument-value admission";
                for(size_t arg=0;arg<live && arg<MAX_REG;++arg) {
                    uint32_t fact=expanded_shape(f.xs[arg]);
                    if(fact==CONSUMED_VALUE) {
                        p->error.stage="external call consumed argument"; return LB_CODE_FORMAT;
                    }
                    if(fact&MATCH_CONTEXT_FLAGS) {
                        p->error.stage=arg==0?"external call argument zero is match context":
                                                 "external call argument one is match context";
                        return LB_CODE_UNSUPPORTED;
                    }
                }
            }
            if(local && !w->defer_tuple_shapes) {
                size_t callee=nodes[labels[a[1].val]].function;
                for(size_t arg=0;arg<live;++arg) {
                    size_t slot=w->parameter_offsets[callee]+arg;
                    if(w->parameter_shapes[slot]!=MATCH_CONTEXT_SHAPE) continue;
                    uint32_t actual=expanded_shape(f.xs[arg]);
                    const char *stage=p->error.stage;
                    /* Unknown terms may cross this edge only when the callee
                     * immediately validates them with a no-fail match start;
                     * a failed type check remains a guest exception. */
                    int proven=actual==MATCH_CONTEXT_SHAPE || actual==APPEND_CONTEXT_SHAPE ||
                        (actual==BITSTRING_SHAPE && parameter_restarts_match(w,callee,arg)) ||
                        (actual==0 && parameter_starts_match_checked(w,callee,arg));
                    if(!proven) {
                        static const char *const failures[]={
                            "match-context callsite proof argument 0", "match-context callsite proof argument 1",
                            "match-context callsite proof argument 2", "match-context callsite proof argument 3",
                            "match-context callsite proof argument 4", "match-context callsite proof argument 5",
                            "match-context callsite proof argument 6", "match-context callsite proof argument 7"
                        };
                        p->error.stage=actual==BITSTRING_SHAPE?
                            (arg<sizeof(failures)/sizeof(failures[0])?failures[arg]:
                             "match-context callsite proof argument beyond 7"):
                            (actual==0?"match-context callsite argument shape is unknown":
                             "match-context callsite argument has incompatible shape");
                        return LB_CODE_FORMAT;
                    }
                    p->error.stage=stage;
                }
            }
            if(local && op->op==genop_call_2 &&
               straight_line_local_always_raises(st,w,local_target)) {
                terminal=1; break;
            }
            if(!local && op->op==genop_call_ext_2 && native_call_always_raises(st->bif_imports[imp],&f)) {
                terminal=1; break;
            }
            forget_append_facts(&f,!append_passed_to_call(&f,live));
            if(op->op==genop_call_2 || op->op==genop_call_ext_2) {
                if(f.frame==NO_FRAME) { p->error.stage="call result frame absent"; return LB_CODE_FORMAT; }
                memset(f.x,0,sizeof(f.x)); memset(f.xs,0,sizeof(f.xs)); define(f.x,0); f.heap=0;
                f.xs[0]=compact_shape(returned);
                if(op->op==genop_call_ext_2) {
                    Export *entry=st->module_code->imports[imp];
                    Eterm monitored;
                    if(entry->owner && entry->info.mfa.module==am_erlang && live==1 &&
                       lb_atoms_find(st->space->atoms,"spawn_monitor",13,&monitored)==LB_ATOM_OK && entry->info.mfa.function==monitored)
                        f.xs[0]=compact_shape(TUPLE_SHAPE|2); /* protected complete pinned wrapper, not a BIF alias */
                }
            } else {
                if(op->op==genop_call_only_2 || op->op==genop_call_ext_only_2) REQUIRE(f.frame==NO_FRAME && f.frame_allocation==NO_FRAME);
                else {
                    p->error.stage="tail-call frame admission";
                    REQUIRE(a[2].type==TAG_u &&
                            (f.frame==(Uint)a[2].val || f.frame_allocation==(Uint)a[2].val));
                }
                terminal=1;
            }
            break;
        }
        case genop_select_val_3: {
            p->error.stage="select_val source admission";
            REQUIRE(source(st,&f,a[0]) && !(shape(st,&f,a[0])&MATCH_CONTEXT_SHAPE) && label(st,a[1]));
            p->error.stage="select_val table admission";
            /* The pinned Elixir Unicode-security module has 6,561-entry
             * select_val tables; bound encoded table words without rejecting
             * normal generated Unicode dispatch tables. */
            REQUIRE(a[2].type==TAG_u && a[2].val>=2 && !(a[2].val&1) &&
                    (Uint)a[2].val==op->arity-3 && (Uint)a[2].val<=65536);
            for(size_t pair=3;pair<op->arity;pair+=2) {
                p->error.stage="select_val key admission";
                BeamOpArg key=a[pair];
                if(key.type==TAG_a) REQUIRE(is_atom((Eterm)key.val));
                else if(key.type==TAG_i) REQUIRE(IS_SSMALL(key.val));
                else if(key.type==TAG_n) { /* nil is an exact selection key */ }
                else if(key.type==TAG_q) {
                    Eterm literal; REQUIRE(lb_beam_program_literal(st->program,st->space->atoms,key.val,&literal)==LB_BEAM_OK);
                } else return LB_CODE_FORMAT;
                p->error.stage="select_val label admission";
                REQUIRE(label(st,a[pair+1]));
                p->error.stage="select_val edge admission";
                status=propagate(w,labels[a[pair+1].val],nodes[index].function,&f);
                if(status!=LB_CODE_OK) return status;
            }
            status=propagate(w,labels[a[1].val],nodes[index].function,&f);
            if(status!=LB_CODE_OK) return status;
            terminal=1; break;
        }
        case genop_select_tuple_arity_3:
            status=select_arities(st,w,index,&f,op); if(status!=LB_CODE_OK) return status; terminal=1; break;
        case genop_is_lt_3:case genop_is_ge_3:case genop_is_eq_3:case genop_is_ne_3:case genop_is_eq_exact_3:case genop_is_ne_exact_3:case genop_is_nil_2:case genop_is_pid_2:case genop_is_reference_2:case genop_is_atom_2:
        case genop_is_binary_2:case genop_is_list_2:case genop_is_integer_2:case genop_is_map_2:case genop_is_function_2:case genop_is_function2_3:
        case genop_is_float_2:case genop_is_number_2:case genop_is_port_2:case genop_is_boolean_2:case genop_is_bitstr_2:
        case genop_is_tuple_2:case genop_is_any_native_record_2:case genop_test_arity_3:case genop_is_tagged_tuple_4:case genop_is_nonempty_list_2:
            if(op->op==genop_is_eq_exact_3) {
                p->error.stage="is_eq_exact failure target"; REQUIRE(label(st,a[0]));
                p->error.stage="is_eq_exact first operand"; REQUIRE(source(st,&f,a[1]));
            } else REQUIRE(label(st,a[0]) && source(st,&f,a[1]));
            alternate=labels[a[0].val];
            REQUIRE(alternate<n && nodes[alternate].function==nodes[index].function);
            if((op->op==genop_is_integer_2 || op->op==genop_is_list_2 || op->op==genop_is_binary_2 ||
                op->op==genop_is_float_2 || op->op==genop_is_number_2 || op->op==genop_is_bitstr_2) && a[1].type==TAG_q) {
                /* Native literal lowering writes scratch x1023 before either
                 * branch. Old shape facts cannot survive that overwrite. */
                define(f.x,MAX_REG-1); f.xs[MAX_REG-1]=compact_shape(shape(st,&f,a[1]));
                failed=f; failure=&failed;
            } else if(op->op==genop_is_function2_3) {
                REQUIRE(source(st,&f,a[2]));
                if(!((a[1].type==TAG_x || a[1].type==TAG_y) && a[2].type==TAG_i)) {
                    /* Native cold lowering uses x1022/x1023 in that order.
                     * Its arity source must not alias the first overwritten
                     * temporary. Both branches observe the new scratch slots. */
                    REQUIRE(a[2].type!=TAG_x || (((Uint)a[2].val&REG_MASK)!=MAX_REG-2));
                    define(f.x,MAX_REG-2); define(f.x,MAX_REG-1);
                    f.xs[MAX_REG-2]=f.xs[MAX_REG-1]=0; failed=f; failure=&failed;
                }
            }
            else if(op->op==genop_is_eq_exact_3) {
                p->error.stage="is_eq_exact second operand"; REQUIRE(source(st,&f,a[2]));
            } else if(op->op==genop_is_eq_3 || op->op==genop_is_ne_3 ||
                    op->op==genop_is_ne_exact_3 || op->op==genop_is_lt_3 ||
                    op->op==genop_is_ge_3) REQUIRE(source(st,&f,a[2]));
            /* is_list also accepts NIL: only the nonempty test grants cons access. */
            else if(op->op==genop_is_nonempty_list_2) {
                if(shape(st,&f,a[1])==LIST_OR_NIL_SHAPE) {
                    failed=f; refine(&failed,a[1],NIL_SHAPE); failure=&failed;
                }
                refine(&f,a[1],LIST_SHAPE);
            }
            else if(op->op==genop_is_map_2) refine(&f,a[1],MAP_SHAPE);
            else if(op->op==genop_is_tuple_2) {
                s=shape(st,&f,a[1]); failed=f;
                refine_tuple_copies(&failed,nodes,index,a[1],KNOWN_NON_TUPLE); failure=&failed;
                refine_tuple_copies(&f,nodes,index,a[1],(s&TUPLE_SHAPE)?s&~POSSIBLE_TUPLE_ARITY:TUPLE_SHAPE);
            } else if(op->op==genop_is_binary_2 || op->op==genop_is_bitstr_2)
                refine(&f,a[1],BITSTRING_SHAPE);
            else if(op->op==genop_is_atom_2) refine(&f,a[1],ATOM_SHAPE);
            else if(op->op==genop_is_integer_2 ||
                      op->op==genop_is_float_2 || op->op==genop_is_number_2 ||
                      op->op==genop_is_pid_2 ||
                      op->op==genop_is_reference_2 || op->op==genop_is_port_2 ||
                      op->op==genop_is_boolean_2 || op->op==genop_is_function_2) refine(&f,a[1],KNOWN_NON_TUPLE);
            else if(op->op==genop_is_nil_2) {
                if(shape(st,&f,a[1])==LIST_OR_NIL_SHAPE) {
                    failed=f; refine(&failed,a[1],LIST_SHAPE); failure=&failed;
                }
                refine(&f,a[1],NIL_SHAPE);
            } else if(op->op==genop_is_list_2) {
                uint32_t list=shape(st,&f,a[1]);
                if(list!=LIST_SHAPE && list!=NIL_SHAPE)
                    refine(&f,a[1],LIST_OR_NIL_SHAPE); /* is_list also accepts NIL. */
            }
            else if(op->op==genop_test_arity_3 || op->op==genop_is_tagged_tuple_4) {
                REQUIRE(a[2].type==TAG_u && (Uint)a[2].val<=MAX_ARITYVAL);
                if(op->op==genop_is_tagged_tuple_4)
                    REQUIRE(a[2].val>0 && a[3].type==TAG_a && is_atom((Eterm)a[3].val));
                refine_tuple_copies(&f,nodes,index,a[1],TUPLE_SHAPE|(uint32_t)a[2].val);
            }
            break;
        case genop_wait_1:case genop_wait_timeout_2: {
            size_t wait_live=0;
            if(op->op==genop_wait_timeout_2) {
                p->error.stage="wait timeout value admission";
                if(a[1].type==TAG_x || a[1].type==TAG_y) {
                    REQUIRE(source(st,&f,a[1]));
                    if(a[1].type==TAG_x) wait_live=((Uint)a[1].val&REG_MASK)+1;
                } else REQUIRE(a[1].type==TAG_i || a[1].type==TAG_a);
            }
            p->error.stage="wait suspension admission";
            REQUIRE(!f.matching && roots(&f,wait_live) && label(st,a[0]));
            trim_live(&f,wait_live); f.heap=0;
            if(op->op==genop_wait_1) next=labels[a[0].val];
            else alternate=labels[a[0].val];
            break;
        }
        case genop_recv_marker_reserve_1:
            REQUIRE(!f.matching && destination(&f,a[0],0)); break;
        case genop_recv_marker_bind_2:
            REQUIRE(!f.matching && source(st,&f,a[0]) && source(st,&f,a[1])); break;
        case genop_recv_marker_clear_1:
            REQUIRE(source(st,&f,a[0])); break;
        case genop_recv_marker_use_1:
            REQUIRE(!f.matching && source(st,&f,a[0])); break;
        case genop_timeout_0:REQUIRE(!f.matching); break;
        case genop_remove_message_0:
            REQUIRE(f.matching); f.matching=0; break;
        case genop_loop_rec_2:
            REQUIRE(!f.matching && roots(&f,0) && label(st,a[0]) && a[1].type==TAG_x && a[1].val==0);
            trim_live(&f,0); f.heap=0; failed=f; failure=&failed;
            alternate=labels[a[0].val]; f.matching=1; REQUIRE(destination(&f,a[1],0)); break;
        case genop_loop_rec_end_1:
            REQUIRE(f.matching && label(st,a[0])); f.matching=0; trim_live(&f,0); f.heap=0;
            next=labels[a[0].val]; break;
        case genop_jump_1:REQUIRE(label(st,a[0])); next=labels[a[0].val]; break;
        case genop_raw_raise_0:
            for(r=0;r<3;++r) REQUIRE(source(st,&f,(BeamOpArg){TAG_x,(Sint)r}));
            if((shape(st,&f,(BeamOpArg){TAG_x,0})==EXCEPTION_CLASS_SHAPE ||
                shape(st,&f,(BeamOpArg){TAG_x,0})==ATOM_SHAPE || raw_raise_has_literal_class(nodes,index)) &&
               shape(st,&f,(BeamOpArg){TAG_x,2})==EXCEPTION_TRACE_SHAPE) terminal=1;
            else refine(&f,(BeamOpArg){TAG_x,0},KNOWN_NON_TUPLE);
            break;
        case genop_raise_2:
            REQUIRE(source(st,&f,a[0]) && source(st,&f,a[1])); terminal=1; break;
        case genop_catch_2:case genop_try_2: {
            REQUIRE(a[0].type==TAG_y && f.frame!=NO_FRAME && a[0].val>=0 && (Uint)a[0].val<f.frame && label(st,a[1]));
            REQUIRE(destination(&f,a[0],0));
            Flow caught=f; memset(caught.x,0,sizeof(caught.x)); memset(caught.xs,0,sizeof(caught.xs));
            for(size_t r=0;r<4;++r) define(caught.x,r);
            caught.heap=0; caught.matching=0;
            caught.exception_handler=op->op==genop_try_2;
            alternate=labels[a[1].val]; failed=caught; failure=&failed; break;
        }
        case genop_try_end_1:case genop_catch_end_1:
            REQUIRE(a[0].type==TAG_y && f.frame!=NO_FRAME && a[0].val>=0 && (Uint)a[0].val<f.frame);
            REQUIRE(defined(f.y,(Uint)a[0].val));
            if(op->op==genop_catch_end_1) REQUIRE(defined(f.x,0));
            f.ys[(Uint)a[0].val]=0; break;
        case genop_try_case_1:
            p->error.stage="try_case exception-path admission";
            REQUIRE(f.exception_handler); f.exception_handler=0;
            p->error.stage="try_case frame admission";
            REQUIRE(a[0].type==TAG_y && f.frame!=NO_FRAME && a[0].val>=0 && (Uint)a[0].val<f.frame);
            p->error.stage="try_case saved-state admission";
            REQUIRE(defined(f.y,(Uint)a[0].val));
            define(f.x,0); f.xs[0]=compact_shape(EXCEPTION_CLASS_SHAPE);
            f.xs[2]=compact_shape(EXCEPTION_TRACE_SHAPE);
            f.ys[(Uint)a[0].val]=0; break;
        case genop_try_case_end_1:
            REQUIRE(source(st,&f,a[0])); terminal=1; break;
        case genop_build_stacktrace_0:
            define(f.x,0); f.xs[0]=0; break;
        case genop_badmatch_1:case genop_case_end_1:case genop_badrecord_1:
            REQUIRE(source(st,&f,a[0])); terminal=1;break;
        case genop_if_end_0:terminal=1;break;
        default:return LB_CODE_FORMAT;
        }
        if(terminal) continue;
        p->error.stage="control-flow successor proof";
        status=propagate(w,next,nodes[index].function,&f); if(status!=LB_CODE_OK) return status;
        if(alternate!=SIZE_MAX) {
            /* Failed tests carry their incoming facts, never success refinements. */
            p->error.stage="control-flow failure-edge proof";
            status=propagate(w,alternate,nodes[index].function,failure);
            if(status!=LB_CODE_OK) return status;
        }
    }
    return LB_CODE_OK;
}
static int parameter_restarts_match_depth(const Analysis *,size_t,size_t,unsigned);
static int parameter_restarts_match(const Analysis *w,size_t function,size_t parameter)
{ return parameter_restarts_match_depth(w,function,parameter,0); }
/* Verification sees upstream bs_start_match4 before its transform. Accept the
 * unknown-input case only if that checked operation is the first body opcode. */
static int parameter_starts_match_checked(const Analysis *w,size_t function,size_t parameter)
{
    size_t start=w->function_starts[function];
    for(size_t i=start+1;i<w->count && w->nodes[i].function==function;++i) {
        const BeamOp *op=w->nodes[i].op;
        if(op->op==genop_label_1 || op->op==genop_line_1) continue;
        if(op->op==genop_bs_start_match3_4)
            return op->a[1].type==TAG_x && (Uint)op->a[1].val==parameter;
        return op->op==genop_bs_start_match4_4 && op->a[0].type==TAG_a &&
            op->a[0].val==am_no_fail && op->a[1].type==TAG_u &&
            op->a[2].type==TAG_x && (Uint)op->a[2].val==parameter;
    }
    return 0;
}
static int parameter_restarts_match_depth(const Analysis *w,size_t function,size_t parameter,unsigned depth)
{
    size_t start=w->function_starts[function]; int restarted=0,unsafe_use=0;
    int x_origin[MAX_REG],y_origin[MAX_REG];
    if(depth>64 || parameter>=MAX_REG) return 0;
    for(size_t reg=0;reg<MAX_REG;++reg) { x_origin[reg]=-1; y_origin[reg]=-1; }
    x_origin[parameter]=(int)parameter;
    for(size_t i=start+1;i<w->count && w->nodes[i].function==function;++i) {
        const BeamOp *op=w->nodes[i].op; BeamOpArg ctx;
        if(op->op==genop_label_1 || op->op==genop_line_1) continue;
        if(op->op==genop_move_2) {
            BeamOpArg src=op->a[0],dst=op->a[1]; int origin=-1;
            if(src.type==TAG_x && (Uint)src.val<MAX_REG) origin=x_origin[(Uint)src.val&REG_MASK];
            if(src.type==TAG_y && (Uint)src.val<MAX_REG) origin=y_origin[(Uint)src.val&REG_MASK];
            if(dst.type==TAG_x && (Uint)dst.val<MAX_REG) x_origin[(Uint)dst.val&REG_MASK]=origin;
            if(dst.type==TAG_y && (Uint)dst.val<MAX_REG) y_origin[(Uint)dst.val&REG_MASK]=origin;
            continue;
        }
        if(op->op==genop_bs_start_match3_4 || op->op==genop_bs_start_match4_4) {
            unsigned source_index=op->op==genop_bs_start_match3_4?1:2;
            BeamOpArg src=op->a[source_index],dst=op->a[3]; int origin=-1;
            if(src.type==TAG_x && (Uint)src.val<MAX_REG) origin=x_origin[(Uint)src.val&REG_MASK];
            if(src.type==TAG_y && (Uint)src.val<MAX_REG) origin=y_origin[(Uint)src.val&REG_MASK];
            if(origin==(int)parameter) restarted=1;
            if(dst.type==TAG_x && (Uint)dst.val<MAX_REG) x_origin[(Uint)dst.val&REG_MASK]=origin;
            if(dst.type==TAG_y && (Uint)dst.val<MAX_REG) y_origin[(Uint)dst.val&REG_MASK]=origin;
            continue;
        }
        ctx=(BeamOpArg){TAG_x,-1};
        if(op->op==genop_bs_match_3) ctx=op->a[1];
        else if(op->op==genop_bs_get_position_3 || op->op==genop_bs_set_position_2 ||
                op->op==genop_bs_get_tail_3) ctx=op->a[0];
        if(ctx.type==TAG_x && (Uint)ctx.val<MAX_REG &&
           x_origin[(Uint)ctx.val&REG_MASK]==(int)parameter && !restarted) unsafe_use=1;
        if(ctx.type==TAG_y && (Uint)ctx.val<MAX_REG &&
           y_origin[(Uint)ctx.val&REG_MASK]==(int)parameter && !restarted) unsafe_use=1;
    }
    if(restarted) return !unsafe_use;
    if(unsafe_use) return 0;

    /* A small prologue may forward the binary through moves before the first
     * operation that can branch or mutate registers. Follow only those calls. */
    {
        int x_origin[MAX_REG],y_origin[MAX_REG];
        for(size_t reg=0;reg<MAX_REG;++reg) { x_origin[reg]=-1; y_origin[reg]=-1; }
        if(parameter>=MAX_REG) return 0;
        x_origin[parameter]=(int)parameter;
        for(size_t i=start+1;i<w->count && w->nodes[i].function==function;++i) {
            const BeamOp *op=w->nodes[i].op;
            if(op->op==genop_label_1 || op->op==genop_line_1 || op->op==genop_init_yregs_1 ||
               op->op==genop_allocate_2 || op->op==genop_allocate_heap_3) continue;
            if(op->op==genop_move_2) {
                BeamOpArg src=op->a[0],dst=op->a[1]; int origin=-1;
                if(src.type==TAG_x && (Uint)src.val<MAX_REG) origin=x_origin[(Uint)src.val&REG_MASK];
                if(src.type==TAG_y && (Uint)src.val<MAX_REG) origin=y_origin[(Uint)src.val&REG_MASK];
                if(dst.type==TAG_x && (Uint)dst.val<MAX_REG) x_origin[(Uint)dst.val&REG_MASK]=origin;
                if(dst.type==TAG_y && (Uint)dst.val<MAX_REG) y_origin[(Uint)dst.val&REG_MASK]=origin;
                continue;
            }
            if(op->op==genop_call_2 || op->op==genop_call_only_2 || op->op==genop_call_last_3) {
                size_t target,callee,arity=(Uint)op->a[0].val;
                if(op->a[1].type!=TAG_f || op->a[1].val<=0 || (Uint)op->a[1].val>=w->count) return 0;
                target=w->labels[op->a[1].val]; if(target>=w->count) return 0;
                callee=w->nodes[target].function;
                if(!callee || arity!=w->parameter_arities[callee]) return 0;
                for(size_t arg=0;arg<arity && arg<MAX_REG;++arg) {
                    size_t slot=w->parameter_offsets[callee]+arg;
                    if(x_origin[arg]==(int)parameter && w->parameter_shapes[slot]==MATCH_CONTEXT_SHAPE &&
                       parameter_restarts_match_depth(w,callee,arg,depth+1)) return 1;
                }
                return 0;
            }
            break;
        }
    }
    return 0;
}
static int function_params_inferable(const LoaderState *st,const BeamOp *start)
{
    Eterm name; Uint arity; size_t i;
    if(start->a[3].type!=TAG_a || start->a[4].type!=TAG_u || start->a[4].val<0) return 0;
    name=(Eterm)start->a[3].val; arity=(Uint)start->a[4].val;
    if(st->module_code) for(i=0;i<st->module_code->export_count;++i)
        if(st->module_code->exports[i].info.mfa.function==name && st->module_code->exports[i].info.mfa.arity==arity) return 0;
    for(i=0;i<st->program->lambda_count;++i)
        if(st->program->lambdas[i].label==(Uint)start->a[0].val) return 0;
    return 1;
}
static int update_parameter_shapes(const LoaderState *st,Analysis *w)
{
    int changed=0;
    memset(w->parameter_candidates,0,w->parameter_count*sizeof(*w->parameter_candidates));
    memset(w->parameter_states,0,w->parameter_count*sizeof(*w->parameter_states));
    for(size_t i=0;i<w->count;++i) {
        Node *node=&w->nodes[i]; BeamOp *op=node->op; size_t target,function,arity;
        if(!node->reached ||
           (op->op!=genop_call_2 && op->op!=genop_call_only_2 && op->op!=genop_call_last_3) ||
           op->a[1].type!=TAG_f || op->a[1].val<=0 || (Uint)op->a[1].val>=st->label_count) continue;
        target=w->labels[op->a[1].val]; if(target>=w->count) continue;
        function=w->nodes[target].function;
        if(!function || function==node->function || !w->function_inferable[function]) continue;
        arity=w->parameter_arities[function];
        if((Uint)op->a[0].val!=arity) continue;
        for(size_t arg=0;arg<arity;++arg) {
            size_t slot=w->parameter_offsets[function]+arg; uint32_t fact=expanded_shape(node->in.xs[arg]);
            /* A provisional match-context use is not an anchor. A definite
             * context from another caller can seed an SCC, but every pending
             * edge must be rechecked at the final strict callsite replay. */
            if(fact==APPEND_CONTEXT_SHAPE) fact=MATCH_CONTEXT_SHAPE;
            if(!fact) { w->parameter_states[slot]|=4; continue; }
            if(fact==POSSIBLE_MATCH_CONTEXT) {
                w->parameter_states[slot]|=2; continue;
            }
            if(!(w->parameter_states[slot]&1)) {
                w->parameter_candidates[slot]=fact; w->parameter_states[slot]|=1;
            } else {
                w->parameter_candidates[slot]=common_shape(w->parameter_candidates[slot],fact);
                if(!w->parameter_candidates[slot]) w->parameter_states[slot]|=2;
            }
        }
    }
    for(size_t function=1;function<=w->function_count;++function) {
        size_t offset=w->parameter_offsets[function],arity=w->parameter_arities[function];
        for(size_t arg=0;arg<arity;++arg) {
            size_t slot=offset+arg; uint32_t shape=0;
            if(w->function_inferable[function] && (w->parameter_states[slot]&1) &&
               (!(w->parameter_states[slot]&6) ||
                (w->parameter_candidates[slot]==MATCH_CONTEXT_SHAPE &&
                 !(w->parameter_states[slot]&4))))
                shape=w->parameter_candidates[slot];
            if(w->parameter_shapes[slot]!=shape) { w->parameter_shapes[slot]=shape; changed=1; }
        }
    }
    return changed;
}
static int update_return_shapes(Analysis *w)
{
    int changed=0;
    for(size_t function=1;function<=w->function_count;++function) {
        size_t target=w->function_starts[function];
        uint32_t shape=target==SIZE_MAX?0:local_return_tuple_shape(w,target);
        if(w->return_shapes[function]!=shape) { w->return_shapes[function]=shape; changed=1; }
    }
    return changed;
}
LbCodeStatus lb_verify_program(LoaderState *st)
{
    Analysis w={0}; BeamOp *op; size_t node_bytes,label_bytes,queue_bytes,functions,offsets_bytes,arities_bytes,starts_bytes;
    size_t returns_bytes,param_capacity,param_bytes,candidate_bytes,state_bytes,inferable_bytes,total=0; void *storage;
    LbCodeStatus status;
    for(op=st->program->ops;op;op=op->next) {
        ++w.count;
        if(op->op==genop_int_func_start_5) {
            size_t arity;
            ++w.function_count;
            if(op->a[4].type!=TAG_u || op->a[4].val<0 || (Uint)op->a[4].val>MAX_REG) return LB_CODE_FORMAT;
            arity=(Uint)op->a[4].val;
            if(!lb_size_add(w.parameter_count,arity,&w.parameter_count)) return LB_CODE_LIMIT;
        }
    }
    REQUIRE(w.count && w.function_count);
    if(!lb_size_add(w.function_count,1,&functions)) return LB_CODE_LIMIT;
    param_capacity=w.parameter_count?w.parameter_count:1;
    if(!lb_size_mul(w.count,sizeof(Node),&node_bytes) || !lb_size_mul(st->label_count,sizeof(size_t),&label_bytes) ||
       !lb_size_mul(w.count,sizeof(size_t),&queue_bytes) || !lb_size_mul(functions,sizeof(size_t),&offsets_bytes) ||
       !lb_size_mul(functions,sizeof(size_t),&arities_bytes) || !lb_size_mul(functions,sizeof(size_t),&starts_bytes) ||
       !lb_size_mul(functions,sizeof(uint32_t),&returns_bytes) || !lb_size_mul(param_capacity,sizeof(uint32_t),&param_bytes) ||
       !lb_size_mul(param_capacity,sizeof(uint32_t),&candidate_bytes) ||
       !lb_size_mul(param_capacity,sizeof(uint8_t),&state_bytes) || !lb_size_mul(functions,sizeof(uint8_t),&inferable_bytes) ||
       !lb_size_add(total,node_bytes,&total) || !lb_size_add(total,label_bytes,&total) ||
       !lb_size_add(total,queue_bytes,&total) || !lb_size_add(total,offsets_bytes,&total) ||
       !lb_size_add(total,arities_bytes,&total) || !lb_size_add(total,starts_bytes,&total) ||
       !lb_size_add(total,returns_bytes,&total) || !lb_size_add(total,param_bytes,&total) ||
       !lb_size_add(total,candidate_bytes,&total) || !lb_size_add(total,state_bytes,&total) ||
       !lb_size_add(total,inferable_bytes,&total)) return LB_CODE_LIMIT;
    if(lb_alloc_domain_allocate(st->space->domain,total,&storage)!=LB_ALLOC_OK) return LB_CODE_NO_MEMORY;
    memset(storage,0,total); w.nodes=storage;
    w.labels=(size_t *)((unsigned char *)storage+node_bytes); w.queue=w.labels+st->label_count;
    w.parameter_offsets=(size_t *)((unsigned char *)w.queue+queue_bytes);
    w.parameter_arities=w.parameter_offsets+functions;
    w.function_starts=w.parameter_arities+functions;
    w.return_shapes=(uint32_t *)((unsigned char *)w.function_starts+starts_bytes);
    w.parameter_shapes=w.return_shapes+functions;
    w.parameter_candidates=w.parameter_shapes+param_capacity;
    w.parameter_states=(uint8_t *)((unsigned char *)w.parameter_candidates+candidate_bytes);
    w.function_inferable=w.parameter_states+state_bytes;
    w.defer_tuple_shapes=w.defer_append_shapes=1;
    size_t function=0,parameter_offset=0;
    for(op=st->program->ops;op;op=op->next) if(op->op==genop_int_func_start_5) {
        ++function; w.parameter_offsets[function]=parameter_offset;
        w.parameter_arities[function]=(Uint)op->a[4].val;
        parameter_offset+=(Uint)op->a[4].val;
    }
    status=verify_flow(st,&w);
    if(status!=LB_CODE_OK && w.deferred_append_uses) {
        /* A provisional, non-writable result can fail an unrelated proof.
         * Recheck without deferral so invalid BEAMs retain the actual strict
         * refusal; neither result is allowed to admit code. */
        LbCodeStatus provisional=status;
        w.defer_append_shapes=0;
        LbCodeStatus strict=verify_flow(st,&w);
        status=strict==LB_CODE_OK?provisional:strict;
    }
    if(status==LB_CODE_OK) {
        int had_deferred=w.deferred_tuple_uses!=0 || w.deferred_context_uses!=0 || w.deferred_append_uses!=0;
        int changed=update_return_shapes(&w)|update_parameter_shapes(st,&w);
        for(size_t pass=0;status==LB_CODE_OK && had_deferred && changed && pass<64;++pass) {
            status=verify_flow(st,&w);
            if(status==LB_CODE_OK) {
                had_deferred|=w.deferred_tuple_uses!=0 || w.deferred_context_uses!=0 || w.deferred_append_uses!=0;
                changed=update_return_shapes(&w)|update_parameter_shapes(st,&w);
            }
        }
        if(status==LB_CODE_OK && had_deferred && changed) status=LB_CODE_UNSUPPORTED;
        if(status==LB_CODE_OK && had_deferred) {
            w.defer_tuple_shapes=w.defer_append_shapes=0;
            status=verify_flow(st,&w);
        }
    }
    if(lb_alloc_domain_release(st->space->domain,storage)!=LB_ALLOC_OK) abort();
    if(status==LB_CODE_OK) {
        /* Hints are not proofs. Native adjacency predicates assume canonical
         * register numbers: x1023 followed by hinted x0 must not become a
         * contiguous two-register write. Preserve hints in the image/program
         * preparation API, but discard them for executable transformations. */
        for(op=st->program->ops;op;op=op->next) {
            size_t arg;
            for(arg=0;arg<op->arity;++arg)
                if(op->a[arg].type==TAG_x || op->a[arg].type==TAG_y) op->a[arg].val&=REG_MASK;
        }
    }
    return status;
}
