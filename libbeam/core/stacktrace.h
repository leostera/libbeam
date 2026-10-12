/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1996-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef LIBBEAM_CORE_STACKTRACE_H
#define LIBBEAM_CORE_STACKTRACE_H
#include "process.h"
#include "engine_internal.h"
/* Immediate native uncaught-error trace materialization, not catch/unwind.
 * Code pointers/MFAs are borrowed only during serialized capture and building.
 */
void lb_trace_callsite(LbProcess *,const BeamInstr *from,const BeamInstr *to);
LbProcessStatus lb_error_down_reason(LbProcess *,Eterm *out);
#endif
