/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1996-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Adapted from beam_load.c:load_code. Masks/order come from beam_makeops, not
 * a handwritten opcode interpretation or dispatch implementation.
 */
#include "beam_select.h"
LbBeamStatus lb_beam_select_specific(const LbBeamOp *op,LbBeamSelection *out)
{
    const GenOpEntry *generic;
    uint32_t mask[3]={0};
    unsigned arg,i,arity;
    int specific;
    if(!out) return LB_BEAM_INVALID_ARGUMENT;
    *out=(LbBeamSelection){0};
    if(!op || op->op>=NUM_GENERIC_OPS || !op->a) return LB_BEAM_INVALID_ARGUMENT;
    generic=&gen_opc[op->op]; arity=(unsigned)generic->arity;
    if(op->arity<arity) return LB_BEAM_BAD_FORMAT;
    if(!generic->num_specific) return LB_BEAM_UNSUPPORTED;
    specific=generic->specific;
    if(specific<0 || (unsigned)specific>=NUM_SPECIFIC_OPS ||
       (unsigned)generic->num_specific>NUM_SPECIFIC_OPS-(unsigned)specific) return LB_BEAM_BAD_FORMAT;
    if(generic->num_specific==1) { out->opcode=(unsigned)specific; return LB_BEAM_OK; }
    if(arity>6) return LB_BEAM_BAD_FORMAT;
    for(arg=0;arg<arity;++arg) {
        if(op->a[arg].type<0 || op->a[arg].type>=BEAM_NUM_TAGS) return LB_BEAM_BAD_FORMAT;
        mask[arg/2]|=(1u<<op->a[arg].type)<<((arg%2)<<4);
    }
    for(i=0;i<(unsigned)generic->num_specific;++i,++specific) {
        const OpEntry *entry=&opc[specific];
        if((entry->mask[0]&mask[0])==mask[0] && (entry->mask[1]&mask[1])==mask[1] &&
           (entry->mask[2]&mask[2])==mask[2]) {
            unsigned rewrite=0;
            if(entry->involves_r) {
                for(arg=0;arg<arity;++arg) if((entry->involves_r&(1u<<arg)) && op->a[arg].type==TAG_x) {
                    if(op->a[arg].val!=0) break;
                    rewrite|=1u<<arg;
                }
                if(arg!=arity) continue;
            }
            out->opcode=(unsigned)specific; out->r_mask=rewrite; return LB_BEAM_OK;
        }
    }
    return LB_BEAM_BAD_FORMAT;
}
