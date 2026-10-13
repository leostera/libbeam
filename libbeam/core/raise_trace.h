/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef LIBBEAM_CORE_RAISE_TRACE_H
#define LIBBEAM_CORE_RAISE_TRACE_H
#include "process.h"
typedef enum {
    LB_RAISE_TRACE_OK, LB_RAISE_TRACE_BADARG, LB_RAISE_TRACE_WORK_LIMIT,
    LB_RAISE_TRACE_MEMORY_LIMIT, LB_RAISE_TRACE_NO_MEMORY, LB_RAISE_TRACE_INVALID
} LbRaiseTraceStatus;
/* Prepare raise/3's native opaque trace from valid owned terms. Syntax errors
 * are BADARG results, not guest exceptions. No pending exception is published.
 * The input trace is rooted across GC; outputs are unchanged on any failure.
 * This component does NOT admit raise/3 or grant catch/unwind capability. */
LbRaiseTraceStatus lb_raise_trace_prepare(LbProcess *,Eterm class,Eterm trace,Eterm *out);
/* Only internally produced opaque traces may reach this boundary. The shape
 * check is defensive, not authority for guest-constructed opaque terms. */
typedef struct { Uint reason; Eterm symbolic; } LbRaisedTrace;
int lb_raise_trace_unpack(Eterm opaque,LbRaisedTrace *out);
#endif
