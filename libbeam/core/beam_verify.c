/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Must-definition, shape and heap-credit dataflow over the admitted profile.
 * Native extraction instructions may omit checks only after this proof. Compiler
 * type hints are not evidence: facts come from real tests and constructors.
 */
#include "code_internal.h"
#define BITS (MAX_REG/64)
#define NO_FRAME SIZE_MAX
#define TUPLE_SHAPE UINT32_C(0x1000000)
#define LIST_SHAPE UINT32_C(0x2000000)
#define ARITY_MASK UINT32_C(0xffffff)
#define ANALYSIS_LIMIT (64u*1024u*1024u)
#define REQUIRE(test) do { if(!(test)) return LB_CODE_FORMAT; } while(0)
_Static_assert(MAX_ARITYVAL==ARITY_MASK,"Shape facts cover native tuple arities");
typedef struct {
    uint64_t x[BITS],y[BITS];
    uint32_t xs[MAX_REG],ys[MAX_REG]; /* tuple minimum arity, nonempty list, or unknown */
    size_t frame,heap;
} Flow;
typedef struct { BeamOp *op; Flow in; size_t function; int reached,queued; } Node;
typedef struct {
    Node *nodes; size_t *labels,*queue,count,head,tail,pending;
} Analysis;
static int defined(const uint64_t *set,size_t index) { return (int)((set[index/64]>>(index%64))&1); }
static void define(uint64_t *set,size_t index) { set[index/64]|=UINT64_C(1)<<(index%64); }
static int source(LoaderState *st,const Flow *f,BeamOpArg arg)
{
    size_t reg=(Uint)arg.val&REG_MASK;
    switch(arg.type) {
    case TAG_x:return defined(f->x,reg);
    case TAG_y:return f->frame!=NO_FRAME && reg<f->frame && defined(f->y,reg);
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
    }
    return 0;
}
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
static void trim_live(Flow *f,size_t live)
{
    size_t i;
    for(i=live;i<MAX_REG;++i) { f->x[i/64]&=~(UINT64_C(1)<<(i%64)); f->xs[i]=0; }
}
static uint32_t common_shape(uint32_t a,uint32_t b)
{
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
        REQUIRE(old->frame==flow->frame);
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
static LbCodeStatus verify_flow(LoaderState *st,Analysis *w)
{
    LbBeamProgram *p=st->program; BeamOp *op;
    Node *nodes=w->nodes; size_t *labels=w->labels,n=w->count,i,function=0,steps=0;
    for(i=0;i<st->label_count;++i) labels[i]=SIZE_MAX;
    for(op=p->ops,i=0;op;op=op->next,++i) {
        p->error.stage="operation arity"; p->error.offset=op->offset;
        REQUIRE(op->op<NUM_GENERIC_OPS);
        switch(op->op) {
        case genop_int_func_start_5:case genop_int_func_end_2:case genop_int_code_end_0:case genop_label_1:case genop_line_1:
        case genop_move_2:case genop_swap_2:case genop_test_heap_2:case genop_put_tuple2_2:case genop_return_0:
        case genop_call_2:case genop_call_only_2:case genop_call_last_3:
        case genop_call_ext_2:case genop_call_ext_only_2:case genop_call_ext_last_3:
        case genop_allocate_2:case genop_allocate_heap_3:case genop_init_yregs_1:case genop_deallocate_1:
        case genop_jump_1:case genop_is_eq_exact_3:case genop_is_ne_exact_3:case genop_is_nil_2:
        case genop_is_tuple_2:case genop_test_arity_3:case genop_is_tagged_tuple_4:case genop_select_tuple_arity_3:
        case genop_is_nonempty_list_2:case genop_get_tuple_element_3:case genop_get_list_3:
        case genop_get_hd_2:case genop_get_tl_2:case genop_put_list_3:
        case genop_badmatch_1:case genop_case_end_1:break;
        default:p->error.stage=gen_opc[op->op].name; return LB_CODE_UNSUPPORTED;
        }
        if(op->op!=genop_put_tuple2_2 && op->op!=genop_init_yregs_1 && op->op!=genop_select_tuple_arity_3)
            REQUIRE(op->arity==(unsigned)gen_opc[op->op].arity);
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
    w->tail%=n;
    while(w->pending) {
        size_t index=w->queue[w->head],next=index+1,alternate=SIZE_MAX,r,live,imp;
        Flow f=nodes[index].in;
        BeamOpArg *a; LbCodeStatus status; uint32_t s,t;
        int terminal=0;
        w->head=(w->head+1)%n; --w->pending; nodes[index].queued=0;
        if(++steps>1048576) return LB_CODE_LIMIT;
        op=nodes[index].op; a=op->a; p->error.stage="register/stack/heap admission"; p->error.offset=op->offset;
        switch(op->op) {
        case genop_int_func_start_5:case genop_label_1:case genop_line_1:break;
        case genop_move_2:
            REQUIRE(source(st,&f,a[0])); s=shape(st,&f,a[0]); REQUIRE(destination(&f,a[1],s)); break;
        case genop_swap_2:
            REQUIRE(source(st,&f,a[0]) && source(st,&f,a[1])); s=shape(st,&f,a[0]); t=shape(st,&f,a[1]);
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
        case genop_return_0:REQUIRE(f.frame==NO_FRAME && defined(f.x,0)); terminal=1;break;
        case genop_call_2:case genop_call_only_2:case genop_call_last_3:
        case genop_call_ext_2:case genop_call_ext_only_2:case genop_call_ext_last_3:
            REQUIRE(a[0].type==TAG_u); live=(Uint)a[0].val;
            if(op->op==genop_call_2 || op->op==genop_call_only_2 || op->op==genop_call_last_3) {
                size_t target;
                REQUIRE(label(st,a[1])); target=labels[a[1].val];
                REQUIRE(target>0 && target<n && nodes[target-1].op->op==genop_int_func_start_5);
                REQUIRE(live==(Uint)nodes[target-1].op->a[4].val);
            } else {
                REQUIRE(a[1].type==TAG_u && (Uint)a[1].val<st->beam.imports.count);
                imp=(Uint)a[1].val; REQUIRE(live==st->beam.imports.entries[imp].arity);
            }
            REQUIRE(roots(&f,live));
            if(op->op==genop_call_2 || op->op==genop_call_ext_2) {
                REQUIRE(f.frame!=NO_FRAME); memset(f.x,0,sizeof(f.x)); memset(f.xs,0,sizeof(f.xs)); define(f.x,0); f.heap=0;
            } else {
                if(op->op==genop_call_only_2 || op->op==genop_call_ext_only_2) REQUIRE(f.frame==NO_FRAME);
                else REQUIRE(a[2].type==TAG_u && f.frame==(Uint)a[2].val);
                terminal=1;
            }
            break;
        case genop_select_tuple_arity_3:
            status=select_arities(st,w,index,&f,op); if(status!=LB_CODE_OK) return status; terminal=1; break;
        case genop_is_eq_exact_3:case genop_is_ne_exact_3:case genop_is_nil_2:
        case genop_is_tuple_2:case genop_test_arity_3:case genop_is_tagged_tuple_4:case genop_is_nonempty_list_2:
            REQUIRE(label(st,a[0]) && source(st,&f,a[1])); alternate=labels[a[0].val];
            REQUIRE(alternate<n && nodes[alternate].function==nodes[index].function);
            if(op->op==genop_is_eq_exact_3 || op->op==genop_is_ne_exact_3) REQUIRE(source(st,&f,a[2]));
            else if(op->op==genop_is_nonempty_list_2) refine(&f,a[1],LIST_SHAPE);
            else if(op->op==genop_is_tuple_2) {
                s=shape(st,&f,a[1]); refine(&f,a[1],(s&TUPLE_SHAPE)?s:TUPLE_SHAPE);
            } else if(op->op==genop_test_arity_3 || op->op==genop_is_tagged_tuple_4) {
                REQUIRE(a[2].type==TAG_u && (Uint)a[2].val<=MAX_ARITYVAL);
                if(op->op==genop_test_arity_3) REQUIRE(shape(st,&f,a[1])&TUPLE_SHAPE);
                else REQUIRE(a[2].val>0 && a[3].type==TAG_a && is_atom((Eterm)a[3].val));
                refine(&f,a[1],TUPLE_SHAPE|(uint32_t)a[2].val);
            }
            break;
        case genop_jump_1:REQUIRE(label(st,a[0])); next=labels[a[0].val]; break;
        case genop_badmatch_1:case genop_case_end_1:REQUIRE(source(st,&f,a[0])); terminal=1;break;
        default:return LB_CODE_FORMAT;
        }
        if(terminal) continue;
        status=propagate(w,next,nodes[index].function,&f); if(status!=LB_CODE_OK) return status;
        if(alternate!=SIZE_MAX) {
            /* Failed tests carry their incoming facts, never success refinements. */
            status=propagate(w,alternate,nodes[index].function,&nodes[index].in);
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
