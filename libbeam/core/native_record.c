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

static Eterm bad_field(LbProcess *process,Eterm module,Eterm name,Eterm field,
                       size_t live,int reason)
{
    if(!lb_heap_reserve(process,6,live)) {
        process->freason=LB_EXC_SYSTEM_LIMIT; return THE_NON_VALUE;
    }
    Eterm *hp=process->htop;
    Eterm id=TUPLE2(hp,module,name);
    Eterm value=TUPLE2(hp+3,id,field);
    process->htop+=6;
    process->freason=reason;
    process->fvalue=value;
    return THE_NON_VALUE;
}

Eterm lb_record_create(LbProcess *process,const LbRecordDefinition *def,Eterm module,
                       size_t live,size_t update_words,const BeamInstr *updates)
{
    size_t n=def->field_count,j=0,order_words,needed,extra=0;
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
        return bad_field(process,module,def->name,updates[j],live,LB_EXC_BADFIELD);
    }
    if(!is_non_value(missing)) {
        return bad_field(process,module,def->name,missing,live,LB_EXC_NOVALUE);
    }
    order_words=n?1+n:2; needed=8+2*n+order_words;
    j=0;
    for(size_t i=0;i<n;++i) {
        if(j<update_words && updates[j]==def->fields[i].key) { j+=2; continue; }
        Eterm value=(Eterm)def->fields[i].default_value.val;
        if(is_boxed(value) || is_list(value)) {
            size_t words;
            if(!lb_flat_size(process->entry_module->space->domain,value,&words) ||
               words>SIZE_MAX-needed-extra) {
                process->freason=LB_EXC_SYSTEM_LIMIT; return THE_NON_VALUE;
            }
            extra+=words;
        }
    }
    if(!lb_heap_reserve(process,needed+extra,live)) {
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
    Eterm *top=hp+needed;
    LbBinRef *checkpoint=process->off_heap.first;
    j=0;
    for(size_t i=0;i<n;++i) {
        definition[6+i]=def->fields[i].key;
        if(j<update_words && updates[j]==def->fields[i].key) {
            instance[2+i]=source(process,updates[j+1]); j+=2;
        } else {
            Eterm value=(Eterm)def->fields[i].default_value.val;
            if(is_boxed(value) || is_list(value)) {
                value=lb_copy_flat(value,&top,&process->off_heap);
                if(is_non_value(value)) {
                    lb_offheap_rollback(&process->off_heap,checkpoint);
                    process->freason=LB_EXC_SYSTEM_LIMIT; return THE_NON_VALUE;
                }
            }
            instance[2+i]=value;
        }
    }
    process->htop=top;
    process->freason=LB_EXC_NORMAL;
    return make_boxed(instance);
}

Eterm lb_record_get_field(LbProcess *process,Eterm source,Eterm id,Eterm key,int local)
{
    LbRecordTermDefinition *definition=lb_record_term_definition(source);
    if(!definition) goto invalid;
    if(local) {
        if(id!=am_Underscore && definition->name!=id) goto invalid;
    } else {
        if(id!=am_Underscore) {
            if(!is_tuple(id) || arityval(*tuple_val(id))!=2 ||
               tuple_val(id)[1]!=definition->module ||
               tuple_val(id)[2]!=definition->name) goto invalid;
        }
        if(definition->is_exported!=am_true) goto invalid;
    }
    LbRecordTermInstance *instance=lb_record_instance(source);
    for(size_t i=0;i<lb_record_term_field_count(source);++i)
        if(definition->keys[i]==key) return instance->values[i];
    return bad_field(process,definition->module,definition->name,key,MAX_REG,LB_EXC_BADFIELD);
invalid:
    process->fvalue=source; process->freason=LB_EXC_BADRECORD;
    return THE_NON_VALUE;
}

Eterm lb_record_update(LbProcess *process,Eterm original,size_t live,
                       size_t update_words,const BeamInstr *updates)
{
    LbRecordTermInstance *old=lb_record_instance(original);
    LbRecordTermDefinition *definition=lb_record_term_definition(original);
    size_t n,j=0;
    if(!old || !definition || !is_atom(definition->module) ||
       !is_atom(definition->name)) {
        process->freason=LB_EXC_BADRECORD; process->fvalue=original;
        return THE_NON_VALUE;
    }
    n=lb_record_term_field_count(original);
    if(n>MAX_ARITYVAL-1 || live>=MAX_REG || !update_words ||
       (update_words&1) || update_words>2*n) {
        process->freason=LB_EXC_SYSTEM_LIMIT; return THE_NON_VALUE;
    }
    for(size_t i=0;i<n && j<update_words;++i)
        if(updates[j]==definition->keys[i]) j+=2;
    if(j<update_words)
        return bad_field(process,definition->module,definition->name,
                         updates[j],live,LB_EXC_BADFIELD);
    /* OTP preserves source in x[live] while collecting. It is dead on entry,
     * and the generated verifier must keep update operands below live. */
    process->reg[live]=original;
    if(!lb_heap_reserve(process,2+n,live+1)) {
        process->freason=LB_EXC_SYSTEM_LIMIT; return THE_NON_VALUE;
    }
    old=lb_record_instance(process->reg[live]);
    definition=lb_record_term_definition(process->reg[live]);
    Eterm *instance=process->htop;
    instance[0]=old->thing_word;
    instance[1]=old->record_definition;
    j=0;
    for(size_t i=0;i<n;++i) {
        if(j<update_words && updates[j]==definition->keys[i]) {
            instance[2+i]=source(process,updates[j+1]); j+=2;
        } else instance[2+i]=old->values[i];
    }
    process->htop+=2+n;
    process->reg[live]=NIL;
    process->freason=LB_EXC_NORMAL;
    return make_boxed(instance);
}
