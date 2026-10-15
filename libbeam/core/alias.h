/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef LIBBEAM_CORE_ALIAS_H
#define LIBBEAM_CORE_ALIAS_H
#include "process.h"
typedef struct LbAlias {
    struct LbAlias *next;
    LbCodeSpace *space;
    LbProcess *owner; /* borrowed while space is open; inactive after process exit */
    uint32_t reference[3];
    int active,reply;
} LbAlias;
LbAlias *lb_alias_lookup(LbCodeSpace *,Eterm,Uint *work);
void lb_alias_deactivate_process(LbProcess *);
void lb_alias_deactivate(LbAlias *);
Eterm lb_bif_alias_0(LbProcess *,Eterm *,const Uint *);
Eterm lb_bif_alias_1(LbProcess *,Eterm *,const Uint *);
Eterm lb_bif_unalias_1(LbProcess *,Eterm *,const Uint *);
void lb_alias_space_destroy(LbCodeSpace *);
#endif
