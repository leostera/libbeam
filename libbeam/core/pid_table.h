/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 2013-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef LIBBEAM_CORE_PID_TABLE_H
#define LIBBEAM_CORE_PID_TABLE_H
#include "alloc.h"
#include "term.h"
typedef struct LbPidTable LbPidTable;
typedef struct LbCodeSpace LbCodeSpace;
typedef struct LbProcess LbProcess;
typedef struct {
    LbPidTable *owner;
    LbProcess *process;
    Eterm id;
} LbPidEntry;
typedef enum { LB_PID_OK, LB_PID_INVALID, LB_PID_NO_MEMORY, LB_PID_FULL, LB_PID_BUSY } LbPidStatus;
/* Serialized native non-legacy PTAB free-ID ring. Entries are embedded in
 * nonmoving process allocations. Lookup borrows expire on removal/unlock.
 * PID terms, like atom terms, are namespace-relative, not owner capabilities.
 * Only terms originating in this table's world may be resolved here. */
LbPidStatus lb_pid_table_create(LbCodeSpace *,size_t capacity,LbPidTable **);
LbPidStatus lb_pid_table_destroy(LbPidTable *);
LbPidStatus lb_pid_insert(LbPidTable *,LbPidEntry *,LbProcess *);
LbPidStatus lb_pid_remove(LbPidTable *,LbPidEntry *);
LbProcess *lb_pid_lookup(LbPidTable *,Eterm);
int lb_pid_components(LbPidTable *,Eterm,Uint *number,Uint *serial);
int lb_pid_from_components(LbPidTable *,Uint number,Uint serial,Eterm *);
size_t lb_pid_count(const LbPidTable *);
size_t lb_pid_capacity(const LbPidTable *);
LbPidStatus lb_pid_make_list(const LbPidTable *,Eterm **heap,size_t count,Eterm *out);
#endif
