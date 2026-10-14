/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Owned timer boundary for native receive dispatch, serialized by the Engine.
 */
#define _POSIX_C_SOURCE 200809L
#include "receive.h"
#include "time_unit.h"
#include "code_internal.h"
#include <time.h>
static LbProcessStatus now_ns(uint64_t *out)
{
    struct timespec now;
    if(clock_gettime(CLOCK_MONOTONIC,&now) || now.tv_sec<0 ||
       (uint64_t)now.tv_sec>UINT64_MAX/LB_NATIVE_TIME_UNITS_PER_SECOND) return LB_PROCESS_ENVIRONMENT;
    uint64_t seconds=(uint64_t)now.tv_sec*LB_NATIVE_TIME_UNITS_PER_SECOND;
    if(now.tv_nsec<0 || (uint64_t)now.tv_nsec>=LB_NATIVE_TIME_UNITS_PER_SECOND || (uint64_t)now.tv_nsec>UINT64_MAX-seconds)
        return LB_PROCESS_ENVIRONMENT;
    *out=seconds+(uint64_t)now.tv_nsec;
    return LB_PROCESS_READY;
}
int lb_receive_is_infinity(LbProcess *p,Eterm term)
{
    LbBeamBytes name;
    return is_atom(term) && lb_atoms_name(p->entry_module->space->atoms,term,&name)==LB_ATOM_OK &&
           name.size==8 && !memcmp(name.data,"infinity",8);
}
LbProcessStatus lb_receive_timer_set(LbProcess *p,Uint milliseconds)
{
    uint64_t now;
    LbProcessStatus status=now_ns(&now);
    if(status!=LB_PROCESS_READY) return status;
    if(milliseconds>(UINT64_MAX-now)/UINT64_C(1000000)) return LB_PROCESS_LIMIT;
    p->receive_deadline=now+milliseconds*UINT64_C(1000000);
    p->receive_timed_out=0; p->flags|=F_INSLPQUEUE; return LB_PROCESS_READY;
}
void lb_receive_timer_cancel(LbProcess *p)
{
    p->flags&=~(F_INSLPQUEUE|F_TIMO);
    p->receive_timed_out=0; p->receive_deadline=0; p->receive_timeout_pc=NULL;
}
LbProcessStatus lb_receive_timer_poll(LbProcess *p)
{
    uint64_t now;
    if(!p->waiting || !(p->flags&F_INSLPQUEUE)) return LB_PROCESS_READY;
    LbProcessStatus status=now_ns(&now);
    if(status!=LB_PROCESS_READY) return status;
    if(now>=p->receive_deadline) {
        if(!p->receive_timeout_pc) return LB_PROCESS_INVALID;
        p->receive_timed_out=1; p->receive_deadline=0;
        p->flags&=~F_INSLPQUEUE; p->flags|=F_TIMO;
        p->i=p->receive_timeout_pc; p->waiting=0;
    }
    return LB_PROCESS_READY;
}
