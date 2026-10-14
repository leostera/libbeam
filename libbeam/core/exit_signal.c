/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 2017-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Local exit-signal semantics from erl_proc_sig_queue.c. Owned message fragments
 * replace foreign heap pointers; Engine serialization orders signal delivery.
 */
#include "exit_signal.h"
#include "process_internal.h"
LbProcessStatus lb_exit_signal_send(LbProcess *sender,LbProcess *receiver,Eterm from,Eterm reason,LbExitSignalKind kind)
{
    if(!sender || !receiver || sender->entry_module->space!=receiver->entry_module->space || !is_internal_pid(from))
        return LB_PROCESS_INVALID;
    if(kind!=LB_EXIT_EXPLICIT && kind!=LB_EXIT_LINK) return LB_PROCESS_INVALID;
    LbAllocDomain *domain=sender->entry_module->space->domain;
    if(!lb_work_domain_charge(domain,1)) return LB_PROCESS_WORK_LIMIT;
    ++sender->last_native_cost;
    /* A pending terminal signal is already ordered before this one. Neither a
     * later kill nor a trapped signal can rewrite that first exit reason. */
    if(receiver->exit_signal || (receiver->status!=LB_PROCESS_READY && receiver->status!=LB_PROCESS_YIELDED))
        return LB_PROCESS_READY;
    int kill=kind==LB_EXIT_EXPLICIT && reason==am_kill;
    int trap=(receiver->flags&F_TRAP_EXIT) && !kill;
    if(!trap && !kill && reason==am_normal && !(kind==LB_EXIT_EXPLICIT && sender==receiver))
        return LB_PROCESS_READY;
    Eterm tuple[4]={make_arityval(3),am_EXIT,from,reason};
    Eterm payload=trap?make_boxed(tuple):kill?am_killed:reason;
    LbMessage *signal; Uint cost;
    LbProcessStatus status=lb_message_create(sender->entry_module->space,payload,&signal,&cost);
    sender->last_native_cost+=cost;
    if(status!=LB_PROCESS_READY) {
        LbAllocStatus cause=lb_alloc_domain_failure(domain);
        return cause==LB_ALLOC_WORK_LIMIT?LB_PROCESS_WORK_LIMIT:cause==LB_ALLOC_LIMIT?LB_PROCESS_LIMIT:status;
    }
    if(trap) {
        status=lb_message_deliver(receiver,signal);
        if(status!=LB_PROCESS_READY) lb_message_destroy(signal);
        return status;
    }
    lb_work_cross_effect(signal->budget,receiver->work_birth);
    receiver->exit_signal=signal; receiver->waiting=0;
    return LB_PROCESS_READY;
}
LbProcessStatus lb_exit_signal_receive(LbProcess *p)
{
    LbMessage *signal=p->exit_signal;
    if(!signal) return LB_PROCESS_INVALID;
    /* Abandon an interrupted selective-receive candidate without consuming it.
     * The terminal path then belongs to the exit sender, not that candidate or
     * the receiver's original invocation. */
    lb_work_receive_end(p,0);
    if(!lb_work_receive_begin(p,signal)) return p->status;
    Eterm reason; LbProcessStatus status=lb_message_import(p,signal,MAX_REG,&reason);
    lb_work_receive_end(p,1);
    if(status!=LB_PROCESS_READY) {
        LbAllocStatus cause=lb_alloc_domain_failure(p->entry_module->space->domain);
        return cause==LB_ALLOC_WORK_LIMIT?LB_PROCESS_WORK_LIMIT:cause==LB_ALLOC_LIMIT?LB_PROCESS_LIMIT:status;
    }
    p->freason=LB_EXC_EXIT; p->fvalue=p->exception_value=reason;
    p->exit_signal=NULL; lb_message_destroy(signal);
    return LB_PROCESS_EXCEPTION;
}
