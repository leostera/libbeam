/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Owned system-clock boundary; POSIX clock access is isolated at this layer.
 */
#define _POSIX_C_SOURCE 200809L
#include "process_internal.h"
#include "code_internal.h"
#include "time_unit.h"
#include <time.h>

static void time_failure(LbProcess *p,LbAllocStatus failure)
{
    if(failure==LB_ALLOC_LIMIT) p->status=LB_PROCESS_LIMIT;
    else if(failure==LB_ALLOC_WORK_LIMIT) p->status=LB_PROCESS_WORK_LIMIT;
    else p->status=LB_PROCESS_NO_MEMORY;
}

static Eterm time_nanoseconds(LbProcess *p,clockid_t clock)
{
    struct timespec now;
    LbAllocDomain *domain=p->entry_module->space->domain;
    p->last_native_cost=1;
    if(!lb_work_domain_charge(domain,1)) {
        p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE;
    }
    if(clock_gettime(clock,&now) || now.tv_sec<0 || now.tv_nsec<0 ||
       (uint64_t)now.tv_nsec>=LB_NATIVE_TIME_UNITS_PER_SECOND ||
       (uint64_t)now.tv_sec>(UINT64_MAX-(uint64_t)now.tv_nsec)/LB_NATIVE_TIME_UNITS_PER_SECOND) {
        p->status=LB_PROCESS_ENVIRONMENT; return THE_NON_VALUE;
    }
    uint64_t nanoseconds=(uint64_t)now.tv_sec*LB_NATIVE_TIME_UNITS_PER_SECOND+(uint64_t)now.tv_nsec;
    if(nanoseconds<=MAX_SMALL) return make_small((Sint)nanoseconds);

    Eterm words[2]; const size_t digits=1; size_t needed;
    if(!lb_work_domain_charge(domain,digits+1)) {
        p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE;
    }
    p->last_native_cost+=digits+1;
    if(!lb_size_add(digits+1,p->heap_reserved,&needed)) {
        p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE;
    }
    size_t collections=p->collections;
    if(!lb_heap_reserve(p,needed,MAX_REG)) {
        time_failure(p,lb_alloc_domain_failure(domain)); return THE_NON_VALUE;
    }
    if(p->collections!=collections) p->last_native_cost+=p->last_gc_cost;
    words[0]=_make_header(digits,POS_BIG_SUBTAG);
    words[1]=(Uint)nanoseconds;
    memcpy(p->htop,words,(digits+1)*sizeof(Eterm));
    Eterm result=make_boxed(p->htop); p->htop+=digits+1;
    return result;
}

static Eterm time_in_unit(LbProcess *p,Eterm unit,clockid_t clock)
{
    Eterm value=time_nanoseconds(p,clock);
    if(is_non_value(value)) return value;
    Uint clock_cost=p->last_native_cost;
    /* During a moving collection, preserve the measured native clock term as
     * an explicit actor root. The unit argument itself remains in the X bank. */
    if(p->native_time_root!=NIL) abort();
    p->native_time_root=value;
    Eterm from=make_small(LB_NATIVE_TIME_UNITS_PER_SECOND);
    Eterm conversion[3]={p->native_time_root,from,unit};
    Eterm converted=lb_bif_convert_time_unit_3(p,conversion,NULL);
    p->last_native_cost+=clock_cost;
    p->native_time_root=NIL;
    return converted;
}

Eterm lb_bif_monotonic_time_0(LbProcess *p,Eterm *args,const BeamInstr *pc)
{ (void)args; (void)pc; return time_nanoseconds(p,CLOCK_MONOTONIC); }
Eterm lb_bif_monotonic_time_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{ (void)pc; return time_in_unit(p,args[0],CLOCK_MONOTONIC); }
Eterm lb_bif_system_time_0(LbProcess *p,Eterm *args,const BeamInstr *pc)
{ (void)args; (void)pc; return time_nanoseconds(p,CLOCK_REALTIME); }
Eterm lb_bif_system_time_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{ (void)pc; return time_in_unit(p,args[0],CLOCK_REALTIME); }
