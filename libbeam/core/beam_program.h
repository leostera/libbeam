/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 2020-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef LIBBEAM_CORE_BEAM_PROGRAM_H
#define LIBBEAM_CORE_BEAM_PROGRAM_H
#include "atoms.h"
#include "opcodes.h"
/* BeamOp/BeamOpArg shapes from beam_file.h. Generic operations are loader IR,
 * never an alternate executable bytecode format. All views borrow the program. */
typedef struct { int type; Sint val; } LbBeamArg;
typedef struct LbBeamOp {
    struct LbBeamOp *next;
    unsigned op, arity;
    size_t offset;
    LbBeamArg *a;
    LbBeamArg def_args[8];
} LbBeamOp;
typedef struct LbBeamProgram LbBeamProgram;
typedef struct { Eterm function; uint32_t arity, label, index, num_free, old_uniq; } LbBeamLambda;
typedef struct { uint16_t types, flags, unit; int64_t min, max; } LbBeamType;
typedef struct { LbBeamStatus status; size_t offset; const char *stage; } LbBeamError;
/* Copies input; all parsing/term construction precedes a SINGLE atomic atom
 * admission. Failure leaves namespace contents/backing unchanged. Serialized;
 * domain/table live until destroy. Bounded 64 MiB preparation arena, not quota.
 * This does NOT transform/emit/link/publish executable code or validate execution
 * safety. ETF/native-layout subset is explicitly documented; unsupported terms
 * fail before namespace admission. No external function/handle authority. */
LbBeamStatus lb_beam_program_prepare(LbAllocDomain *, LbAtomTable *, const void *, size_t,
                                     LbBeamProgram **, LbBeamError *);
void lb_beam_program_destroy(LbBeamProgram *);
const LbBeamOp *lb_beam_program_ops(const LbBeamProgram *);
const LbBeamImage *lb_beam_program_image(const LbBeamProgram *);
LbBeamStatus lb_beam_program_literal(const LbBeamProgram *, const LbAtomTable *, Sint, Eterm *);
size_t lb_beam_program_literal_count(const LbBeamProgram *);
size_t lb_beam_program_dynamic_literal_count(const LbBeamProgram *);
size_t lb_beam_program_lambda_count(const LbBeamProgram *);
LbBeamStatus lb_beam_program_lambda(const LbBeamProgram *, const LbAtomTable *, size_t, LbBeamLambda *);
LbBeamStatus lb_beam_program_type(const LbBeamProgram *, size_t, LbBeamType *);
int lb_beam_program_type_fallback(const LbBeamProgram *);
#endif
