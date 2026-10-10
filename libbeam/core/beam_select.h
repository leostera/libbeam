/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1996-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef LIBBEAM_CORE_BEAM_SELECT_H
#define LIBBEAM_CORE_BEAM_SELECT_H
#include "beam_program.h"
typedef struct { unsigned opcode, r_mask; } LbBeamSelection;
/* beam_load.c's specific-instruction selection, for a transformation-complete
 * operation. Does not run/skip transformations, emit words, or validate the
 * selected instruction's operand signature. In particular, upstream's single
 * candidate path deliberately defers signature checks to emission. r_mask tells
 * the emitter which x(0) operands must become TAG_r without mutating the input.
 * An operation needing transformation must go through that engine first; this
 * primitive is not a public module-loading success boundary. */
LbBeamStatus lb_beam_select_specific(const LbBeamOp *, LbBeamSelection *);
#endif
