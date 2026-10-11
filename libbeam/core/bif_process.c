/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1996-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#include "process_internal.h"
/* Identity is allocated on first observation, with no moving-heap allocation.
 * The directory is owned by the world/code namespace and keeps its generations
 * after the last process exits. Spawn must obtain identity before publication;
 * no spawn, signal or mailbox capability follows from self/0 alone. */
LbProcessStatus lb_process_identity(LbProcess *p,Eterm *out)
{
    if(!p || !out || (p->status!=LB_PROCESS_READY && p->status!=LB_PROCESS_YIELDED)) return LB_PROCESS_INVALID;
    p->last_native_cost=1;
    if(p->identity.owner) { *out=p->identity.id; return LB_PROCESS_READY; }
    LbCodeSpace *space=p->entry_module->space;
    LbPidTable *table=space->pids;
    if(!table) {
        LbPidStatus status=lb_pid_table_create(space,1024,&table);
        if(status!=LB_PID_OK) return status==LB_PID_NO_MEMORY?LB_PROCESS_NO_MEMORY:LB_PROCESS_INVALID;
        p->last_native_cost+=1024/16;
    }
    LbPidStatus status=lb_pid_insert(table,&p->identity,p);
    if(status!=LB_PID_OK) {
        if(!space->pids && lb_pid_table_destroy(table)!=LB_PID_OK) abort();
        return status==LB_PID_FULL?LB_PROCESS_LIMIT:LB_PROCESS_INVALID;
    }
    space->pids=table; *out=p->identity.id; return LB_PROCESS_READY;
}
Eterm lb_bif_self_0(LbProcess *p,Eterm *args,const BeamInstr *instruction)
{
    (void)args; (void)instruction;
    Eterm result;
    LbProcessStatus status=lb_process_identity(p,&result);
    if(status==LB_PROCESS_READY) return result;
    p->status=status; return THE_NON_VALUE;
}
