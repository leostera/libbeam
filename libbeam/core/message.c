/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1996-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Owned adaptation of native self-contained message heap fragments/copying.
 */
#include "message.h"
#include "process_internal.h"

/* Selected native do_send local-PID branch: dead PIDs drop the message and
 * return it; live receivers own a self-contained copy before queue mutation.
 * Names/ports/external destinations have no execution admission in this slice. */
Eterm lb_erl_send(LbProcess *sender,Eterm destination,Eterm term)
{
    sender->last_native_cost=1;
    if(!is_internal_pid(destination)) {
        if(is_atom(destination) || is_pid(destination)) sender->status=LB_PROCESS_REFUSED;
        else sender->freason=LB_EXC_BADARG;
        return THE_NON_VALUE;
    }
    LbCodeSpace *space=sender->entry_module->space;
    if(!sender->world || !sender->running) { sender->status=LB_PROCESS_REFUSED; return THE_NON_VALUE; }
    LbProcess *receiver=lb_pid_lookup(space->pids,destination);
    if(!receiver) return term;
    if(receiver->messages.mq_len==INTPTR_MAX) { sender->status=LB_PROCESS_LIMIT; return THE_NON_VALUE; }
    Eterm from; LbProcessStatus status=lb_process_identity(sender,&from);
    if(status!=LB_PROCESS_READY) { sender->status=status; return THE_NON_VALUE; }
    Uint identity_cost=sender->last_native_cost,cost; LbMessage *message;
    status=lb_message_create(space,term,&message,&cost); sender->last_native_cost=identity_cost+cost;
    if(status!=LB_PROCESS_READY) { sender->status=status; return THE_NON_VALUE; }
    message->m[2]=from;
    status=lb_message_queue_append(&receiver->messages,message);
    if(status!=LB_PROCESS_READY) { lb_message_destroy(message); sender->status=status; return THE_NON_VALUE; }
    return term;
}
LbProcessStatus lb_message_import(LbProcess *receiver,const LbMessage *message,size_t live,Eterm *out)
{
    if(!out) return LB_PROCESS_INVALID;
    *out=THE_NON_VALUE;
    if(!receiver || !message || live>MAX_REG ||
       receiver->entry_module->space!=message->space) return LB_PROCESS_REFUSED;
    receiver->last_native_cost=1;
    size_t words=message->data.heap_frag?message->data.heap_frag->used_size:0,need;
    if(!lb_size_add(words,receiver->heap_reserved,&need) || need>LB_MAX_BINARY_BYTES/sizeof(Eterm)) return LB_PROCESS_LIMIT;
    size_t collections=receiver->collections;
    if(!lb_heap_reserve(receiver,need,live)) return LB_PROCESS_NO_MEMORY;
    if(receiver->collections!=collections) receiver->last_native_cost+=receiver->last_gc_cost;
    LbCodeSpace *space=message->space; LbFunLease *previous=receiver->fun_leases;
    LbAllocStatus held=lb_fun_retain_term(receiver,message->m[0],&receiver->last_native_cost);
    LbProcessStatus status=held==LB_ALLOC_NO_MEMORY?LB_PROCESS_NO_MEMORY:LB_PROCESS_REFUSED;
    if(held!=LB_ALLOC_OK) goto rollback_leases;
    Eterm *top=receiver->htop; LbOffHeap prepared={0};
    Eterm term=lb_copy_flat(message->m[0],&top,&prepared);
    status=LB_PROCESS_NO_MEMORY;
    if(is_non_value(term)) goto rollback_copy;
    if(top!=receiver->htop+words) abort();
    status=LB_PROCESS_LIMIT;
    if(prepared.overhead>UINT64_MAX-receiver->off_heap.overhead) goto rollback_copy;
    /* All fallible copying is complete. Chain ownership and the heap span
     * become visible together; no borrowed writer/temporary C alias survives. */
    if(prepared.first) {
        LbBinRef *tail=prepared.first;
        while(tail->next) { ++receiver->last_native_cost; tail=tail->next; }
        tail->next=receiver->off_heap.first; receiver->off_heap.first=prepared.first;
    }
    receiver->off_heap.overhead+=prepared.overhead;
    receiver->htop=top; receiver->last_native_cost+=words;
    *out=term; return LB_PROCESS_READY;
rollback_copy:
    lb_offheap_clear(&prepared);
rollback_leases:
    lb_fun_release_prefix(space,&receiver->fun_leases,previous); return status;
}
void lb_message_queue_init(LbMessageQueue *queue)
{
    queue->first=NULL; queue->last=queue->save=&queue->first; queue->mq_len=0;
}
LbProcessStatus lb_message_queue_append(LbMessageQueue *queue,LbMessage *message)
{
    if(!queue || !message || message->queue || message->next || !queue->last || *queue->last) return LB_PROCESS_INVALID;
    if(queue->mq_len==INTPTR_MAX) return LB_PROCESS_LIMIT;
    message->queue=queue;
    *queue->last=message; queue->last=&message->next; ++queue->mq_len;
    return LB_PROCESS_READY;
}
LbMessage *lb_message_queue_take(LbMessageQueue *queue)
{
    LbMessage *message=*queue->save;
    if(!message) return NULL;
    if(queue->mq_len<=0 || message->queue!=queue) abort();
    *queue->save=message->next;
    if(queue->last==&message->next) queue->last=queue->save;
    message->next=NULL; message->queue=NULL; --queue->mq_len; return message;
}
void lb_message_queue_clear(LbMessageQueue *queue)
{
    queue->save=&queue->first;
    LbMessage *message;
    while((message=lb_message_queue_take(queue))) lb_message_destroy(message);
    lb_message_queue_init(queue);
}

void lb_message_destroy(LbMessage *message)
{
    if(!message) return;
    if(message->queue) abort(); /* unlink and fix cursor/tail before freeing */
    LbCodeSpace *space=message->space;
    LbHeapFragment *fragment=message->data.heap_frag;
    if(fragment) {
        lb_offheap_clear(&fragment->off_heap);
        lb_release(space->domain,fragment);
    }
    lb_fun_release_roots(space,&message->leases);
    if(!space->messages) abort(); --space->messages;
    lb_release(space->domain,message);
}
LbProcessStatus lb_message_create(LbCodeSpace *space,Eterm term,LbMessage **out,Uint *cost)
{
    if(!out || !cost || !space) return LB_PROCESS_INVALID;
    *out=NULL; *cost=0;
    if(space->loading) return LB_PROCESS_REFUSED;
    if(space->messages==SIZE_MAX) return LB_PROCESS_LIMIT;
    size_t words,bytes; void *memory;
    if(!lb_flat_size(space->domain,term,&words)) return LB_PROCESS_NO_MEMORY;
    *cost=1+words;
    if(words>LB_MAX_BINARY_BYTES/sizeof(Eterm) ||
       !lb_size_mul(words,sizeof(Eterm),&bytes) ||
       !lb_size_add(offsetof(LbHeapFragment,mem),bytes,&bytes)) return LB_PROCESS_LIMIT;
    if(lb_alloc_domain_allocate(space->domain,sizeof(LbMessage),&memory)!=LB_ALLOC_OK) return LB_PROCESS_NO_MEMORY;
    LbMessage *message=memory; memset(message,0,sizeof(*message)); message->space=space;
    message->m[0]=message->m[1]=message->m[2]=NIL;
    /* Count only an internal prepared owner, never a mailbox publication.
     * The destructor rolls back every subsequently acquired prefix resource. */
    ++space->messages;
    LbAllocStatus held=lb_fun_retain_roots(space,&message->leases,term,cost);
    if(held!=LB_ALLOC_OK) {
        lb_message_destroy(message);
        return held==LB_ALLOC_NO_MEMORY?LB_PROCESS_NO_MEMORY:LB_PROCESS_REFUSED;
    }
    if(words) {
        if(lb_alloc_domain_allocate(space->domain,bytes,&memory)!=LB_ALLOC_OK) {
            lb_message_destroy(message); return LB_PROCESS_NO_MEMORY;
        }
        LbHeapFragment *fragment=memory;
        memset(fragment,0,offsetof(LbHeapFragment,mem)); fragment->alloc_size=words;
        message->data.heap_frag=fragment; Eterm *top=fragment->mem;
        Eterm copied=lb_copy_flat(term,&top,&fragment->off_heap);
        if(is_non_value(copied)) { lb_message_destroy(message); return LB_PROCESS_NO_MEMORY; }
        if(top!=fragment->mem+words) abort();
        fragment->used_size=words; message->m[0]=copied;
    } else message->m[0]=term;
    *out=message; return LB_PROCESS_READY;
}
