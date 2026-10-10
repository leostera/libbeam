/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 2020-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Adapted from emu/emu_load.c: signature emission, packing, special instructions
 * and final fixups. Fixed 64-bit, CP_SIZE=1, upstream NO_JUMP_TABLE word ABI.
 * Checked growth/errors replace fatal ERTS allocation and malformed-input asserts.
 */
#include "code_internal.h"
#define VERIFY(x) do { if(!(x)) return LB_CODE_FORMAT; } while(0)
#define ALLOC(x) do { if(!(x)) return st->program->error.status==LB_BEAM_LIMIT?LB_CODE_LIMIT:LB_CODE_NO_MEMORY; } while(0)
static int need(LoaderState *st,size_t n)
{
    size_t wanted,capacity;
    BeamInstr *next;
    if(!lb_size_add(st->ci,n,&wanted)) return lb_pfail(st->program,LB_BEAM_LIMIT);
    if(wanted<=st->capacity) return 1;
    capacity=st->capacity ? st->capacity : 256;
    while(capacity<wanted) { if(capacity>SIZE_MAX/2) return lb_pfail(st->program,LB_BEAM_LIMIT); capacity*=2; }
    next=lb_palloc(st->program,capacity,sizeof(*next)); if(!next) return 0;
    if(st->ci && st->codev) memcpy(next,st->codev,st->ci*sizeof(*next));
    st->codev=next; st->capacity=capacity; return 1;
}
static int patch(LoaderState *st,LbPatch **list,size_t pos)
{
    LbPatch *p=lb_palloc(st->program,1,sizeof(*p)); if(!p) return 0;
    p->pos=pos; p->next=*list; *list=p; return 1;
}
static int label_patch(LoaderState *st,size_t label,size_t pos,Sint offset)
{
    Label *l=&st->labels[label];
    if(l->count==l->capacity) {
        size_t capacity=l->capacity?l->capacity*2:4;
        LabelPatch *p=lb_palloc(st->program,capacity,sizeof(*p)); if(!p) return 0;
        if(l->count) memcpy(p,l->patches,l->count*sizeof(*p));
        l->patches=p; l->capacity=capacity;
    }
    l->patches[l->count++]=(LabelPatch){pos,offset,0}; st->codev[pos]=label; return 1;
}
static LabelPatch *label_at(LoaderState *st,Uint label,size_t pos)
{
    Label *l;
    size_t i;
    if(!label || label>=st->label_count) return NULL;
    l=&st->labels[label];
    for(i=l->count;i;i--) if(l->patches[i-1].pos==pos) return &l->patches[i-1];
    return NULL;
}
LbCodeStatus lb_emit(LoaderState *st,BeamOp *op)
{
    const OpEntry *entry=&opc[st->specific_op];
    const char *sign=entry->sign, *prog;
    size_t arg=0, start, trailing=0, before=st->ci;
    ALLOC(need(st,(size_t)entry->sz+16));
    VERIFY(entry->adjust>=0 && (size_t)entry->adjust<16);
    start=st->ci+(size_t)entry->adjust;
    st->codev[st->ci++]=(unsigned)st->specific_op;
    while(*sign) {
        unsigned tag; Uint value;
        VERIFY(arg<op->arity && op->a[arg].type>=0 && op->a[arg].type<BEAM_NUM_TAGS);
        tag=(unsigned)op->a[arg].type; value=(Uint)op->a[arg].val;
        switch(*sign) {
        case 'r': case 'n': VERIFY(tag_to_letter[tag]==*sign); break;
        case 'x': case 'y':
            VERIFY(tag_to_letter[tag]==*sign);
            st->codev[st->ci++]=((value&REG_MASK)+(*sign=='y'?CP_SIZE:0))*sizeof(Eterm); break;
        case 'a': VERIFY(tag==TAG_a && is_atom(value)); st->codev[st->ci++]=value; break;
        case 'c': case 's':
            switch(tag) {
            case TAG_x: case TAG_y:
                VERIFY(*sign=='s');
                value=tag==TAG_x ? make_loader_x_reg(value&REG_MASK) : make_loader_y_reg((value&REG_MASK)+CP_SIZE); break;
            case TAG_i: VERIFY(IS_SSMALL(op->a[arg].val)); value=make_small(value); break;
            case TAG_a: VERIFY(is_atom(value)); break;
            case TAG_n: value=NIL; break;
            case TAG_q: {
                Eterm term;
                VERIFY(lb_beam_program_literal(st->program,st->space->atoms,(Sint)value,&term)==LB_BEAM_OK);
                VERIFY(loader_tag(term)!=LOADER_X_REG && loader_tag(term)!=LOADER_Y_REG);
                ALLOC(patch(st,&st->literal_patches,st->ci)); break;
            }
            default: return LB_CODE_FORMAT;
            }
            st->codev[st->ci++]=value; break;
        case 'd': case 'S':
            VERIFY(tag==TAG_x || tag==TAG_y);
            st->codev[st->ci++]=((value&REG_MASK)+(tag==TAG_y?CP_SIZE:0))*sizeof(Eterm)+(tag==TAG_y); break;
        case 't': case 'I': case 'W':
            VERIFY(tag==TAG_u);
            VERIFY(*sign!='t' || value<=UINT16_MAX);
            VERIFY(*sign!='I' || value<=UINT32_MAX);
            st->codev[st->ci++]=value; break;
        case 'A': VERIFY(tag==TAG_u && value<=MAX_ARITYVAL); st->codev[st->ci++]=value<<_HEADER_ARITY_OFFS; break;
        case 'f': case 'j':
            if(*sign=='j' && tag==TAG_p) st->codev[st->ci]=0;
            else {
                VERIFY(tag==TAG_f && value>0 && value<st->label_count);
                ALLOC(label_patch(st,value,st->ci,-(Sint)start));
            }
            ++st->ci; break;
        case 'L':
            VERIFY(st->specific_op==op_label_L && tag==TAG_u && value>0 && value<st->label_count);
            --st->ci; VERIFY(!st->labels[value].value);
            st->labels[value].value=st->ci; st->last_label=value; break;
        case 'e':
            VERIFY(tag==TAG_u && value<st->beam.imports.count);
            ALLOC(patch(st,&st->import_patches,st->ci)); st->codev[st->ci++]=value; break;
        case 'b': {
            LbBifFn function;
            _Static_assert(sizeof(function)==sizeof(BeamInstr),"Selected native function pointer ABI");
            VERIFY(tag==TAG_u && value<st->beam.imports.count && st->bif_imports[value]);
            function=st->bif_imports[value]->f; VERIFY(function);
            memcpy(&st->codev[st->ci++],&function,sizeof(function)); break;
        }
        case 'P': case 'Q':
            VERIFY(tag==TAG_u && value<(UINTPTR_MAX/sizeof(Eterm)));
            value=(value+1)*sizeof(Eterm); VERIFY(*sign!='Q' || value<=UINT16_MAX);
            st->codev[st->ci++]=value; break;
        case 'l': VERIFY(tag==TAG_l && value<MAX_REG); st->codev[st->ci++]=value*sizeof(double); break;
        case 'q':
            VERIFY(tag==TAG_q); ALLOC(patch(st,&st->literal_patches,st->ci)); st->codev[st->ci++]=value; break;
        case 'F':
            VERIFY(tag==TAG_u && value<st->program->lambda_count);
            ALLOC(patch(st,&st->lambda_patches,st->ci)); st->codev[st->ci++]=value; break;
        default: return LB_CODE_UNSUPPORTED;
        }
        ++sign; ++arg;
    }
    /* Upstream packing engine: saved operands keep pointers to fixup positions
     * so literal/relative-label relocation follows their new packed locations. */
    if(*entry->pack) {
        struct { BeamInstr word; size_t *position; } stack[8];
        unsigned sp=0;
        BeamInstr packed=0;
        LabelPatch *packed_label=NULL;
        for(prog=entry->pack;*prog;prog++) {
            Uint word;
            if(*prog=='g' || *prog=='f' || *prog=='q' || (*prog>='1' && *prog<='4')) VERIFY(st->ci>before+1);
            switch(*prog) {
            case 'g': case 'f': case 'q':
                VERIFY(sp<8); --st->ci; stack[sp].word=st->codev[st->ci]; stack[sp].position=NULL;
                if(*prog=='f' && stack[sp].word) {
                    LabelPatch *lp=label_at(st,stack[sp].word,st->ci); VERIFY(lp); stack[sp].position=&lp->pos;
                } else if(*prog=='q') {
                    LbPatch *lp;
                    for(lp=st->literal_patches;lp;lp=lp->next) if(lp->pos==st->ci) { stack[sp].position=&lp->pos; break; }
                }
                ++sp; break;
            case '1':
                word=st->codev[--st->ci]; VERIFY(!(word&~UINT64_C(0x1ff8)));
                packed=(packed<<BEAM_TIGHTEST_SHIFT)|(word>>3);
                if(packed_label) ++packed_label->packed;
                break;
            case '2': case '3':
                word=st->codev[--st->ci]; VERIFY(word<=UINT16_MAX);
                packed=(packed<<BEAM_LOOSE_SHIFT)|word;
                if(packed_label) ++packed_label->packed;
                break;
            case '4': {
                LabelPatch *lp;
                word=st->codev[--st->ci]; VERIFY(word<=UINT32_MAX);
                if(packed_label) ++packed_label->packed;
                lp=label_at(st,word,st->ci);
                if(lp) { lp->packed=1; packed_label=lp; }
                packed=(packed<<BEAM_WIDE_SHIFT)|(word&BEAM_WIDE_MASK); break;
            }
            case 'p':
                VERIFY(sp); --sp; st->codev[st->ci]=stack[sp].word;
                if(stack[sp].position) *stack[sp].position=st->ci;
                ++st->ci; break;
            case 'P':
                VERIFY(sp<8); stack[sp].word=packed; stack[sp].position=packed_label?&packed_label->pos:NULL;
                ++sp; packed=0; packed_label=NULL; break;
            default: return LB_CODE_FORMAT;
            }
        }
        VERIFY(!sp && !packed_label);
    }
    if(op->op==genop_put_tuple2_2) {
        VERIFY(op->arity>=2 && op->a[1].type==TAG_u && op->a[1].val>0 &&
               (Uint)op->a[1].val==op->arity-2 && (Uint)op->a[1].val<=MAX_ARITYVAL);
    }
    for(;arg<op->arity;++arg) {
        Uint value=(Uint)op->a[arg].val;
        int tag=op->a[arg].type;
        trailing=tag==TAG_f?trailing+1:0;
        ALLOC(need(st,1));
        switch(tag) {
        case TAG_i: VERIFY(IS_SSMALL(op->a[arg].val)); value=make_small(value); break;
        case TAG_u: case TAG_a: case TAG_v: break;
        case TAG_f:
            VERIFY(value>0 && value<st->label_count);
            ALLOC(label_patch(st,value,st->ci,-(Sint)start)); break;
        case TAG_x: value=make_loader_x_reg(value&REG_MASK); break;
        case TAG_y: value=make_loader_y_reg((value&REG_MASK)+CP_SIZE); break;
        case TAG_n: value=NIL; break;
        case TAG_q: ALLOC(patch(st,&st->literal_patches,st->ci)); break;
        default: return LB_CODE_FORMAT;
        }
        st->codev[st->ci++]=value;
    }
    if(trailing) {
        size_t src=st->ci-trailing,end=st->ci;
        st->ci=src;
        while(src<end) {
            Uint labels[2]={st->codev[src],src+1<end?st->codev[src+1]:0};
            unsigned i;
            Uint word=0;
            for(i=0;i<2;++i) {
                LabelPatch *lp=label_at(st,labels[i],src+i);
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
                unsigned half=2-i;
#else
                unsigned half=i+1;
#endif
                word|=labels[i]<<((half-1)*32);
                if(lp) { lp->pos=st->ci; lp->packed=half; }
            }
            st->codev[st->ci++]=word; src+=2;
        }
    }
    switch(st->specific_op) {
    case op_i_func_info_IaaI: {
        size_t fn_count=lb_beam_image_info(st->program->image)->function_count;
        _Static_assert(sizeof(LbCodeInfo)==5*sizeof(Eterm),"Native function header ABI");
        VERIFY(st->function_number<fn_count && st->ci>=5 && st->labels[st->last_label].value==st->ci-5);
        st->function=st->codev[st->ci-2]; st->arity=(unsigned)st->codev[st->ci-1];
        VERIFY(st->arity<=255 && is_atom(st->function) && st->codev[st->ci-3]==st->module);
        st->last_func_start=st->ci;
        ALLOC(label_patch(st,st->last_label,st->function_number,0)); ++st->function_number; break;
    }
    case op_line_I:
        /* Explicit no-line-instrumentation profile; metadata is retained below. */
        st->ci-=2; break;
    case op_nif_start: case op_i_nif_padding: case op_on_load: case op_i_debug_line_IIt:
        return LB_CODE_UNSUPPORTED;
    case op_i_bs_match_string_xfWW: case op_i_bs_match_string_yfWW:
        ALLOC(patch(st,&st->string_patches,st->ci-1)); break;
    case op_i_bs_create_bin_jIWdW:
    case op_catch_yf:
        return LB_CODE_UNSUPPORTED; /* require actual binary/catch consumers */
    case op_int_code_end:
        VERIFY(st->function_number==lb_beam_image_info(st->program->image)->function_count);
        break;
    default: break;
    }
    return LB_CODE_OK;
}
LbCodeStatus lb_finish_emit(LoaderState *st)
{
    size_t i,j,functions=lb_beam_image_info(st->program->image)->function_count;
    LbPatch *p;
    LbBeamBytes strings;
    st->codev[functions]=(Uint)(st->codev+st->ci-1);
    for(i=0;i<st->label_count;++i) {
        Label *l=&st->labels[i];
        VERIFY(!l->count || (l->value && l->value<st->ci));
        for(j=0;j<l->count;++j) {
            LabelPatch *lp=&l->patches[j]; Sint rel=(Sint)l->value+lp->offset;
            VERIFY(lp->pos<st->ci && rel>=INT32_MIN && rel<=INT32_MAX);
            if(lp->pos<functions) st->codev[lp->pos]=(Uint)(st->codev+l->value);
            else if(lp->packed==0) { VERIFY(st->codev[lp->pos]==i); st->codev[lp->pos]=(Uint)rel; }
            else {
                unsigned shift=(lp->packed-1)*32;
                VERIFY(lp->packed<=2 && ((st->codev[lp->pos]>>shift)&UINT32_MAX)==i);
                st->codev[lp->pos]=(st->codev[lp->pos]&~((Uint)UINT32_MAX<<shift))|((Uint)(uint32_t)rel<<shift);
            }
        }
    }
    for(p=st->literal_patches;p;p=p->next) {
        Eterm term;
        VERIFY(p->pos<st->ci && lb_beam_program_literal(st->program,st->space->atoms,(Sint)st->codev[p->pos],&term)==LB_BEAM_OK);
        st->codev[p->pos]=term;
    }
    VERIFY(lb_beam_image_chunk(st->program->image,LB_BEAM_ID('S','t','r','T'),&strings)==LB_BEAM_OK);
    for(p=st->string_patches;p;p=p->next) {
        VERIFY(p->pos<st->ci && st->codev[p->pos]<strings.size);
        st->codev[p->pos]=(Uint)(strings.data+st->codev[p->pos]);
    }
    for(p=st->import_patches;p;p=p->next) {
        VERIFY(p->pos<st->ci && st->codev[p->pos]<st->module_code->import_count);
        st->codev[p->pos]=(Uint)st->module_code->imports[st->codev[p->pos]];
    }
    if(st->lambda_patches) return LB_CODE_UNSUPPORTED;
    st->module_code->words=st->codev; st->module_code->word_count=st->ci;
    return LB_CODE_OK;
}
