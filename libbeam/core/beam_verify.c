/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Admission checks for the explicit first interpreter profile. This is not a
 * replacement instruction executor or a claim of complete BEAM validation.
 * Must-definition/heap-credit dataflow follows every admitted branch. Native
 * emitters still validate their exact generated signatures and operand widths.
 */
#include "code_internal.h"
#define BITS (MAX_REG/64)
#define NO_FRAME SIZE_MAX
typedef struct { uint64_t x[BITS],y[BITS]; size_t frame,heap; } Flow;
typedef struct { BeamOp *op; Flow in; size_t function; int reached,queued; } Node;
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
static int destination(Flow *f,BeamOpArg arg)
{
    size_t reg=(Uint)arg.val&REG_MASK;
    if(arg.type==TAG_x) { define(f->x,reg); return 1; }
    if(arg.type==TAG_y && f->frame!=NO_FRAME && reg<f->frame) { define(f->y,reg); return 1; }
    return 0;
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
    size_t i; for(i=live;i<MAX_REG;++i) f->x[i/64]&=~(UINT64_C(1)<<(i%64));
}
#define REQUIRE(test) do { if(!(test)) return LB_CODE_FORMAT; } while(0)
LbCodeStatus lb_verify_program(LoaderState *st)
{
    LbBeamProgram *p=st->program; BeamOp *op;
    Node *nodes; size_t *labels,*queue,n=0,i,function=0,head=0,tail=0,pending=0,steps=0;
    for(op=p->ops;op;op=op->next) ++n;
    nodes=lb_palloc(p,n,sizeof(*nodes)); labels=lb_palloc(p,st->label_count,sizeof(*labels)); queue=lb_palloc(p,n,sizeof(*queue));
    if(!nodes || !labels || !queue) return p->error.status==LB_BEAM_LIMIT?LB_CODE_LIMIT:LB_CODE_NO_MEMORY;
    for(i=0;i<st->label_count;++i) labels[i]=SIZE_MAX;
    for(op=p->ops,i=0;op;op=op->next,++i) {
        p->error.stage="operation arity"; p->error.offset=op->offset;
        REQUIRE(op->op<NUM_GENERIC_OPS);
        /* An unadmitted variadic instruction is unsupported, not malformed
         * merely because it has more operands than the generic fixed prefix. */
        switch(op->op) {
        case genop_int_func_start_5:case genop_int_func_end_2:case genop_int_code_end_0:case genop_label_1:case genop_line_1:
        case genop_move_2:case genop_test_heap_2:case genop_put_tuple2_2:case genop_return_0:
        case genop_call_ext_2:case genop_call_ext_only_2:case genop_call_ext_last_3:
        case genop_allocate_2:case genop_allocate_heap_3:case genop_init_yregs_1:case genop_deallocate_1:
        case genop_jump_1:case genop_is_eq_exact_3:case genop_badmatch_1:case genop_case_end_1:break;
        default:p->error.stage=gen_opc[op->op].name; p->error.offset=op->offset; return LB_CODE_UNSUPPORTED;
        }
        if(op->op!=genop_put_tuple2_2 && op->op!=genop_init_yregs_1)
            REQUIRE(op->arity==(unsigned)gen_opc[op->op].arity);
        nodes[i].op=op;
        if(op->op==genop_int_func_start_5) {
            size_t r,arity=(Uint)op->a[4].val;
            ++function; nodes[i].reached=nodes[i].queued=1; nodes[i].in.frame=NO_FRAME;
            for(r=0;r<arity;++r) define(nodes[i].in.x,r);
            queue[tail++]=i; ++pending;
        }
        nodes[i].function=function;
        if(op->op==genop_label_1) labels[op->a[0].val]=i;
    }
    tail%=n;
    while(pending) {
        size_t index=queue[head],next=index+1,alternate=SIZE_MAX,edge,r,live,imp;
        Flow f=nodes[index].in;
        BeamOpArg *a;
        int terminal=0,changed=0;
        head=(head+1)%n; --pending; nodes[index].queued=0;
        if(++steps>1048576) return LB_CODE_LIMIT;
        op=nodes[index].op; a=op->a; p->error.stage="register/stack/heap admission"; p->error.offset=op->offset;
        switch(op->op) {
        case genop_int_func_start_5:case genop_label_1:case genop_line_1:break;
        case genop_move_2:REQUIRE(source(st,&f,a[0]) && destination(&f,a[1]));break;
        case genop_test_heap_2:
            REQUIRE(a[0].type==TAG_u && a[1].type==TAG_u && a[0].val>=0 && a[1].val>=0);
            live=(Uint)a[1].val; REQUIRE(roots(&f,live)); trim_live(&f,live); f.heap=(Uint)a[0].val; break;
        case genop_allocate_2:case genop_allocate_heap_3:
            r=op->op==genop_allocate_2?1:2;
            REQUIRE(f.frame==NO_FRAME && a[0].type==TAG_u && (Uint)a[0].val<MAX_REG && a[r].type==TAG_u);
            live=(Uint)a[r].val; REQUIRE(roots(&f,live)); trim_live(&f,live);
            f.frame=(Uint)a[0].val; memset(f.y,0,sizeof(f.y));
            if(r==2) { REQUIRE(a[1].type==TAG_u); f.heap=(Uint)a[1].val; } else f.heap=0;
            break;
        case genop_init_yregs_1:
            REQUIRE(a[0].type==TAG_u && (Uint)a[0].val==op->arity-1);
            for(r=1;r<op->arity;++r) REQUIRE(a[r].type==TAG_y && destination(&f,a[r]));
            break;
        case genop_deallocate_1:
            REQUIRE(a[0].type==TAG_u && f.frame==(Uint)a[0].val); f.frame=NO_FRAME; memset(f.y,0,sizeof(f.y)); break;
        case genop_put_tuple2_2:
            REQUIRE(a[1].type==TAG_u && a[1].val>0 && (Uint)a[1].val==op->arity-2);
            REQUIRE(f.heap>=(Uint)a[1].val+1);
            for(r=2;r<op->arity;++r) REQUIRE(source(st,&f,a[r]));
            REQUIRE(destination(&f,a[0])); f.heap-=(Uint)a[1].val+1; break;
        case genop_return_0:REQUIRE(f.frame==NO_FRAME && defined(f.x,0)); terminal=1;break;
        case genop_call_ext_2:case genop_call_ext_only_2:case genop_call_ext_last_3:
            REQUIRE(a[0].type==TAG_u && a[1].type==TAG_u && (Uint)a[1].val<st->beam.imports.count);
            imp=(Uint)a[1].val; live=(Uint)a[0].val;
            REQUIRE(live==st->beam.imports.entries[imp].arity && roots(&f,live));
            if(op->op==genop_call_ext_2) { REQUIRE(f.frame!=NO_FRAME); memset(f.x,0,sizeof(f.x)); define(f.x,0); f.heap=0; }
            else { if(op->op==genop_call_ext_only_2) REQUIRE(f.frame==NO_FRAME);
                   else REQUIRE(a[2].type==TAG_u && f.frame==(Uint)a[2].val);
                   terminal=1; }
            break;
        case genop_is_eq_exact_3:
            REQUIRE(a[0].type==TAG_f && a[0].val>0 && (Uint)a[0].val<st->label_count);
            REQUIRE(source(st,&f,a[1]) && source(st,&f,a[2]));
            alternate=labels[a[0].val]; REQUIRE(alternate<n && nodes[alternate].function==nodes[index].function); break;
        case genop_jump_1:
            REQUIRE(a[0].type==TAG_f && a[0].val>0 && (Uint)a[0].val<st->label_count);
            next=labels[a[0].val]; REQUIRE(next<n && nodes[next].function==nodes[index].function); break;
        case genop_badmatch_1:case genop_case_end_1:REQUIRE(source(st,&f,a[0])); terminal=1;break;
        default:return LB_CODE_FORMAT; /* reachable fallthrough past function end */
        }
        if(terminal) continue;
        for(edge=0;edge<(alternate==SIZE_MAX?1u:2u);++edge) {
            size_t target=edge?alternate:next;
            changed=0;
            REQUIRE(target<n && nodes[target].function==nodes[index].function);
            if(!nodes[target].reached) { nodes[target].in=f; nodes[target].reached=1; changed=1; }
            else {
                Flow *old=&nodes[target].in;
                REQUIRE(old->frame==f.frame);
                for(r=0;r<BITS;++r) {
                    uint64_t x=old->x[r]&f.x[r],y=old->y[r]&f.y[r];
                    changed|=x!=old->x[r] || y!=old->y[r]; old->x[r]=x; old->y[r]=y;
                }
                if(f.heap<old->heap) { old->heap=f.heap; changed=1; }
            }
            if(changed && !nodes[target].queued) {
                REQUIRE(pending<n); queue[tail]=target; tail=(tail+1)%n; ++pending; nodes[target].queued=1;
            }
        }
    }
    return LB_CODE_OK;
}
