/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 2017-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Local symmetric link halves, adapted from erl_monitor_link.h. The Engine
 * serializes membership; a single domain allocation owns both halves.
 */
#include "link.h"
#include "process_internal.h"
#include "exit_signal.h"
#include "stacktrace.h"
struct LbLinkHalf { LbLinkHalf *next,*prev; LbLink *owner; LbProcess *process; };
struct LbLink { LbLinkHalf halves[2]; LbAllocDomain *domain; int published; };
static void insert(LbLinkHalf *h)
{
    LbLinkHalf **head=&h->process->links;
    if(!*head) { h->next=h->prev=h; *head=h; }
    else { h->next=*head; h->prev=(*head)->prev; h->prev->next=h; (*head)->prev=h; }
}
static void erase(LbLinkHalf *h)
{
    LbLinkHalf **head=&h->process->links;
    if(h->next==h) { if(*head!=h) abort(); *head=NULL; }
    else { if(*head==h) *head=h->next; h->prev->next=h->next; h->next->prev=h->prev; }
    h->next=h->prev=NULL;
}
static LbProcess *other(const LbLinkHalf *half)
{ return half==&half->owner->halves[0]?half->owner->halves[1].process:half->owner->halves[0].process; }
LbProcessStatus lb_link_prepare(LbProcess *origin,LbLink **out)
{
    *out=NULL; void *memory; LbAllocDomain *domain=origin->entry_module->space->domain;
    if(lb_alloc_domain_allocate(domain,sizeof(LbLink),&memory)!=LB_ALLOC_OK) {
        LbAllocStatus cause=lb_alloc_domain_failure(domain);
        return cause==LB_ALLOC_WORK_LIMIT?LB_PROCESS_WORK_LIMIT:cause==LB_ALLOC_LIMIT?LB_PROCESS_LIMIT:LB_PROCESS_NO_MEMORY;
    }
    LbLink *link=memory; memset(link,0,sizeof(*link)); link->domain=domain;
    link->halves[0].owner=link->halves[1].owner=link; link->halves[0].process=origin;
    *out=link; return LB_PROCESS_READY;
}
void lb_link_discard(LbLink *link)
{ if(link->published) abort(); lb_release(link->domain,link); }
void lb_link_publish(LbLink *link,LbProcess *target)
{
    LbProcess *origin=link->halves[0].process;
    if(link->published || origin==target || !origin->identity.owner || !target->identity.owner ||
       origin->entry_module->space!=target->entry_module->space) abort();
    link->halves[1].process=target; insert(&link->halves[0]); insert(&link->halves[1]); link->published=1;
    lb_work_cross_effect(origin->work_active,target->work_birth);
}
static void detach(LbLink *link)
{
    if(!link->published) abort();
    erase(&link->halves[0]); erase(&link->halves[1]); link->published=0; lb_link_discard(link);
}
static Eterm modify(LbProcess *p,Eterm pid,int remove)
{
    LbAllocDomain *domain=p->entry_module->space->domain; p->last_native_cost=1;
    if(!lb_work_domain_charge(domain,1)) goto work;
    if(!is_internal_pid(pid)) {
        if(is_external_pid(pid) || (pid&_TAG_IMMED1_MASK)==_TAG_IMMED1_PORT) p->status=LB_PROCESS_REFUSED;
        else p->freason=LB_EXC_BADARG;
        return THE_NON_VALUE;
    }
    LbLinkHalf *first=p->links,*half=first;
    if(half) do {
        if(!lb_work_domain_charge(domain,1)) goto work;
        ++p->last_native_cost;
        if(other(half)->identity.id==pid) {
            if(remove) { lb_work_cross_effect(p->work_active,other(half)->work_birth); detach(half->owner); }
            return am_true;
        }
        half=half->next;
    } while(half!=first);
    if(remove || (p->identity.owner && p->identity.id==pid)) return am_true;
    LbProcess *target=lb_pid_lookup(p->entry_module->space->pids,pid);
    if(!target || (target->status!=LB_PROCESS_READY && target->status!=LB_PROCESS_YIELDED)) {
        if(!(p->flags&F_TRAP_EXIT)) { p->freason=LB_EXC_NOPROC; return THE_NON_VALUE; }
        LbProcessStatus status=lb_exit_signal_send(p,p,pid,am_noproc,LB_EXIT_LINK);
        if(status==LB_PROCESS_READY) return am_true;
        p->status=status; return THE_NON_VALUE;
    }
    Eterm self; LbProcessStatus status=lb_process_identity(p,&self);
    if(status!=LB_PROCESS_READY) { p->status=status; return THE_NON_VALUE; }
    LbLink *link; status=lb_link_prepare(p,&link);
    if(status!=LB_PROCESS_READY) { p->status=status; return THE_NON_VALUE; }
    lb_link_publish(link,target); return am_true;
work: p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE;
}
Eterm lb_bif_link_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{ (void)pc; return modify(p,args[0],0); }
Eterm lb_bif_unlink_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{ (void)pc; return modify(p,args[0],1); }
LbProcessStatus lb_link_count(LbProcess *caller,LbProcess *target,size_t *out)
{
    *out=0; LbLinkHalf *first=target->links,*half=first;
    if(half) do {
        if(!lb_work_domain_charge(caller->entry_module->space->domain,1)) return LB_PROCESS_WORK_LIMIT;
        ++caller->last_native_cost; ++*out; half=half->next;
    } while(half!=first);
    return LB_PROCESS_READY;
}
Eterm lb_link_list(LbProcess *target,Eterm **hp)
{
    Eterm list=NIL; LbLinkHalf *first=target->links,*half=first;
    if(half) do { list=CONS(*hp,other(half)->identity.id,list); *hp+=2; half=half->next; } while(half!=first);
    return list;
}
void lb_link_exit(LbProcess *p,int notify)
{
    Eterm reason=am_normal; LbProcessStatus status=LB_PROCESS_READY;
    if(notify && p->links) {
        if(p->status==LB_PROCESS_EXCEPTION && LB_GET_EXC_CLASS(p->freason)==LB_EXTAG_EXIT) reason=p->exception_value;
        else if(p->status==LB_PROCESS_EXCEPTION && LB_GET_EXC_CLASS(p->freason)==LB_EXTAG_ERROR) status=lb_error_down_reason(p,&reason);
        else if(p->status!=LB_PROCESS_DONE) status=p->status==LB_PROCESS_EXCEPTION?LB_PROCESS_REFUSED:p->status;
    }
    while(p->links) {
        LbLink *link=p->links->owner; LbProcess *target=other(p->links);
        if(notify) {
            if(status==LB_PROCESS_READY) status=lb_exit_signal_send(p,target,p->identity.id,reason,LB_EXIT_LINK);
            if(status!=LB_PROCESS_READY) {
                /* Infrastructure failure is never an EXIT message. Preserve its
                 * cause for the source and wake affected linked actors to fail. */
                LbAllocStatus cause=lb_alloc_domain_failure(p->entry_module->space->domain);
                if(cause==LB_ALLOC_WORK_LIMIT) status=LB_PROCESS_WORK_LIMIT;
                else if(cause==LB_ALLOC_LIMIT) status=LB_PROCESS_LIMIT;
                else if(cause==LB_ALLOC_NO_MEMORY) status=LB_PROCESS_NO_MEMORY;
                p->status=status;
                if(target->status==LB_PROCESS_READY || target->status==LB_PROCESS_YIELDED) { target->status=status; target->waiting=0; }
            }
        }
        detach(link);
    }
}
