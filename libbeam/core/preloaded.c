/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1996-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Ordinary compiler-produced pinned erlang.erl wrapper dependency.
 */
#include "code_internal.h"
#include "lb_preloaded_generated.inc"
LbCodeStatus lb_code_preload(LbCodeSpace *space)
{
    if(!space || space->preloading || space->modules) return LB_CODE_INVALID;
    LbCodeModule *module; LbBeamError error;
    space->preloading=1;
    LbCodeStatus status=lb_code_load(space,lb_preloaded_erlang,sizeof(lb_preloaded_erlang),&module,&error);
    space->preloading=0;
    return status;
}
