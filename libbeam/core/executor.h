/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef LIBBEAM_CORE_EXECUTOR_H
#define LIBBEAM_CORE_EXECUTOR_H
#include "process.h"
typedef struct LbTask LbTask;
typedef enum {
    LB_TASK_PENDING, LB_TASK_DONE, LB_TASK_EXCEPTION, LB_TASK_NO_MEMORY,
    LB_TASK_CANCELLED
} LbTaskStatus;
/* Internal asynchronous execution boundary, not the public Call transport.
 * One lazy shared worker per Engine. Tasks retain actual processes/code until
 * release; cancellation waits for a dispatch safepoint, then destroys the process.
 * Control calls remain serialized on the host. Raw code/heap/domain APIs on this
 * Engine must be protected by lb_engine_enter/leave once a worker exists. */
LbEngineStatus lb_task_create_binary(LbEngine *,const LbCodeEntry *,const void *,size_t,LbTask **);
LbTaskStatus lb_task_status(LbTask *);
/* Borrowed terminal roots, valid until cancellation/release; no raw host ABI. */
Eterm lb_task_result(LbTask *);
Eterm lb_task_exception(LbTask *);
void lb_task_cancel(LbTask *);
void lb_task_release(LbTask *);
/* Internal serialization for raw C consumers; not reentrant or a tenant API. */
void lb_engine_enter(LbEngine *);
void lb_engine_leave(LbEngine *);
#endif
