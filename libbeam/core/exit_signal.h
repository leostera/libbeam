/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef LIBBEAM_CORE_EXIT_SIGNAL_H
#define LIBBEAM_CORE_EXIT_SIGNAL_H
#include "process.h"
typedef enum { LB_EXIT_EXPLICIT, LB_EXIT_LINK } LbExitSignalKind;
/* Engine-serialized delivery. from is an identity, not a liveness assertion.
 * All storage and causal credit are acquired before receiver mutation. */
LbProcessStatus lb_exit_signal_send(LbProcess *,LbProcess *,Eterm from,Eterm reason,LbExitSignalKind);
/* Consume the first terminal signal at an interpreter safepoint. Its immutable
 * fragment owns both reason/code roots and the sender's causal budget. */
LbProcessStatus lb_exit_signal_receive(LbProcess *);
#endif
