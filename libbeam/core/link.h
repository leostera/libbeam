/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef LIBBEAM_CORE_LINK_H
#define LIBBEAM_CORE_LINK_H
#include "process.h"
typedef struct LbLink LbLink;
typedef struct LbLinkHalf LbLinkHalf;
/* Caller establishes absence of this pair before preparation (a fresh spawned
 * target has no links). Publication requires distinct live namespace identities
 * and cannot allocate, fail or execute the child. */
LbProcessStatus lb_link_prepare(LbProcess *,LbLink **);
void lb_link_publish(LbLink *,LbProcess *);
void lb_link_discard(LbLink *);
void lb_link_exit(LbProcess *,int notify);
LbProcessStatus lb_link_count(LbProcess *,LbProcess *,size_t *);
/* Caller reserved/precharged all list storage. No allocation or collection. */
Eterm lb_link_list(LbProcess *,Eterm **);
#endif
