/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#include "native_record.h"
#include "exception.h"

static Eterm source(LbProcess *process,Eterm value)
{
    switch(loader_tag(value)) {
    case LOADER_X_REG:return process->reg[loader_x_reg_index(value)];
    case LOADER_Y_REG:return process->stop[loader_y_reg_index(value)];
    default:return value;
    }
}

Eterm lb_record_create(LbProcess *process,const LbRecordDefinition *def,Eterm module,
                       size_t live,size_t update_words,const BeamInstr *updates)
{
    size_t n=def->field_count,j=0,order_words,needed;
    Eterm *hp,*definition,*order,*instance;
    if(n>MAX_ARITYVAL-5 || n>(SIZE_MAX-10)/3 || live>MAX_REG ||
       (update_words&1) || update_words>2*n) {
        process->freason=LB_EXC_SYSTEM_LIMIT; return THE_NON_VALUE;
    }
    /* OTP reports badfield in preference to a missing default. Do this
     * before reserving space so failure leaves no half-built heap object. */
    Eterm missing=THE_NON_VALUE;
    for(size_t i=0;i<n;++i) {
        if(j<update_words && updates[j]==def->fields[i].key) j+=2;
        else if(def->fields[i].default_value.type==TAG_u && is_non_value(missing))
            missing=def->fields[i].key;
    }
    if(j<update_words) {
        process->freason=LB_EXC_BADFIELD; process->fvalue=updates[j];
        return THE_NON_VALUE;
    }
    if(!is_non_value(missing)) {
        process->freason=LB_EXC_NOVALUE; process->fvalue=missing;
        return THE_NON_VALUE;
    }
    order_words=n?1+n:2; needed=8+2*n+order_words;
    if(!lb_heap_reserve(process,needed,live)) {
        process->freason=LB_EXC_SYSTEM_LIMIT; return THE_NON_VALUE;
    }
    hp=process->htop;
    definition=hp; order=hp+6+n; instance=order+order_words;
    definition[0]=make_arityval(5+n);
    definition[1]=make_tuple(order);
    definition[2]=make_arityval(3+n); /* temporary tuple for portable hash */
    definition[3]=module;
    definition[4]=def->name;
    definition[5]=def->exported?am_true:am_false;
    order[0]=make_arityval_unchecked(n);
    if(!n) order[1]=NIL; /* empty tuple read-ahead */
    for(size_t i=0;i<n;++i) {
        size_t original=def->fields[i].declaration_order;
        if(original>=n) { process->freason=LB_EXC_SYSTEM_LIMIT; return THE_NON_VALUE; }
        order[1+original]=make_small(i);
        definition[6+original]=def->fields[i].key;
    }
    Eterm hash_args[2]={make_tuple(definition+2),make_small(UINT64_C(1)<<32)};
    Eterm hash=lb_bif_phash2_2(process,hash_args,NULL);
    if(is_non_value(hash)) return hash;
    definition[2]=hash;
    instance[0]=make_record_header(n);
    instance[1]=make_tuple(definition);
    j=0;
    for(size_t i=0;i<n;++i) {
        definition[6+i]=def->fields[i].key;
        if(j<update_words && updates[j]==def->fields[i].key) {
            instance[2+i]=source(process,updates[j+1]); j+=2;
        } else {
            instance[2+i]=(Eterm)def->fields[i].default_value.val;
        }
    }
    process->htop+=needed;
    process->freason=LB_EXC_NORMAL;
    return make_boxed(instance);
}
