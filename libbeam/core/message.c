/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1996-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Owned adaptation of native self-contained message heap fragments/copying.
 */
#include "message.h"
#include "process_internal.h"

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
