/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1996-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Native erl_message.h message-reference/heap-fragment representations.
 */
#ifndef LIBBEAM_CORE_MESSAGE_H
#define LIBBEAM_CORE_MESSAGE_H
#include "fun.h"
#include "process.h"
typedef struct LbHeapFragment {
    struct LbHeapFragment *next;
    LbOffHeap off_heap;
    Uint alloc_size,used_size;
    Eterm mem[];
} LbHeapFragment;
typedef struct LbMessage {
    struct LbMessage *next;
    union { LbHeapFragment *heap_frag; void *attached; } data;
    Eterm m[3]; /* message, sequence-trace token, sender */
    /* Owned sidecar after the native ErtsMessageRef prefix. */
    LbCodeSpace *space;
    LbFunLease *leases;
    struct LbMessageQueue *queue; /* exclusive published ownership, NULL while prepared */
} LbMessage;
_Static_assert(offsetof(LbMessage,space)==5*sizeof(Eterm),"native message reference prefix");
/* Serialized namespace operation on a valid owned term. Nothing is enqueued
 * until storage, offheap references and every embedded fun module are owned.
 * The immutable fragment outlives source GC/process destruction. */
LbProcessStatus lb_message_create(LbCodeSpace *,Eterm,LbMessage **,Uint *cost);
void lb_message_destroy(LbMessage *);
Eterm lb_erl_send(LbProcess *,Eterm destination,Eterm message);
/* Native inner-queue pointer-to-pointer cursor: unmatched messages stay in
 * arrival order; removal fixes the tail before freeing the selected node. */
typedef struct LbMessageQueue {
    LbMessage *first;
    LbMessage **last,**save;
    Sint mq_len;
} LbMessageQueue;
void lb_message_queue_init(LbMessageQueue *);
LbProcessStatus lb_message_queue_append(LbMessageQueue *,LbMessage *);
LbMessage *lb_message_queue_take(LbMessageQueue *);
void lb_message_queue_clear(LbMessageQueue *);
#endif
