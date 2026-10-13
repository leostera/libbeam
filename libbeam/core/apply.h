/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef LIBBEAM_CORE_APPLY_H
#define LIBBEAM_CORE_APPLY_H
#include "process_internal.h"
/* Internal native control-transfer boundaries, not BIF value callbacks.
 * Success installs argument registers and retains the target's code until GC
 * proves neither its terms, current PC nor continuations need it. NULL carries
 * a guest freason or an infrastructure process status. No autoload/error-handler
 * capability: missing exports are explicit profile refusals.
 * List apply never collects. Fixed apply may collect to build badarg arguments;
 * callers must swap heap/stack state out and reload it after either helper. */
const Export *lb_apply(LbProcess *);
const Export *lb_fixed_apply(LbProcess *,unsigned arity);
#endif
