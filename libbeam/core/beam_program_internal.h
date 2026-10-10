/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef LIBBEAM_CORE_BEAM_PROGRAM_INTERNAL_H
#define LIBBEAM_CORE_BEAM_PROGRAM_INTERNAL_H
#include "beam_program.h"
#include "beam_reader.h"
#include "binary.h"
#include <string.h>
typedef union LbPrepBlock { struct { union LbPrepBlock *next; } link; max_align_t alignment; } LbPrepBlock;
typedef struct LbNamePatch { struct LbNamePatch *next; LbBeamBytes name; Eterm *destination; } LbNamePatch;
typedef struct LbDynamicLiteral { struct LbDynamicLiteral *next; Eterm value; Sint index; } LbDynamicLiteral;
typedef struct LbOpBlock { struct LbOpBlock *next; LbBeamOp ops[32]; } LbOpBlock;
struct LbBeamProgram {
    LbAllocDomain *domain;
    LbAtomTable *atoms;
    LbBeamImage *image;
    LbPrepBlock *blocks;
    LbOffHeap off_heap;
    size_t bytes;
    LbBeamError error;
    int retained;
    LbAtomTransaction *transaction;
    Eterm attributes, compile;
    Eterm *file_atoms;
    LbNamePatch *names;
    size_t name_count;
    Eterm *literals;
    size_t literal_count, dynamic_count;
    LbDynamicLiteral *dynamic;
    LbBeamLambda *lambdas;
    size_t lambda_count;
    LbBeamType *types;
    size_t type_count;
    int type_fallback;
    LbBeamOp *ops, *free_ops;
    size_t op_count;
};
LbBeamStatus lb_program_begin(LbAllocDomain *, LbAtomTable *, const void *, size_t, LbBeamProgram **, LbBeamError *);
void lb_program_commit(LbBeamProgram *);
void *lb_palloc(LbBeamProgram *, size_t, size_t);
int lb_pfail(LbBeamProgram *, LbBeamStatus);
int lb_pname(LbBeamProgram *, LbBeamBytes, Eterm *);
int lb_pliterals(LbBeamProgram *);
int lb_pmodule_info(LbBeamProgram *);
int lb_pmetadata(LbBeamProgram *);
int lb_pdecode(LbBeamProgram *);
int lb_pinteger(LbBeamProgram *, const unsigned char *, size_t, int, Eterm *);
int lb_pdynamic(LbBeamProgram *, Eterm, Sint *);
#endif
