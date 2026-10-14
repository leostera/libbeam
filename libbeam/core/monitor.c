/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 2017-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * erl_monitor_link.h circular half lists, offset/container and owned ref data.
 * Only ordinary local process monitors; no aliases, distribution or tracing.
 */
#include "monitor.h"
#include "stacktrace.h"
#include "process_internal.h"
#define TARGET (UINT32_C(1)<<7)
#define IN_TABLE (UINT32_C(1)<<8)
static void insert(LbMonitorNode **list,LbMonitorNode *m)
{
    LbMonitorNode *first=*list;
    if(m->flags&IN_TABLE) abort();
    if(!first) { m->node.list.next=m->node.list.prev=m; *list=m; }
    else {
        m->node.list.next=first; m->node.list.prev=first->node.list.prev;
        first->node.list.prev=m; m->node.list.prev->node.list.next=m;
    }
    m->flags|=IN_TABLE;
}
static void erase(LbMonitorNode **list,LbMonitorNode *m)
{
    if(!(m->flags&IN_TABLE)) abort();
    if(m->node.list.next==m) { if(*list!=m) abort(); *list=NULL; }
    else {
        if(*list==m) *list=m->node.list.next;
        m->node.list.prev->node.list.next=m->node.list.next;
        m->node.list.next->node.list.prev=m->node.list.prev;
    }
    m->flags&=~IN_TABLE;
}
static LbMonitor *owner(LbMonitorNode *node)
{ return (LbMonitor *)((unsigned char *)node-node->offset); }
static int equal_ref(Eterm a,Eterm b)
{
    if(!is_boxed(a) || !is_boxed(b)) return 0;
    if(boxed_val(a)[0]!=LB_REF_HEADER || boxed_val(b)[0]!=LB_REF_HEADER) return 0;
    return !memcmp(boxed_val(a),boxed_val(b),LB_REF_WORDS*sizeof(Eterm));
}
LbProcessStatus lb_monitor_prepare(LbProcess *origin,Eterm ref,LbMonitor **out)
{
    *out=NULL;
    if(!lb_reference(ref)) return LB_PROCESS_INVALID;
    void *memory;
    if(lb_alloc_domain_allocate(origin->entry_module->space->domain,sizeof(LbMonitor),&memory)!=LB_ALLOC_OK)
        return LB_PROCESS_NO_MEMORY;
    LbMonitor *m=memory; memset(m,0,sizeof(*m));
    m->origin.offset=offsetof(LbMonitor,origin); m->u.target.offset=offsetof(LbMonitor,u.target);
    m->origin.key_offset=offsetof(LbMonitor,ref)-offsetof(LbMonitor,origin);
    m->u.target.key_offset=offsetof(LbMonitor,ref)-offsetof(LbMonitor,u.target);
    m->u.target.flags=TARGET; m->refc=0; m->origin_process=origin; m->target_name=NIL;
    memcpy(m->heap,boxed_val(ref),sizeof(m->heap)); m->ref=make_boxed(m->heap);
    *out=m; return LB_PROCESS_READY;
}
void lb_monitor_discard(LbMonitor *m)
{
    if(!m) return;
    if(m->refc || (m->origin.flags&IN_TABLE) || (m->u.target.flags&IN_TABLE)) abort();
    if(lb_alloc_domain_release(m->origin_process->entry_module->space->domain,m)!=LB_ALLOC_OK) abort();
}
void lb_monitor_publish(LbMonitor *m,LbProcess *target)
{
    if(m->refc || !target->identity.owner || !m->origin_process->identity.owner ||
       (!is_nil(m->target_name) && !is_atom(m->target_name)) ||
       target->entry_module->space!=m->origin_process->entry_module->space) abort();
    lb_work_cross_effect(m->origin_process->work_active,target->work_birth);
    m->target_process=target; m->origin.other.item=target->identity.id;
    m->u.target.other.item=m->origin_process->identity.id;
    insert(&m->origin_process->monitors_origin,&m->origin);
    insert(&target->monitors_target,&m->u.target); m->refc=2;
}
static void detach(LbMonitor *m)
{
    if(m->refc!=2) abort();
    erase(&m->origin_process->monitors_origin,&m->origin);
    erase(&m->target_process->monitors_target,&m->u.target); m->refc=0;
    lb_monitor_discard(m);
}
static void flush(LbProcess *p,Eterm ref)
{
    LbMessageQueue *q=&p->messages; LbMessage **saved=q->save,**link=&q->first;
    while(*link) {
        LbMessage *message=*link;
        LbAllocDomain *domain=p->entry_module->space->domain;
        LbWorkBudget *prior=lb_work_domain_owner(domain);
        lb_work_domain_borrow(domain,message->budget);
        int charged=lb_work_domain_charge(domain,1+LB_RECV_MARKERS);
        lb_work_domain_borrow(domain,prior);
        if(!charged) { p->status=LB_PROCESS_WORK_LIMIT; break; }
        Eterm value=message->m[0];
        if(is_tuple(value)) {
            Eterm *tuple=tuple_val(value);
            if(arityval(tuple[0])==5 && tuple[1]==am_DOWN && equal_ref(tuple[2],ref)) {
                if(saved==&message->next) saved=link;
                q->save=link;
                if(lb_message_queue_take(q)!=message) abort();
                lb_message_destroy(message); continue;
            }
        }
        link=&message->next;
    }
    q->save=saved;
}
void lb_monitor_demonitor(LbProcess *p,Eterm ref,int do_flush)
{
    LbMonitorNode *first=p->monitors_origin,*node=first;
    if(node) do {
        if(!lb_work_domain_charge(p->entry_module->space->domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return; }
        LbMonitor *m=owner(node); node=node->node.list.next;
        if(equal_ref(m->ref,ref)) { detach(m); break; }
    } while(node!=first);
    if(do_flush) flush(p,ref);
}
void lb_monitor_exit(LbProcess *p,int notify)
{
    /* An exiting origin cannot receive DOWN and owns no surviving halves. */
    while(p->monitors_origin) detach(owner(p->monitors_origin));
    Eterm error_reason=THE_NON_VALUE; LbProcessStatus captured=LB_PROCESS_READY;
    if(notify && p->monitors_target && p->status==LB_PROCESS_EXCEPTION && LB_GET_EXC_CLASS(p->freason)==LB_EXTAG_ERROR)
        captured=lb_error_down_reason(p,&error_reason);
    while(p->monitors_target) {
        LbMonitor *m=owner(p->monitors_target); LbProcess *origin=m->origin_process;
        if(notify) {
            LbProcessStatus status=LB_PROCESS_READY;
            Eterm reason=am_normal;
            if(p->status==LB_PROCESS_EXCEPTION && LB_GET_EXC_CLASS(p->freason)==LB_EXTAG_EXIT)
                reason=p->exception_value;
            else if(p->status==LB_PROCESS_EXCEPTION && LB_GET_EXC_CLASS(p->freason)==LB_EXTAG_ERROR) {
                status=captured; reason=error_reason;
            } else if(p->status!=LB_PROCESS_DONE) status=p->status==LB_PROCESS_EXCEPTION?LB_PROCESS_REFUSED:p->status;
            if(status==LB_PROCESS_READY) {
                Eterm named[3]={make_arityval(2),m->target_name,p->entry_module->space->node_name};
                Eterm object=is_atom(m->target_name)?make_boxed(named):p->identity.id;
                Eterm tuple[6]={make_arityval(5),am_DOWN,m->ref,am_process,object,reason};
                LbMessage *message; Uint cost;
                status=lb_message_create(p->entry_module->space,make_boxed(tuple),&message,&cost);
                if(status==LB_PROCESS_READY) {
                    lb_work_cross_effect(p->work_active,origin->work_birth);
                    status=lb_message_queue_append(&origin->messages,message);
                    if(status!=LB_PROCESS_READY) lb_message_destroy(message);
                }
            }
            if(status!=LB_PROCESS_READY) origin->status=status;
            origin->waiting=0;
        }
        detach(m);
    }
}
