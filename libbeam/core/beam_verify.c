/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Must-definition, shape and heap-credit dataflow over the admitted profile.
 * Native extraction instructions may omit checks only after this proof. Compiler
 * type hints are not evidence: facts come from real tests and constructors.
 */
#include "code_internal.h"
#include "fun.h"
#include "flatmap.h"
#define BITS (MAX_REG/64)
#define NO_FRAME SIZE_MAX
#define TUPLE_SHAPE UINT32_C(0x1000000)
#define LIST_SHAPE UINT32_C(0x2000000)
#define APPEND_SHAPE UINT32_C(0x4000000) /* reviewed append result; not uniqueness */
#define MATCH_CONTEXT_SHAPE UINT32_C(0x10000000)
#define POSSIBLE_MATCH_CONTEXT UINT32_C(0x20000000)
#define MAP_SHAPE UINT32_C(0x40000000)
#define MATCH_CONTEXT_FLAGS (MATCH_CONTEXT_SHAPE|POSSIBLE_MATCH_CONTEXT)
#define CONSUMED_VALUE UINT32_C(0x8000000) /* initialized GC root, unavailable logical value */
#define ARITY_MASK UINT32_C(0xffffff)
#define ANALYSIS_LIMIT (64u*1024u*1024u)
#define REQUIRE(test) do { if(!(test)) return LB_CODE_FORMAT; } while(0)
_Static_assert(MAX_ARITYVAL==ARITY_MASK,"Shape facts cover native tuple arities");
typedef struct {
    uint64_t x[BITS],y[BITS];
    uint32_t xs[MAX_REG],ys[MAX_REG]; /* representation fact, consumed value, or unknown */
    size_t frame,heap;
    int matching;
} Flow;
typedef struct { BeamOp *op; Flow in; size_t function; int reached,queued; } Node;
typedef struct {
    Node *nodes; size_t *labels,*queue,count,head,tail,pending;
} Analysis;
static int defined(const uint64_t *set,size_t index) { return (int)((set[index/64]>>(index%64))&1); }
static void define(uint64_t *set,size_t index) { set[index/64]|=UINT64_C(1)<<(index%64); }
static int root_source(LoaderState *st,const Flow *f,BeamOpArg arg)
{
    size_t reg=(Uint)arg.val&REG_MASK;
    switch(arg.type) {
    case TAG_x:return defined(f->x,reg) && f->xs[reg]!=CONSUMED_VALUE;
    case TAG_y:return f->frame!=NO_FRAME && reg<f->frame && defined(f->y,reg) && f->ys[reg]!=CONSUMED_VALUE;
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
    if(arg.type==TAG_x) return f->xs[reg];
    if(arg.type==TAG_y) return f->ys[reg];
    if(arg.type==TAG_q && lb_beam_program_literal(st->program,st->space->atoms,arg.val,&term)==LB_BEAM_OK) {
        if(is_tuple(term)) return TUPLE_SHAPE | (uint32_t)arityval(*tuple_val(term));
        if(is_list(term)) return LIST_SHAPE;
        if(lb_is_flatmap(term)) return MAP_SHAPE;
    }
    return 0;
}
static int source(LoaderState *st,const Flow *f,BeamOpArg arg)
{ return root_source(st,f,arg) && !(shape(st,f,arg)&MATCH_CONTEXT_FLAGS); }
static int destination(Flow *f,BeamOpArg arg,uint32_t fact)
{
    size_t reg=(Uint)arg.val&REG_MASK;
    if(arg.type==TAG_x) { define(f->x,reg); f->xs[reg]=fact; return 1; }
    if(arg.type==TAG_y && f->frame!=NO_FRAME && reg<f->frame) { define(f->y,reg); f->ys[reg]=fact; return 1; }
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
    for(size_t i=0;i<arity;++i) if(f->xs[i]==CONSUMED_VALUE || (f->xs[i]&MATCH_CONTEXT_FLAGS)) return 0;
    return 1;
}
static void trim_live(Flow *f,size_t live)
{
    size_t i;
    for(i=live;i<MAX_REG;++i) { f->x[i/64]&=~(UINT64_C(1)<<(i%64)); f->xs[i]=0; }
}
static uint32_t common_shape(uint32_t a,uint32_t b)
{
    if(a==CONSUMED_VALUE || b==CONSUMED_VALUE) return CONSUMED_VALUE;
    if((a|b)&MATCH_CONTEXT_FLAGS) return a==b?a:POSSIBLE_MATCH_CONTEXT;
    if((a&TUPLE_SHAPE) && (b&TUPLE_SHAPE)) return a<b?a:b;
    return a==b?a:0;
}
static LbCodeStatus propagate(Analysis *w,size_t target,size_t function,const Flow *flow)
{
    Node *node; size_t r; int changed=0;
    REQUIRE(target<w->count && w->nodes[target].function==function);
    node=&w->nodes[target];
    /* func_info is a terminal exception boundary, never normal argument reentry. */
    if(node->op->op==genop_int_func_start_5) return LB_CODE_OK;
    if(!node->reached) { node->in=*flow; node->reached=1; changed=1; }
    else {
        Flow *old=&node->in;
        REQUIRE(old->frame==flow->frame && old->matching==flow->matching);
        for(r=0;r<BITS;++r) {
            uint64_t x=old->x[r]&flow->x[r],y=old->y[r]&flow->y[r];
            changed|=x!=old->x[r] || y!=old->y[r]; old->x[r]=x; old->y[r]=y;
        }
        for(r=0;r<MAX_REG;++r) {
            uint32_t x=defined(old->x,r)?common_shape(old->xs[r],flow->xs[r]):0;
            uint32_t y=defined(old->y,r)?common_shape(old->ys[r],flow->ys[r]):0;
            changed|=x!=old->xs[r] || y!=old->ys[r]; old->xs[r]=x; old->ys[r]=y;
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
static void forget_append_facts(Flow *f)
{
    /* A call or append through an alias can revoke a header. Do not confuse
     * its unchanged SubBits layout with permission for later private append. */
    for(size_t i=0;i<MAX_REG;++i) {
        if(f->xs[i]==APPEND_SHAPE) f->xs[i]=0;
        if(f->ys[i]==APPEND_SHAPE) f->ys[i]=0;
    }
}
static int same_register(BeamOpArg a,BeamOpArg b)
{
    return (a.type==TAG_x || a.type==TAG_y) && a.type==b.type &&
        ((Uint)a.val&REG_MASK)==((Uint)b.val&REG_MASK);
}
static LbCodeStatus binary_flow(LoaderState *st,Flow *f,Flow *failed,BeamOp *op)
{
    BeamOpArg *a=op->a; uint32_t result_shape=0; int consuming;
    REQUIRE(op->arity>=12 && a[5].type==TAG_u && a[5].val>=6 &&
            (Uint)a[5].val==op->arity-6 && a[5].val%6==0 && (Uint)a[5].val<=65536);
    REQUIRE(a[1].type==TAG_u && (Uint)a[1].val<=UINT32_MAX && a[2].type==TAG_u && roots(f,(Uint)a[2].val));
    /* Every X operand must survive the possible collection. Unlike addition,
     * construction reloads these values after allocating, not into scratch. */
    trim_live(f,(Uint)a[2].val);
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
        st->program->error.stage="private append source/consumption proof";
        REQUIRE((a[10].type==TAG_x || a[10].type==TAG_y) && shape(st,f,a[10])==APPEND_SHAPE);
        REQUIRE(destination(f,a[10],CONSUMED_VALUE)); result_shape=APPEND_SHAPE;
    } else if(a[6].val==am_append && shape(st,f,a[10])==APPEND_SHAPE) {
        result_shape=APPEND_SHAPE; /* zero extension also preserves this representation */
    }
    /* Appending can revoke a header reached through aliases. Even other
     * append facts are conservatively forgotten; only the new result gets one. */
    if(a[6].val==am_append || consuming) forget_append_facts(f);
    /* Any failure can follow private consumption, but never destination
     * publication. Poison also covers early failure with the original value
     * still in its initialized slot. Promise no heap credit on that edge. */
    *failed=*f; failed->heap=0;
    REQUIRE(destination(f,a[4],result_shape)); f->heap=(Uint)a[1].val;
    return LB_CODE_OK;
}
static uint32_t native_return_shape(LoaderState *st,size_t import)
{
    if(import>=st->beam.imports.count || !st->bif_imports) return 0;
    const BifEntry *native=st->bif_imports[import];
    if(native==&st->space->engine->bifs[BIF_list_to_tuple_1] ||
       native==&st->space->engine->bifs[BIF_make_tuple_2]) return TUPLE_SHAPE;
    if(native==&st->space->engine->bifs[BIF_spawn_opt_4]) return TUPLE_SHAPE|2;
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
static int plain_bif_allowed(const LbEngine *engine,const BifEntry *native,size_t arity)
{
    static const unsigned unary[]={BIF_is_function_1,BIF_map_size_1,BIF_is_integer_1,BIF_hd_1,BIF_tl_1,BIF_is_list_1,BIF_not_1,BIF_is_atom_1,BIF_is_binary_1,
#define LB_TYPE_PREDICATE(name, predicate) BIF_##name##_1,
#include "type_predicates.def"
#undef LB_TYPE_PREDICATE
        BIF_tuple_size_1};
    static const unsigned binary[]={BIF_is_function_2,BIF_max_2,BIF_min_2,BIF_is_map_key_2,BIF_seqeq_2};
    if(!native || native->kind!=BIF_KIND_GUARD) return 0;
    const unsigned *ids; size_t count;
    if(arity==1) { ids=unary; count=sizeof(unary)/sizeof(*unary); }
    else if(arity==2) { ids=binary; count=sizeof(binary)/sizeof(*binary); }
    else return 0;
    for(size_t i=0;i<count;++i) if(native==&engine->bifs[ids[i]]) return 1;
    return 0;
}
static LbCodeStatus verify_flow(LoaderState *st,Analysis *w)
{
    LbBeamProgram *p=st->program; BeamOp *op;
    Node *nodes=w->nodes; size_t *labels=w->labels,n=w->count,i,function=0,steps=0;
    for(i=0;i<st->label_count;++i) labels[i]=SIZE_MAX;
    for(op=p->ops,i=0;op;op=op->next,++i) {
        p->error.stage="operation arity"; p->error.offset=op->offset;
        REQUIRE(op->op<NUM_GENERIC_OPS);
        switch(op->op) {
        case genop_int_func_start_5:case genop_int_func_end_2:case genop_int_code_end_0:case genop_label_1:case genop_line_1:case genop_nif_start_0:
        case genop_move_2:case genop_swap_2:case genop_test_heap_2:case genop_put_tuple2_2:case genop_return_0:case genop_send_0:
        case genop_loop_rec_2:case genop_loop_rec_end_1:case genop_remove_message_0:
        case genop_wait_1:case genop_wait_timeout_2:case genop_timeout_0:
        case genop_recv_marker_reserve_1:case genop_recv_marker_bind_2:
        case genop_recv_marker_clear_1:case genop_recv_marker_use_1:
        case genop_call_2:case genop_call_only_2:case genop_call_last_3:
        case genop_call_fun_1:case genop_call_fun2_3:
        case genop_call_ext_2:case genop_call_ext_only_2:case genop_call_ext_last_3:
        case genop_apply_1:case genop_apply_last_2:
        case genop_allocate_2:case genop_allocate_heap_3:case genop_init_yregs_1:case genop_deallocate_1:case genop_trim_2:
        case genop_jump_1:case genop_is_eq_exact_3:case genop_is_ne_exact_3:case genop_is_nil_2:case genop_is_pid_2:case genop_is_reference_2:case genop_is_atom_2:
        case genop_is_binary_2:case genop_is_list_2:case genop_is_integer_2:case genop_is_map_2:case genop_is_function_2:case genop_is_function2_3:
        case genop_is_float_2:case genop_is_number_2:case genop_is_port_2:case genop_is_boolean_2:case genop_is_bitstr_2:
        case genop_is_tuple_2:case genop_test_arity_3:case genop_is_tagged_tuple_4:case genop_select_tuple_arity_3:case genop_select_val_3:
        case genop_is_nonempty_list_2:case genop_get_tuple_element_3:case genop_get_list_3:
        case genop_get_hd_2:case genop_get_tl_2:case genop_put_list_3:case genop_gc_bif1_5:case genop_gc_bif2_6:
        case genop_bs_match_3:case genop_bs_start_match3_4:case genop_bs_create_bin_6:case genop_bs_init_writable_0:case genop_bif0_2:case genop_bif1_4:case genop_bif2_5:case genop_make_fun3_3:
        case genop_new_map_3:case genop_put_map_assoc_5:case genop_put_map_exact_5:
        case genop_get_map_elements_3:case genop_has_map_fields_3:
        case genop_badmatch_1:case genop_case_end_1:break;
        default:p->error.stage=gen_opc[op->op].name; return LB_CODE_UNSUPPORTED;
        }
        if(op->op!=genop_select_val_3 && op->op!=genop_bs_match_3 && op->op!=genop_put_tuple2_2 && op->op!=genop_init_yregs_1 && op->op!=genop_select_tuple_arity_3 && op->op!=genop_bs_create_bin_6 && op->op!=genop_make_fun3_3 &&
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
            ++function; nodes[i].reached=nodes[i].queued=1; nodes[i].in.frame=NO_FRAME;
            labels[op->a[0].val]=i;
            for(r=0;r<arity;++r) define(nodes[i].in.x,r);
            w->queue[w->tail++]=i; ++w->pending;
        }
        nodes[i].function=function;
        if(op->op==genop_label_1) labels[op->a[0].val]=i;
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
        if(++steps>1048576) return LB_CODE_LIMIT;
        op=nodes[index].op; a=op->a; p->error.stage="register/stack/heap admission"; p->error.offset=op->offset;
        switch(op->op) {
        case genop_int_func_start_5:case genop_label_1:case genop_line_1:case genop_nif_start_0:break;
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
            REQUIRE(f.frame==NO_FRAME && a[0].type==TAG_u && (Uint)a[0].val<MAX_REG && a[r].type==TAG_u);
            live=(Uint)a[r].val; REQUIRE(roots(&f,live)); trim_live(&f,live);
            f.frame=(Uint)a[0].val; memset(f.y,0,sizeof(f.y)); memset(f.ys,0,sizeof(f.ys));
            if(r==2) { REQUIRE(a[1].type==TAG_u); f.heap=(Uint)a[1].val; } else f.heap=0;
            break;
        case genop_init_yregs_1:
            REQUIRE(a[0].type==TAG_u && (Uint)a[0].val==op->arity-1);
            for(r=1;r<op->arity;++r) REQUIRE(a[r].type==TAG_y && destination(&f,a[r],0));
            break;
        case genop_deallocate_1:
            REQUIRE(a[0].type==TAG_u && f.frame==(Uint)a[0].val);
            f.frame=NO_FRAME; memset(f.y,0,sizeof(f.y)); memset(f.ys,0,sizeof(f.ys)); break;
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
            for(r=2;r<op->arity;++r) REQUIRE(source(st,&f,a[r]));
            REQUIRE(destination(&f,a[0],TUPLE_SHAPE|(uint32_t)a[1].val)); f.heap-=(Uint)a[1].val+1; break;
        case genop_put_list_3:
            REQUIRE(f.heap>=2 && source(st,&f,a[0]) && source(st,&f,a[1]));
            REQUIRE(destination(&f,a[2],LIST_SHAPE)); f.heap-=2; break;
        case genop_get_tuple_element_3:
            REQUIRE(source(st,&f,a[0]) && a[1].type==TAG_u); s=shape(st,&f,a[0]);
            p->error.stage="tuple/list shape admission";
            REQUIRE((s&TUPLE_SHAPE) && (Uint)a[1].val<(s&ARITY_MASK));
            REQUIRE(destination(&f,a[2],0)); break;
        case genop_get_list_3:case genop_get_hd_2:case genop_get_tl_2:
            REQUIRE(source(st,&f,a[0])); p->error.stage="tuple/list shape admission";
            REQUIRE(shape(st,&f,a[0])==LIST_SHAPE);
            REQUIRE(destination(&f,a[1],0));
            if(op->op==genop_get_list_3) REQUIRE(destination(&f,a[2],0));
            break;
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
                   native!=&st->space->engine->bifs[BIF_sminus_1]) return LB_CODE_UNSUPPORTED;
            } else if(native!=&st->space->engine->bifs[BIF_splus_2] &&
                      native!=&st->space->engine->bifs[BIF_stimes_2] &&
                      native!=&st->space->engine->bifs[BIF_intdiv_2] &&
                      native!=&st->space->engine->bifs[BIF_rem_2]) return LB_CODE_UNSUPPORTED;
            REQUIRE(roots(&f,(Uint)a[1].val) && source(st,&f,a[3]) && (arity==1 || source(st,&f,a[4])));
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
        case genop_bif1_4:case genop_bif2_5: {
            size_t arity=op->op==genop_bif1_4?1:2;
            REQUIRE(a[1].type==TAG_u && (Uint)a[1].val<st->beam.imports.count);
            const BifEntry *native=st->bif_imports[a[1].val];
            if(native==&st->space->engine->bifs[BIF_get_1] && arity==1) {
                /* Compiler get/1 lowers to non-allocating dictionary lookup,
                 * not a guard. Never admit an exceptional guard edge. */
                REQUIRE((a[0].type==TAG_p || a[0].type==TAG_f) && !a[0].val);
            } else if(!plain_bif_allowed(st->space->engine,native,arity)) return LB_CODE_UNSUPPORTED;
            for(r=0;r<arity;++r) REQUIRE(source(st,&f,a[2+r]));
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
            REQUIRE((a[0].type==TAG_f && (!a[0].val || label(st,a[0]))) || (a[0].type==TAG_p && !a[0].val));
            REQUIRE(source(st,&f,a[1]) && shape(st,&f,a[1])==MAP_SHAPE);
            REQUIRE((a[2].type==TAG_x || a[2].type==TAG_y) && a[3].type==TAG_u && (Uint)a[3].val<MAX_REG);
            REQUIRE(a[4].type==TAG_u && !(a[4].val&1) && (Uint)a[4].val<=2*LB_FLATMAP_MAX_SIZE);
            size=(Uint)a[4].val; live=(Uint)a[3].val;
            REQUIRE(op->arity==5+size && roots(&f,live));
            for(r=5;r<op->arity;++r) REQUIRE(source(st,&f,a[r]));
            trim_live(&f,live); REQUIRE(destination(&f,a[2],MAP_SHAPE)); f.heap=0; break;
        }
        case genop_put_map_exact_5: {
            size_t size,live;
            REQUIRE(((a[0].type==TAG_f && label(st,a[0])) || (a[0].type==TAG_p && !a[0].val)) &&
                    source(st,&f,a[1]) && shape(st,&f,a[1])==MAP_SHAPE);
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
            REQUIRE(label(st,a[0]) && source(st,&f,a[1]) && shape(st,&f,a[1])==MAP_SHAPE);
            REQUIRE(a[2].type==TAG_u && (Uint)a[2].val<=LB_FLATMAP_MAX_SIZE);
            count=(Uint)a[2].val; REQUIRE(op->arity==3+count);
            for(r=3;r<op->arity;++r) REQUIRE(source(st,&f,a[r]));
            alternate=labels[a[0].val]; REQUIRE(alternate<n && nodes[alternate].function==nodes[index].function);
            break;
        }
        case genop_bif0_2:
            REQUIRE(a[0].type==TAG_u && (Uint)a[0].val<st->beam.imports.count);
            if(st->bif_imports[a[0].val]!=&st->space->engine->bifs[BIF_self_0] &&
               st->bif_imports[a[0].val]!=&st->space->engine->bifs[BIF_node_0]) return LB_CODE_UNSUPPORTED;
            REQUIRE(destination(&f,a[1],0)); break; /* identity storage never moves the term heap */
        case genop_bs_match_3: {
            REQUIRE(label(st,a[0]) && root_source(st,&f,a[1]) && shape(st,&f,a[1])==MATCH_CONTEXT_SHAPE);
            REQUIRE(a[2].type==TAG_u && a[2].val>0 && (Uint)a[2].val==op->arity-3);
            alternate=labels[a[0].val];
            REQUIRE(alternate<n && nodes[alternate].function==nodes[index].function);
            failed=f; failure=&failed;
            Uint available=0,packed=0;
            for(unsigned pos=3;pos<op->arity;) {
                REQUIRE(a[pos].type==TAG_a);
                Eterm command=(Eterm)a[pos].val;
                if(command==am_ensure_at_least) {
                    REQUIRE(op->arity-pos>=3 && a[pos+1].type==TAG_u && a[pos+2].type==TAG_u && a[pos+2].val==1);
                    available=(Uint)a[pos+1].val; pos+=3;
                } else if(command==am_ensure_exactly) {
                    REQUIRE(op->arity-pos>=2 && a[pos+1].type==TAG_u);
                    available=(Uint)a[pos+1].val; pos+=2;
                } else if(command==am_integer) {
                    REQUIRE(op->arity-pos>=6 && a[pos+1].type==TAG_u && roots(&f,(Uint)a[pos+1].val));
                    REQUIRE(a[pos+2].type==TAG_n || (a[pos+2].type==TAG_q && a[pos+2].val>=0 && (Uint)a[pos+2].val<p->literal_count && is_nil(p->literals[a[pos+2].val])));
                    REQUIRE(a[pos+3].type==TAG_u && a[pos+4].type==TAG_u && a[pos+4].val==1);
                    Uint bits=(Uint)a[pos+3].val;
                    REQUIRE(bits>0 && bits<SMALL_BITS && bits<=available && packed<=63-bits);
                    REQUIRE(!(a[pos+5].type==a[1].type && a[pos+5].val==a[1].val));
                    REQUIRE(destination(&f,a[pos+5],0)); available-=bits; packed+=bits; pos+=6;
                } else if(command==am_Eq) {
                    REQUIRE(op->arity-pos>=4 && a[pos+1].type==TAG_n && a[pos+2].type==TAG_u && a[pos+3].type==TAG_u);
                    Uint bits=(Uint)a[pos+2].val;
                    REQUIRE(bits>0 && bits<=63 && bits<=available && packed<=63-bits && ((Uint)a[pos+3].val>>bits)==0);
                    available-=bits; packed+=bits; pos+=4;
                } else return LB_CODE_UNSUPPORTED; /* other complete generator paths have no execution authority */
            }
            break;
        }
        case genop_bs_start_match3_4:
            REQUIRE(label(st,a[0]) && root_source(st,&f,a[1]) && a[2].type==TAG_u && (Uint)a[2].val<MAX_REG);
            REQUIRE(roots(&f,(Uint)a[2].val));
            alternate=labels[a[0].val];
            REQUIRE(alternate<n && nodes[alternate].function==nodes[index].function);
            /* Failure preserves original operands; success owns a mutable cursor. */
            trim_live(&f,(Uint)a[2].val); f.heap=0;
            REQUIRE(destination(&f,a[3],MATCH_CONTEXT_SHAPE)); break;
        case genop_bs_init_writable_0:
            REQUIRE(f.frame!=NO_FRAME && arguments(&f,1)); trim_live(&f,1);
            REQUIRE(destination(&f,(BeamOpArg){TAG_x,0},APPEND_SHAPE)); f.heap=0; break;
        case genop_bs_create_bin_6:
            p->error.stage="binary construction flow proof";
            status=binary_flow(st,&f,&failed,op); if(status!=LB_CODE_OK) return status;
            failure=&failed;
            if(a[0].type==TAG_p) REQUIRE(!a[0].val);
            else {
                REQUIRE(label(st,a[0])); alternate=labels[a[0].val];
                REQUIRE(alternate<n && nodes[alternate].function==nodes[index].function);
            }
            break;
        case genop_send_0:
            REQUIRE(source(st,&f,(BeamOpArg){TAG_x,0}) && source(st,&f,(BeamOpArg){TAG_x,1}));
            REQUIRE(destination(&f,(BeamOpArg){TAG_x,0},f.xs[1])); break;
        case genop_return_0:REQUIRE(!f.matching && f.frame==NO_FRAME && source(st,&f,(BeamOpArg){TAG_x,0})); terminal=1;break;
        case genop_call_fun_1:case genop_call_fun2_3:
            REQUIRE(!f.matching); r=op->op==genop_call_fun_1?0:1;
            REQUIRE(a[r].type==TAG_u && (Uint)a[r].val<=MAX_ARG && f.frame!=NO_FRAME);
            live=(Uint)a[r].val; REQUIRE(arguments(&f,live));
            REQUIRE(source(st,&f,op->op==genop_call_fun_1?(BeamOpArg){TAG_x,(Sint)live}:a[2]));
            forget_append_facts(&f); memset(f.x,0,sizeof(f.x)); memset(f.xs,0,sizeof(f.xs)); define(f.x,0); f.heap=0; break;
        case genop_apply_1:case genop_apply_last_2:
            REQUIRE(!f.matching && a[0].type==TAG_u && (Uint)a[0].val<=MAX_ARG);
            live=(Uint)a[0].val;
            for(size_t arg=0;arg<live+2;++arg) REQUIRE(source(st,&f,(BeamOpArg){TAG_x,(Sint)arg}));
            forget_append_facts(&f);
            if(op->op==genop_apply_1) {
                REQUIRE(f.frame!=NO_FRAME); memset(f.x,0,sizeof(f.x)); memset(f.xs,0,sizeof(f.xs)); define(f.x,0); f.heap=0;
            } else { REQUIRE(a[1].type==TAG_u && f.frame==(Uint)a[1].val); terminal=1; }
            break;
        case genop_call_2:case genop_call_only_2:case genop_call_last_3:
        case genop_call_ext_2:case genop_call_ext_only_2:case genop_call_ext_last_3: {
            uint32_t returned=0;
            REQUIRE(!f.matching && a[0].type==TAG_u); live=(Uint)a[0].val;
            if(op->op==genop_call_2 || op->op==genop_call_only_2 || op->op==genop_call_last_3) {
                size_t target;
                REQUIRE(label(st,a[1])); target=labels[a[1].val];
                REQUIRE(target>0 && target<n && nodes[target-1].op->op==genop_int_func_start_5);
                REQUIRE(live==(Uint)nodes[target-1].op->a[4].val);
                if(op->op==genop_call_2) returned=forwarding_return_shape(st,w,target);
            } else {
                REQUIRE(a[1].type==TAG_u && (Uint)a[1].val<st->beam.imports.count);
                imp=(Uint)a[1].val; REQUIRE(live==st->beam.imports.entries[imp].arity);
                if(op->op==genop_call_ext_2) returned=native_return_shape(st,imp);
            }
            for(size_t arg=0;arg<live && arg<MAX_REG;++arg) if(f.xs[arg]&MATCH_CONTEXT_FLAGS) {
                p->error.stage="match-context argument admission"; return LB_CODE_UNSUPPORTED;
            }
            REQUIRE(arguments(&f,live)); forget_append_facts(&f);
            if(op->op==genop_call_2 || op->op==genop_call_ext_2) {
                REQUIRE(f.frame!=NO_FRAME); memset(f.x,0,sizeof(f.x)); memset(f.xs,0,sizeof(f.xs)); define(f.x,0); f.heap=0;
                f.xs[0]=returned;
                if(op->op==genop_call_ext_2) {
                    Export *entry=st->module_code->imports[imp];
                    Eterm monitored;
                    if(entry->owner && entry->info.mfa.module==am_erlang && live==1 &&
                       lb_atoms_find(st->space->atoms,"spawn_monitor",13,&monitored)==LB_ATOM_OK && entry->info.mfa.function==monitored)
                        f.xs[0]=TUPLE_SHAPE|2; /* protected complete pinned wrapper, not a BIF alias */
                }
            } else {
                if(op->op==genop_call_only_2 || op->op==genop_call_ext_only_2) REQUIRE(f.frame==NO_FRAME);
                else REQUIRE(a[2].type==TAG_u && f.frame==(Uint)a[2].val);
                terminal=1;
            }
            break;
        }
        case genop_select_val_3: {
            REQUIRE(source(st,&f,a[0]) && !(shape(st,&f,a[0])&MATCH_CONTEXT_SHAPE) && label(st,a[1]));
            REQUIRE(op->arity==7 && a[2].type==TAG_u && a[2].val==4);
            REQUIRE(a[3].type==TAG_i && a[5].type==TAG_i && label(st,a[4]) && label(st,a[6]));
            REQUIRE(a[3].val!=a[5].val);
            Sint minimum=(Sint)a[3].val,maximum=(Sint)a[5].val;
            if(minimum>maximum) { Sint t=minimum; minimum=maximum; maximum=t; }
            if((Uint)maximum-(Uint)minimum<=4) return LB_CODE_UNSUPPORTED; /* native dense-table execution is not admitted */
            const unsigned edges[]={1,4,6};
            for(unsigned edge=0;edge<3;++edge) {
                status=propagate(w,labels[a[edges[edge]].val],nodes[index].function,&f);
                if(status!=LB_CODE_OK) return status;
            }
            terminal=1; break;
        }
        case genop_select_tuple_arity_3:
            status=select_arities(st,w,index,&f,op); if(status!=LB_CODE_OK) return status; terminal=1; break;
        case genop_is_eq_exact_3:case genop_is_ne_exact_3:case genop_is_nil_2:case genop_is_pid_2:case genop_is_reference_2:case genop_is_atom_2:
        case genop_is_binary_2:case genop_is_list_2:case genop_is_integer_2:case genop_is_map_2:case genop_is_function_2:case genop_is_function2_3:
        case genop_is_float_2:case genop_is_number_2:case genop_is_port_2:case genop_is_boolean_2:case genop_is_bitstr_2:
        case genop_is_tuple_2:case genop_test_arity_3:case genop_is_tagged_tuple_4:case genop_is_nonempty_list_2:
            REQUIRE(label(st,a[0]) && source(st,&f,a[1])); alternate=labels[a[0].val];
            REQUIRE(alternate<n && nodes[alternate].function==nodes[index].function);
            if((op->op==genop_is_integer_2 || op->op==genop_is_list_2 || op->op==genop_is_binary_2 ||
                op->op==genop_is_float_2 || op->op==genop_is_number_2 || op->op==genop_is_bitstr_2) && a[1].type==TAG_q) {
                /* Native literal lowering writes scratch x1023 before either
                 * branch. Old shape facts cannot survive that overwrite. */
                define(f.x,MAX_REG-1); f.xs[MAX_REG-1]=shape(st,&f,a[1]);
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
            else if(op->op==genop_is_eq_exact_3 || op->op==genop_is_ne_exact_3) REQUIRE(source(st,&f,a[2]));
            /* is_list also accepts NIL: only the nonempty test grants cons access. */
            else if(op->op==genop_is_nonempty_list_2) refine(&f,a[1],LIST_SHAPE);
            else if(op->op==genop_is_map_2) refine(&f,a[1],MAP_SHAPE);
            else if(op->op==genop_is_tuple_2) {
                s=shape(st,&f,a[1]); refine(&f,a[1],(s&TUPLE_SHAPE)?s:TUPLE_SHAPE);
            } else if(op->op==genop_test_arity_3 || op->op==genop_is_tagged_tuple_4) {
                REQUIRE(a[2].type==TAG_u && (Uint)a[2].val<=MAX_ARITYVAL);
                if(op->op==genop_test_arity_3) REQUIRE(shape(st,&f,a[1])&TUPLE_SHAPE);
                else REQUIRE(a[2].val>0 && a[3].type==TAG_a && is_atom((Eterm)a[3].val));
                refine(&f,a[1],TUPLE_SHAPE|(uint32_t)a[2].val);
            }
            break;
        case genop_wait_1:case genop_wait_timeout_2:
            REQUIRE(!f.matching && roots(&f,0) && label(st,a[0]));
            trim_live(&f,0); f.heap=0;
            if(op->op==genop_wait_1) next=labels[a[0].val];
            else {
                REQUIRE(a[1].type==TAG_i || a[1].type==TAG_a);
                alternate=labels[a[0].val];
            }
            break;
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
        case genop_badmatch_1:case genop_case_end_1:REQUIRE(source(st,&f,a[0])); terminal=1;break;
        default:return LB_CODE_FORMAT;
        }
        if(terminal) continue;
        status=propagate(w,next,nodes[index].function,&f); if(status!=LB_CODE_OK) return status;
        if(alternate!=SIZE_MAX) {
            /* Failed tests carry their incoming facts, never success refinements. */
            status=propagate(w,alternate,nodes[index].function,failure);
            if(status!=LB_CODE_OK) return status;
        }
    }
    return LB_CODE_OK;
}
LbCodeStatus lb_verify_program(LoaderState *st)
{
    Analysis w={0}; BeamOp *op; size_t node_bytes,label_bytes,queue_bytes,total; void *storage;
    LbCodeStatus status;
    for(op=st->program->ops;op;op=op->next) ++w.count;
    REQUIRE(w.count);
    if(!lb_size_mul(w.count,sizeof(Node),&node_bytes) || !lb_size_mul(st->label_count,sizeof(size_t),&label_bytes) ||
       !lb_size_mul(w.count,sizeof(size_t),&queue_bytes) || !lb_size_add(node_bytes,label_bytes,&total) ||
       !lb_size_add(total,queue_bytes,&total) || total>ANALYSIS_LIMIT) return LB_CODE_LIMIT;
    if(lb_alloc_domain_allocate(st->space->domain,total,&storage)!=LB_ALLOC_OK) return LB_CODE_NO_MEMORY;
    memset(storage,0,total); w.nodes=storage;
    w.labels=(size_t *)((unsigned char *)storage+node_bytes); w.queue=w.labels+st->label_count;
    status=verify_flow(st,&w);
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
