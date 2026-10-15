/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef LIBBEAM_CORE_TIMER_H
#define LIBBEAM_CORE_TIMER_H
#include "process_internal.h"
typedef struct LbTimer LbTimer;
Eterm lb_bif_send_after_3(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_start_timer_3(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_send_after_4(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_start_timer_4(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_cancel_timer_1(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_cancel_timer_2(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_read_timer_1(LbProcess *,Eterm *,const BeamInstr *);
void lb_timer_cancel_world(LbWorld *);
void lb_timer_poll_engine(LbEngine *);
int lb_timer_engine_pending(const LbEngine *);
#endif
