/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1996-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Owned register.c name/Process association boundary.
 */
#ifndef LIBBEAM_CORE_REGISTRY_H
#define LIBBEAM_CORE_REGISTRY_H
#include "process.h"
typedef struct LbRegistry LbRegistry;
typedef struct LbRegisteredProcess LbRegisteredProcess;
LbProcess *lb_registry_lookup(LbCodeSpace *,Eterm,Uint *cost);
LbProcessStatus lb_registry_register(LbProcess *,Eterm,Eterm);
void lb_registry_remove(LbProcess *);
#endif
