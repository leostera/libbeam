/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Bounded ASCII regex profile for the HelloWorker literal patterns.
 */
#include "process_internal.h"
#include "binary.h"
#include "work_budget.h"

static Eterm badarg(LbProcess *p)
{
    p->freason=LB_EXC_BADARG;
    return THE_NON_VALUE;
}

static int charge(LbProcess *p,size_t units)
{
    if(!lb_work_domain_charge(p->entry_module->space->domain,(uint64_t)units)) {
        p->status=LB_PROCESS_WORK_LIMIT;
        return 0;
    }
    p->last_native_cost+=(uint64_t)units;
    return 1;
}

static int binary_view(Eterm term,LbBitstringView *view,const unsigned char **bytes,size_t *length)
{
    if(!lb_bitstring_view(term,view) || view->bit_size%8 || view->bit_offset%8) return 0;
    *bytes=view->data+view->bit_offset/8;
    *length=view->bit_size/8;
    return 1;
}

static int pattern_kind(Eterm source)
{
    LbBitstringView view;
    const unsigned char *bytes;
    size_t length;
    static const unsigned char uppercase[]="\\A[A-Z]+\\z";
    static const unsigned char lowercase[]="\\A[a-z0-9-]+\\z";
    if(!binary_view(source,&view,&bytes,&length)) return 0;
    if(length==sizeof(uppercase)-1 && !memcmp(bytes,uppercase,length)) return 1;
    if(length==sizeof(lowercase)-1 && !memcmp(bytes,lowercase,length)) return 2;
    return 0;
}

static int supported_options(Eterm options)
{
    Eterm current=options;
    size_t count=0;
    while(!is_nil(current)) {
        if(!is_list(current) || ++count>8) return 0;
        Eterm item=CAR(list_val(current));
        current=CDR(list_val(current));
        if(is_atom(item) && item==am_unicode) continue;
        if(!is_tuple(item) || arityval(*tuple_val(item))!=2) return 0;
        Eterm *fields=tuple_val(item);
        if(fields[1]!=am_capture || fields[2]!=am_none) return 0;
    }
    return 1;
}

Eterm lb_bif_re_import_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)pc;
    p->last_native_cost=0;
    if(args!=p->reg) { p->status=LB_PROCESS_INVALID; return THE_NON_VALUE; }
    if(!charge(p,1)) return THE_NON_VALUE;
    Eterm exported=args[0];
    if(!is_tuple(exported) || arityval(*tuple_val(exported))!=5 ||
       tuple_val(exported)[1]!=am_re_exported_pattern) return badarg(p);
    Eterm source=tuple_val(exported)[3];
    int kind=pattern_kind(source);
    if(!kind) { p->status=LB_PROCESS_REFUSED; return THE_NON_VALUE; }
    Eterm options=tuple_val(exported)[4];
    size_t need,before=p->collections;
    if(!lb_size_add(6,p->heap_reserved,&need)) { p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE; }
    if(!lb_heap_reserve(p,need,MAX_REG)) {
        LbAllocStatus status=lb_alloc_domain_failure(p->entry_module->space->domain);
        p->status=status==LB_ALLOC_WORK_LIMIT?LB_PROCESS_WORK_LIMIT:
            status==LB_ALLOC_NO_MEMORY?LB_PROCESS_NO_MEMORY:LB_PROCESS_LIMIT;
        return THE_NON_VALUE;
    }
    if(p->collections!=before) p->last_native_cost+=p->last_gc_cost;
    /* The custom pattern is an explicitly bounded two-class recognizer, not
     * a PCRE bytecode image. Source/options are retained for re:run/3. */
    Eterm marker=kind==1?am_true:am_false;
    p->htop[0]=make_arityval(5); p->htop[1]=am_re_pattern; p->htop[2]=marker;
    p->htop[3]=source; p->htop[4]=options; p->htop[5]=am_true;
    Eterm result=make_tuple(p->htop); p->htop+=6;
    return result;
}

Eterm lb_bif_re_version_0(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)pc;
    /* Distinguish this bounded recognizer from the PCRE engine that produced
     * the artifact's exported patterns; Regex then recompiles from source. */
    static const unsigned char version[]="libbeam-regex-subset/1";
    size_t bits=sizeof(version)-1,words,need,before=p->collections;
    if(args!=p->reg) { p->status=LB_PROCESS_INVALID; return THE_NON_VALUE; }
    p->last_native_cost=0;
    if(!charge(p,1)) return THE_NON_VALUE;
    if(!lb_size_mul(bits,8,&bits) || !(words=lb_bitstring_heap_words(bits)) ||
       !lb_size_add(words,p->heap_reserved,&need)) { p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE; }
    if(!lb_heap_reserve(p,need,MAX_REG)) {
        LbAllocStatus status=lb_alloc_domain_failure(p->entry_module->space->domain);
        p->status=status==LB_ALLOC_WORK_LIMIT?LB_PROCESS_WORK_LIMIT:
            status==LB_ALLOC_NO_MEMORY?LB_PROCESS_NO_MEMORY:LB_PROCESS_LIMIT;
        return THE_NON_VALUE;
    }
    if(p->collections!=before) p->last_native_cost+=p->last_gc_cost;
    Eterm result;
    LbAllocStatus status=lb_bitstring_build(p->entry_module->space->domain,&p->off_heap,&p->htop,
        version,bits,0,&result);
    if(status!=LB_ALLOC_OK) {
        p->status=status==LB_ALLOC_WORK_LIMIT?LB_PROCESS_WORK_LIMIT:
            status==LB_ALLOC_NO_MEMORY?LB_PROCESS_NO_MEMORY:LB_PROCESS_LIMIT;
        return THE_NON_VALUE;
    }
    return result;
}

Eterm lb_bif_re_run_3(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)pc;
    p->last_native_cost=0;
    if(args!=p->reg) { p->status=LB_PROCESS_INVALID; return THE_NON_VALUE; }
    LbBitstringView subject_view;
    const unsigned char *subject;
    size_t length;
    if(!binary_view(args[0],&subject_view,&subject,&length) || !supported_options(args[2])) return badarg(p);
    int kind=0;
    if(is_tuple(args[1]) && arityval(*tuple_val(args[1]))==5 &&
       tuple_val(args[1])[1]==am_re_pattern) {
        Eterm *fields=tuple_val(args[1]);
        if(fields[2]==am_true) kind=1;
        else if(fields[2]==am_false) kind=2;
        else { p->status=LB_PROCESS_REFUSED; return THE_NON_VALUE; }
    } else {
        kind=pattern_kind(args[1]);
        if(!kind) { p->status=LB_PROCESS_REFUSED; return THE_NON_VALUE; }
    }
    if(length>65536) { p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE; }
    size_t units=1+(length+15)/16;
    if(!charge(p,units)) return THE_NON_VALUE;
    size_t i;
    int matched=length!=0;
    for(i=0;i<length && matched;++i) {
        unsigned char c=subject[i];
        matched=kind==1?(c>='A' && c<='Z'):
            ((c>='a' && c<='z') || (c>='0' && c<='9') || c=='-');
    }
    return matched?am_match:am_nomatch;
}
