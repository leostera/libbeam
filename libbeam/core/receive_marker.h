/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 2017-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Owned erl_message.h marker block / erl_proc_sig_queue.h boundaries.
 */
#ifndef LIBBEAM_CORE_RECEIVE_MARKER_H
#define LIBBEAM_CORE_RECEIVE_MARKER_H
#include "message.h"
#include "reference.h"
#define LB_RECV_MARKERS 8
typedef struct {
    Eterm signal[5]; /* native ErtsSignal union/message-reference prefix */
    LbMessage **prev_next;
    signed char mark_type,pass,set_save,in_prioq,in_sigq,in_msgq,prev_ix,next_ix;
#ifdef DEBUG
    signed char used;
    LbProcess *proc;
#endif
} LbRecvMarker;
_Static_assert(offsetof(LbRecvMarker,prev_next)==5*sizeof(Eterm),"native marker signal prefix");
typedef struct LbRecvMarkerBlock {
    Eterm ref[LB_RECV_MARKERS];
    LbRecvMarker marker[LB_RECV_MARKERS];
    signed char free_ix,used_ix,unused,pending_set_save_ix,set_save_ix;
    /* Stable ordinary references do not borrow moving process-heap aliases.
     * Serial tail anchors replace non-message insertion/lock interleavings. */
    Eterm reference_heap[LB_RECV_MARKERS][LB_REF_WORDS];
    Uint sequence;
} LbRecvMarkerBlock;
LbProcessStatus lb_receive_marker_reserve(LbProcess *,Eterm *);
void lb_receive_marker_bind(LbProcess *,Eterm,Eterm);
void lb_receive_marker_clear(LbProcess *,Eterm);
void lb_receive_marker_use(LbProcess *,Eterm);
void lb_receive_marker_unlink(LbMessageQueue *,LbMessage *,LbMessage **);
void lb_receive_marker_destroy(LbProcess *);
#endif
