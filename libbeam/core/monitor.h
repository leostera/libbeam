/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 2017-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Local serialized erl_monitor_link.h monitor-data representations.
 */
#ifndef LIBBEAM_CORE_MONITOR_H
#define LIBBEAM_CORE_MONITOR_H
#include "process.h"
#include "reference.h"
typedef struct LbMonitorNode LbMonitorNode;
struct LbMonitorNode {
    union {
        struct { void *next,*specific; Eterm tag; Sint mlenoffs; } signal;
        struct { Uint parent; LbMonitorNode *right,*left; } tree;
        struct { LbMonitorNode *next,*prev; } list;
    } node;
    union { Eterm item; void *ptr; } other;
    uint16_t offset,key_offset;
    uint32_t flags;
};
typedef struct LbMonitor {
    LbMonitorNode origin;
    union { LbMonitorNode target; Eterm ref_heap[LB_REF_WORDS]; } u;
    Eterm ref;
    uint32_t refc; /* Engine serialization replaces atomic half ownership */
    Eterm heap[LB_REF_WORDS];
    /* Owner pointers are borrowed only while their corresponding half links. */
    LbProcess *origin_process,*target_process;
    Eterm target_name; /* NIL or an immutable namespace atom; never a moving root */
    Eterm down_tag; /* immediate monitor signal tag, default am_DOWN */
} LbMonitor;
_Static_assert(sizeof(LbMonitorNode)==6*sizeof(Eterm),"native monitor node");
LbProcessStatus lb_monitor_prepare(LbProcess *,Eterm,LbMonitor **);
void lb_monitor_discard(LbMonitor *);
void lb_monitor_publish(LbMonitor *,LbProcess *);
int lb_monitor_demonitor(LbProcess *,Eterm,int flush);
void lb_monitor_exit(LbProcess *,int notify);
#endif
