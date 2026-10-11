/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef LIBBEAM_CORE_RECEIVE_H
#define LIBBEAM_CORE_RECEIVE_H
#include "process_internal.h"
/* Engine-serialized monotonic receive timers; no callback retains actor pointers. */
int lb_receive_is_infinity(LbProcess *,Eterm);
LbProcessStatus lb_receive_timer_set(LbProcess *,Uint);
void lb_receive_timer_cancel(LbProcess *);
LbProcessStatus lb_receive_timer_poll(LbProcess *);
#endif
