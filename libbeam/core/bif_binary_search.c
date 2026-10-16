/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Bounded binary search/split operations used by URI query decoding.
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

static int find_bytes(const unsigned char *subject,size_t subject_size,
                      const unsigned char *pattern,size_t pattern_size,size_t *position)
{
    size_t i;
    if(!pattern_size || pattern_size>subject_size) return 0;
    for(i=0;i<=subject_size-pattern_size;++i)
        if(!memcmp(subject+i,pattern,pattern_size)) { *position=i; return 1; }
    return 0;
}

static int pattern_list(Eterm term,Eterm *patterns,size_t *count)
{
    if(lb_is_binary(term)) { patterns[0]=term; *count=1; return 1; }
    if(!is_list(term)) return 0;
    Eterm rest=term;
    size_t n=0;
    while(!is_nil(rest)) {
        if(!is_list(rest) || n==8) return 0;
        Eterm item=CAR(list_val(rest));
        if(!lb_is_binary(item)) return 0;
        patterns[n++]=item;
        rest=CDR(list_val(rest));
    }
    *count=n;
    return n!=0;
}

static int prepare_search(Eterm subject,Eterm patterns_term,
                          const unsigned char **bytes,size_t *length,Eterm *patterns,size_t *count)
{
    LbBitstringView view;
    if(!binary_view(subject,&view,bytes,length) || *length>65536 ||
       !pattern_list(patterns_term,patterns,count)) return 0;
    return 1;
}

Eterm lb_bif_binary_match_2(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)pc;
    p->last_native_cost=0;
    if(args!=p->reg) { p->status=LB_PROCESS_INVALID; return THE_NON_VALUE; }
    const unsigned char *subject;
    size_t subject_size,count,i,best=SIZE_MAX,best_size=0,total_work=1;
    Eterm patterns[8];
    LbBitstringView subject_view,pattern_view;
    if(!prepare_search(args[0],args[1],&subject,&subject_size,patterns,&count)) return badarg(p);
    for(i=0;i<count;++i) {
        const unsigned char *pattern;
        size_t pattern_size,position;
        if(!binary_view(patterns[i],&pattern_view,&pattern,&pattern_size) || !pattern_size) return badarg(p);
        if(!lb_size_add(total_work,subject_size,&total_work) ||
           !lb_size_add(total_work,pattern_size,&total_work)) { p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE; }
        if(find_bytes(subject,subject_size,pattern,pattern_size,&position) && position<best) {
            best=position; best_size=pattern_size;
        }
    }
    (void)subject_view;
    if(!charge(p,1+(total_work+15)/16)) return THE_NON_VALUE;
    if(best==SIZE_MAX) return am_nomatch;
    size_t need,before=p->collections;
    if(!lb_size_add(3,p->heap_reserved,&need)) { p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE; }
    if(!lb_heap_reserve(p,need,MAX_REG)) {
        LbAllocStatus status=lb_alloc_domain_failure(p->entry_module->space->domain);
        p->status=status==LB_ALLOC_WORK_LIMIT?LB_PROCESS_WORK_LIMIT:
            status==LB_ALLOC_NO_MEMORY?LB_PROCESS_NO_MEMORY:LB_PROCESS_LIMIT;
        return THE_NON_VALUE;
    }
    if(p->collections!=before) p->last_native_cost+=p->last_gc_cost;
    Eterm result=TUPLE2(p->htop,make_small((Sint)best),make_small((Sint)best_size));
    p->htop+=3;
    return result;
}

Eterm lb_bif_binary_split_2(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)pc;
    p->last_native_cost=0;
    if(args!=p->reg) { p->status=LB_PROCESS_INVALID; return THE_NON_VALUE; }
    const unsigned char *subject,*pattern;
    size_t subject_size,pattern_size,position;
    LbBitstringView subject_view,pattern_view;
    if(!binary_view(args[0],&subject_view,&subject,&subject_size) || subject_size>65536 ||
       !binary_view(args[1],&pattern_view,&pattern,&pattern_size) || !pattern_size) return badarg(p);
    if(!charge(p,1+(subject_size+15)/16)) return THE_NON_VALUE;
    int found=find_bytes(subject,subject_size,pattern,pattern_size,&position);
    size_t left_size=found?position:subject_size;
    size_t right_size=found?subject_size-position-pattern_size:0;
    size_t left_words=lb_bitstring_heap_words(left_size*8),right_words=lb_bitstring_heap_words(right_size*8),need;
    if((left_words==0 && left_size) || (right_words==0 && right_size) ||
       !lb_size_add(left_words,right_words,&need) || !lb_size_add(need,4,&need) ||
       !lb_size_add(need,p->heap_reserved,&need)) { p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE; }
    size_t before=p->collections;
    if(!lb_heap_reserve(p,need,MAX_REG)) {
        LbAllocStatus status=lb_alloc_domain_failure(p->entry_module->space->domain);
        p->status=status==LB_ALLOC_WORK_LIMIT?LB_PROCESS_WORK_LIMIT:
            status==LB_ALLOC_NO_MEMORY?LB_PROCESS_NO_MEMORY:LB_PROCESS_LIMIT;
        return THE_NON_VALUE;
    }
    if(p->collections!=before) p->last_native_cost+=p->last_gc_cost;
    /* GC may move the subject; recover the borrowed byte view from rooted X0. */
    if(!binary_view(p->reg[0],&subject_view,&subject,&subject_size)) { p->status=LB_PROCESS_INVALID; return THE_NON_VALUE; }
    Eterm left,right;
    LbAllocStatus status=lb_bitstring_build(p->entry_module->space->domain,&p->off_heap,&p->htop,
        subject,left_size*8,0,&left);
    if(status!=LB_ALLOC_OK) {
        p->status=status==LB_ALLOC_WORK_LIMIT?LB_PROCESS_WORK_LIMIT:
            status==LB_ALLOC_NO_MEMORY?LB_PROCESS_NO_MEMORY:LB_PROCESS_LIMIT;
        return THE_NON_VALUE;
    }
    if(found) {
        status=lb_bitstring_build(p->entry_module->space->domain,&p->off_heap,&p->htop,
            subject+position+pattern_size,right_size*8,0,&right);
        if(status!=LB_ALLOC_OK) {
            p->status=status==LB_ALLOC_WORK_LIMIT?LB_PROCESS_WORK_LIMIT:
                status==LB_ALLOC_NO_MEMORY?LB_PROCESS_NO_MEMORY:LB_PROCESS_LIMIT;
            return THE_NON_VALUE;
        }
        Eterm tail=CONS(p->htop,right,NIL); p->htop+=2;
        Eterm result=CONS(p->htop,left,tail); p->htop+=2;
        return result;
    }
    Eterm result=CONS(p->htop,left,NIL); p->htop+=2;
    return result;
}
