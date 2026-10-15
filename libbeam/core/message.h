/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1996-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Native erl_message.h message-reference/heap-fragment representations.
 */
#ifndef LIBBEAM_CORE_MESSAGE_H
#define LIBBEAM_CORE_MESSAGE_H
#include "fun.h"
#include "process.h"
#include "work_budget.h"
struct LbAlias;
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
    LbWorkBudget *budget; /* owned sender-causal lease, independent of moving terms */
    LbFunLease *leases;
    struct LbAlias *reply_alias; /* reply alias deactivated when this message is consumed */
    struct LbMessageQueue *queue; /* exclusive published ownership, NULL while prepared */
} LbMessage;
_Static_assert(offsetof(LbMessage,space)==5*sizeof(Eterm),"native message reference prefix");
/* Serialized namespace operation on a valid owned term. Nothing is enqueued
 * until storage, offheap references and every embedded fun module are owned.
 * The immutable fragment outlives source GC/process destruction. */
LbProcessStatus lb_message_create(LbCodeSpace *,Eterm,LbMessage **,Uint *cost);
void lb_message_destroy(LbMessage *);
Eterm lb_erl_send(LbProcess *,Eterm destination,Eterm message);
/* Engine-serialized publication after delivery work is charged. Ownership
 * transfers only on success; this never interprets guest code. */
LbProcessStatus lb_message_deliver(LbProcess *,LbMessage *);
/* Prepare an actor-heap message view without freeing its immutable source.
 * live is the proven X-root span at the receiving instruction. No destination
 * or offheap chain is published until copying and code retention complete. */
LbProcessStatus lb_message_import(LbProcess *,const LbMessage *,size_t live,Eterm *out);
/* Selective-receive cursors, serialized by the owning Engine. An empty peek
 * returns READY/THE_NON_VALUE; infrastructure failure never means no message. */
LbProcessStatus lb_message_peek_owned(LbProcess *,Eterm *out);
void lb_message_skip_owned(LbProcess *);
void lb_message_consume_owned(LbProcess *);
/* Native inner-queue pointer-to-pointer cursor: unmatched messages stay in
 * arrival order; removal fixes the tail before freeing the selected node. */
typedef struct LbRecvMarkerBlock LbRecvMarkerBlock;
typedef struct LbMessageQueue {
    LbMessage *first;
    LbMessage **last,**save;
    Sint mq_len;
    LbRecvMarkerBlock *markers; /* actor-owned block; borrowed tail anchors */
} LbMessageQueue;
void lb_message_queue_init(LbMessageQueue *);
LbProcessStatus lb_message_queue_append(LbMessageQueue *,LbMessage *);
LbMessage *lb_message_queue_take(LbMessageQueue *);
void lb_message_queue_clear(LbMessageQueue *);
#endif
