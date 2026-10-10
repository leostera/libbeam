/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 2020-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Adapted from beam_file.c: BeamOpAllocator, operand marshalling and function
 * boundary synthesis. Operations live in the bounded prepared-program arena.
 */
#include "beam_program_internal.h"
#define REQUIRE(x) do { if (!(x)) return lb_pfail(p,LB_BEAM_BAD_FORMAT); } while (0)
static LbBeamOp *new_op(LbBeamProgram *p)
{
    LbBeamOp *op;
    if(!p->free_ops) {
        LbOpBlock *block=lb_palloc(p,1,sizeof(*block)); size_t i;
        if(!block) return NULL;
        for(i=0;i<31;++i) block->ops[i].next=&block->ops[i+1];
        p->free_ops=block->ops;
    }
    op=p->free_ops; p->free_ops=op->next; op->next=NULL; op->a=op->def_args;
    return op;
}
static int integer(LbBeamProgram *p,LbTaggedNumber *value)
{
    unsigned char *conv;
    size_t i,n=value->size;
    int negative;
    Eterm term;
    if(!n) return 1;
    conv=lb_palloc(p,n,1); if(!conv) return 0;
    for(i=0;i<n;++i) conv[n-i-1]=value->bytes[i];
    negative=!!(conv[n-1]&128);
    if(negative) {
        unsigned carry=1;
        for(i=0;i<n;++i) { conv[i]=(unsigned char)(~conv[i]+carry); carry=conv[i]==0 && carry==1; }
        REQUIRE(!carry);
    }
    if(!conv[n-1]) {
        --n; REQUIRE(n && conv[n-1]);
    }
    if(!lb_pinteger(p,conv,n,negative,&term) || !lb_pdynamic(p,term,&value->word)) return 0;
    value->tag=TAG_q; value->size=0; return 1;
}
static int allocation_list(LbBeamProgram *p,LbBeamReader *r,Sint *result)
{
    LbTaggedNumber count,kind,number;
    Sint sum=0,i;
    /* Selected upstream flat-64 layouts: ordinary word, FLOAT_SIZE_OBJECT,
     * ERL_FUN_SIZE and RECORD_INST_SIZE(0). This does not admit fun/record use. */
    static const Sint words[]={1,2,2,2};
    REQUIRE(lb_reader_tagged(r,&count) && count.tag==TAG_u && !count.size && count.word<=4);
    for(i=0;i<count.word;++i) {
        REQUIRE(lb_reader_tagged(r,&kind) && kind.tag==TAG_u && !kind.size && kind.word<4);
        REQUIRE(lb_reader_tagged(r,&number) && number.tag==TAG_u && !number.size && number.word<=INT32_MAX/3);
        REQUIRE(sum<=INT32_MAX-words[kind.word]*number.word);
        sum+=words[kind.word]*number.word;
    }
    *result=sum; return 1;
}
static int read_op(LbBeamProgram *p,LbBeamReader *r,LbBeamOp **out)
{
    unsigned opcode,i,arity;
    LbBeamOp *op;
    const LbBeamImageInfo *info=lb_beam_image_info(p->image);
    size_t offset=r->pos;
    REQUIRE(lb_reader_u8(r,&opcode) && opcode>0 && opcode<=MAX_GENERIC_OPCODE && opcode<=info->max_opcode);
    arity=(unsigned)gen_opc[opcode].arity;
    REQUIRE(arity<=8 && gen_opc[opcode].name[0]);
    op=new_op(p); if(!op) return 0;
    op->op=opcode; op->arity=arity; op->offset=offset;
    for(i=0;i<arity;++i) {
        LbTaggedNumber arg;
        REQUIRE(lb_reader_tagged(r,&arg));
        switch(arg.tag) {
        case TAG_u: case TAG_o: break;
        case TAG_a:
            REQUIRE((Uint)arg.word<=info->atom_count);
            if(!arg.word) { arg.tag=TAG_n; arg.word=NIL; }
            /* Positive atom indices become actual namespace terms only after
             * all fallible decoding/preparation has succeeded. */
            break;
        case TAG_f:
            if(!arg.word) arg.tag=TAG_p;
            else REQUIRE((Uint)arg.word<info->label_count);
            break;
        case TAG_i: if(!integer(p,&arg)) return 0; break;
        case TAG_x: case TAG_y: REQUIRE((Uint)arg.word<MAX_REG); break;
        case TAG_z:
            switch(arg.word) {
            case 0: return lb_pfail(p,LB_BEAM_UNSUPPORTED); /* old inline float */
            case 1: {
                LbTaggedNumber extra;
                REQUIRE(i+1==arity && op->a==op->def_args);
                REQUIRE(lb_reader_tagged(r,&extra) && extra.tag==TAG_u && !extra.size);
                /* Every extra operand consumes >= 1 byte, before any growth. */
                REQUIRE((Uint)extra.word<=r->size-r->pos);
                if((Uint)extra.word>65536-arity) return lb_pfail(p,LB_BEAM_LIMIT);
                arity+=(unsigned)extra.word; op->arity=arity;
                if(arity>8) {
                    op->a=lb_palloc(p,arity,sizeof(*op->a)); if(!op->a) return 0;
                    memcpy(op->a,op->def_args,i*sizeof(*op->a));
                }
                arg.tag=TAG_u; arg.word=extra.word; break;
            }
            case 2: {
                LbTaggedNumber index;
                REQUIRE(lb_reader_tagged(r,&index) && index.tag==TAG_u && !index.size && (Uint)index.word<MAX_REG);
                arg.tag=TAG_l; arg.word=index.word; break;
            }
            case 3:
                if(!allocation_list(p,r,&arg.word)) return 0;
                arg.tag=TAG_u; break;
            case 4: {
                LbTaggedNumber index;
                REQUIRE(lb_reader_tagged(r,&index) && index.tag==TAG_u && !index.size && (Uint)index.word<p->literal_count);
                arg.tag=TAG_q; arg.word=index.word; break;
            }
            case 5: {
                LbTaggedNumber index;
                REQUIRE(lb_reader_tagged(r,&arg) && (arg.tag==TAG_x || arg.tag==TAG_y) && !arg.size && (Uint)arg.word<MAX_REG);
                REQUIRE(lb_reader_tagged(r,&index) && index.tag==TAG_u && !index.size);
                if(!p->type_fallback) {
                    REQUIRE((Uint)index.word<p->type_count && (Uint)index.word<=INTPTR_MAX>>10);
                    arg.word|=index.word<<10;
                }
                break;
            }
            default: REQUIRE(0);
            }
            break;
        default: REQUIRE(0);
        }
        REQUIRE(!arg.size || arg.tag==TAG_o);
        op->a[i]=(LbBeamArg){arg.tag,arg.word};
    }
    *out=op; return 1;
}
static int atom_equal(LbBeamProgram *p,Sint a,Sint b)
{
    LbBeamBytes aa,bb;
    if(lb_beam_image_atom(p->image,(uint32_t)a,&aa)!=LB_BEAM_OK ||
       lb_beam_image_atom(p->image,(uint32_t)b,&bb)!=LB_BEAM_OK) return 0;
    return aa.size==bb.size && !memcmp(aa.data,bb.data,aa.size);
}
static LbBeamOp *func_end(LbBeamProgram *p,LbBeamOp *function)
{
    LbBeamOp *end=new_op(p); if(!end) return NULL;
    end->op=genop_int_func_end_2; end->arity=2; end->offset=function->offset;
    end->a[0]=(LbBeamArg){TAG_f,function->a[0].val};
    end->a[1]=(LbBeamArg){TAG_f,function->next->a[0].val};
    return end;
}
int lb_pdecode(LbBeamProgram *p)
{
    const LbBeamImageInfo *info=lb_beam_image_info(p->image);
    LbBeamReader r={info->code.data,info->code.size,0};
    LbBeamOp *op, **tail=&p->ops, **cursor, *current=NULL;
    LbBeamOp **labels, **entries;
    size_t functions=0,i;
    p->error.stage="operands";
    if(info->max_opcode>MAX_GENERIC_OPCODE) return lb_pfail(p,LB_BEAM_UNSUPPORTED);
    if(info->label_count>info->code.size) return lb_pfail(p,LB_BEAM_BAD_FORMAT);
    labels=lb_palloc(p,info->label_count,sizeof(*labels));
    entries=lb_palloc(p,info->label_count,sizeof(*entries));
    if(!labels || !entries) return 0;
    do {
        if(!read_op(p,&r,&op)) { p->error.offset=r.pos; return 0; }
        if(op->op==genop_label_1) {
            REQUIRE(op->a[0].type==TAG_u && op->a[0].val>0 && (Uint)op->a[0].val<info->label_count);
            REQUIRE(!labels[op->a[0].val]); labels[op->a[0].val]=op;
        }
        *tail=op; tail=&op->next;
    } while(op->op!=genop_int_code_end_0);
    REQUIRE(r.pos==r.size);
    /* Same label/[line]/func_info/entry-label synthesis as beamcodereader_next,
     * on an owned decoded list rather than a streaming one-instruction queue. */
    p->error.stage="function boundaries";
    cursor=&p->ops;
    while((op=*cursor)) {
        LbBeamOp *line=NULL,*mfa,*entry,*start,*end;
        p->error.offset=op->offset;
        if(op->op==genop_label_1 && op->next) {
            mfa=op->next;
            if(mfa->op==genop_line_1) { line=mfa; mfa=mfa->next; }
            REQUIRE(mfa && mfa->op!=genop_int_code_end_0);
            if(mfa->op==genop_func_info_3) {
                entry=mfa->next;
                REQUIRE(entry && entry->op==genop_label_1);
                REQUIRE(mfa->a[0].type==TAG_a && atom_equal(p,mfa->a[0].val,1));
                REQUIRE(mfa->a[1].type==TAG_a && mfa->a[2].type==TAG_u && (Uint)mfa->a[2].val<=255);
                start=new_op(p); if(!start) return 0;
                start->op=genop_int_func_start_5; start->arity=5; start->offset=op->offset;
                start->a[0]=op->a[0]; start->a[1]=line ? line->a[0] : (LbBeamArg){TAG_n,NIL};
                memcpy(start->a+2,mfa->a,3*sizeof(*mfa->a)); start->next=entry;
                if(current) {
                    end=func_end(p,current); if(!end) return 0;
                    *cursor=end; end->next=start;
                } else *cursor=start;
                entries[entry->a[0].val]=start; current=start; ++functions;
                cursor=&entry->next; continue;
            }
        }
        REQUIRE(op->op!=genop_func_info_3);
        if(op->op==genop_int_code_end_0) {
            REQUIRE(current);
            end=func_end(p,current); if(!end) return 0;
            *cursor=end; end->next=op; break;
        }
        cursor=&op->next;
    }
    REQUIRE(functions==info->function_count);
    for(op=p->ops;op;op=op->next) {
        ++p->op_count;
        for(i=0;i<op->arity;++i) if(op->a[i].type==TAG_f) REQUIRE(labels[op->a[i].val]);
    }
    for(i=0;i<info->export_count;++i) {
        LbBeamExport exp; REQUIRE(lb_beam_image_export(p->image,(uint32_t)i,&exp)==LB_BEAM_OK);
        op=entries[exp.label]; REQUIRE(op && atom_equal(p,op->a[3].val,exp.atom) && op->a[4].val==exp.arity);
    }
    for(i=0;i<p->lambda_count;++i) {
        LbBeamLambda *l=&p->lambdas[i]; op=entries[l->label];
        REQUIRE(op && atom_equal(p,op->a[3].val,(Sint)l->function) && op->a[4].val==l->arity);
    }
    return 1;
}
