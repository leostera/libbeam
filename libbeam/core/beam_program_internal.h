/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef LIBBEAM_CORE_BEAM_PROGRAM_INTERNAL_H
#define LIBBEAM_CORE_BEAM_PROGRAM_INTERNAL_H
#include "beam_program.h"
#include "beam_reader.h"
#include "binary.h"
#include <string.h>
typedef union LbPrepBlock {
    struct { union LbPrepBlock *previous,*next; size_t bytes; } link;
    max_align_t alignment;
} LbPrepBlock;
typedef struct LbNamePatch { struct LbNamePatch *next; LbBeamBytes name; Eterm *destination; } LbNamePatch;
typedef struct LbNameBlock { struct LbNameBlock *next; size_t used,capacity; LbNamePatch patches[]; } LbNameBlock;
typedef struct LbDynamicLiteral { struct LbDynamicLiteral *next; Eterm value; Sint index; } LbDynamicLiteral;
typedef struct LbProcess LbProcess;
typedef enum { LB_ETF_TEMPORARY, LB_ETF_LITERAL } LbEtfStorage;
typedef struct LbMapLiteralPatch {
    struct LbMapLiteralPatch *next;
    Eterm *words;
    Eterm *term_slot;
    size_t size;
    LbEtfStorage storage;
} LbMapLiteralPatch;
typedef struct LbExternalFunPatch {
    struct LbExternalFunPatch *next;
    Eterm *fun_words;
    Eterm module, function, arity;
} LbExternalFunPatch;
typedef struct LbOpBlock { struct LbOpBlock *next; size_t capacity; LbBeamOp ops[]; } LbOpBlock;
typedef struct { uint32_t key; LbBeamArg default_value; } LbRecordField;
typedef struct { uint32_t name; int exported; size_t field_count; LbRecordField *fields; } LbRecordDefinition;
struct LbBeamProgram {
    LbAllocDomain *domain;
    LbAtomTable *atoms;
    LbBeamImage *image;
    LbPrepBlock *blocks;
    LbOpBlock *op_blocks;
    size_t op_block_capacity;
    LbOffHeap off_heap;
    size_t bytes, block_count;
    LbBeamError error;
    int retained;
    LbAtomTransaction *transaction,*atom_lookup;
    Eterm attributes, compile;
    Eterm *file_atoms;
    LbNamePatch *names;
    LbNameBlock *name_blocks;
    size_t name_block_capacity,name_count;
    Eterm *literals;
    size_t literal_count, dynamic_count;
    LbDynamicLiteral *dynamic;
    LbExternalFunPatch *external_funs;
    size_t external_fun_count;
    LbMapLiteralPatch *map_literals;
    LbBeamLambda *lambdas;
    size_t lambda_count;
    /* Versioned Recs metadata remains non-executable until definitions and
     * instance terms have owned representations. */
    size_t record_count,record_field_count;
    LbRecordDefinition *records;
    LbBeamType *types;
    size_t type_count;
    int type_fallback;
    LbBeamOp *ops, *free_ops;
    size_t op_count;
};
LbBeamStatus lb_program_begin(LbAllocDomain *, LbAtomTable *, const void *, size_t, LbBeamProgram **, LbBeamError *);
LbBeamStatus lb_program_begin_staged(LbAllocDomain *, LbAtomTable *, const void *, size_t, LbBeamProgram **, LbBeamError *);
LbBeamStatus lb_program_prepare_staged(LbBeamProgram *);
LbBeamStatus lb_program_decode_staged(LbBeamProgram *);
LbBeamStatus lb_program_bind_batch(LbBeamProgram *const *, size_t);
void lb_program_commit(LbBeamProgram *);
void *lb_palloc(LbBeamProgram *, size_t, size_t);
void lb_pfree(LbBeamProgram *, void *);
void lb_program_discard_ops(LbBeamProgram *, int);
void lb_program_discard_names(LbBeamProgram *);
int lb_program_allocate_op_block(LbBeamProgram *, LbBeamOp **);
int lb_pfail(LbBeamProgram *, LbBeamStatus);
int lb_pname(LbBeamProgram *, LbBeamBytes, Eterm *);
int lb_pliterals(LbBeamProgram *);
int lb_pvalidate_maps(LbBeamProgram *,LbProcess *meter);
int lb_pmodule_info(LbBeamProgram *);
int lb_pmetadata(LbBeamProgram *);
int lb_pdecode(LbBeamProgram *);
int lb_pinteger(LbBeamProgram *, const unsigned char *, size_t, int, LbEtfStorage, Eterm *);
/* Decode one versioned ETF value; reader.pos reports consumption, leaving any
 * trailing bytes for the caller. Temporary pointers are NOT literal-tagged.
 * Neither storage mode is executable until atom/map/export patches are bound.
 * Input name bytes are borrowed until atom preparation. Failure leaves an
 * unpublished partial arena, which must be discarded, never traversed as terms. */
int lb_pdecode_etf(LbBeamProgram *,LbBeamReader *,Eterm *,LbEtfStorage);
int lb_pdynamic(LbBeamProgram *, Eterm, Sint *);
#endif
