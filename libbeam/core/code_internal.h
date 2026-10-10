/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1996-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Explicit-owner adaptations of beam_load.h, emu/load.h, code_ix.h, export.h.
 */
#ifndef LIBBEAM_CORE_CODE_INTERNAL_H
#define LIBBEAM_CORE_CODE_INTERNAL_H
#include "code.h"
#include "engine_internal.h"
#include "beam_program_internal.h"
#include "beam_select.h"
#include "lb_atoms_generated.h"
#include "lb_bif_ids_generated.h"
#include <stdlib.h>
#define ASSERT assert
#define MAX_ARG 255
#define CP_SIZE 1
#define S_RESERVED 4
#define TE_OK 0
#define TE_FAIL (-1)
#define TE_SHORT_WINDOW (-2)
#define TE_UNSUPPORTED (-3)
#define TE_NO_MEMORY (-4)
#define TE_BAD_FORMAT (-5)
#define IS_SSMALL(v) ((Sint)(v)>=MIN_SMALL && (Sint)(v)<=MAX_SMALL)
#define make_loader_x_reg(r) (((Uint)(r)<<_TAG_IMMED1_SIZE)|_TAG_IMMED1_PID)
#define make_loader_y_reg(r) (((Uint)(r)<<_TAG_IMMED1_SIZE)|_TAG_IMMED1_PORT)
#define loader_tag(t) ((t)&_TAG_IMMED1_MASK)
#define LOADER_X_REG _TAG_IMMED1_PID
#define LOADER_Y_REG _TAG_IMMED1_PORT
#define loader_x_reg_index(t) ((t)>>_TAG_IMMED1_SIZE)
#define loader_y_reg_index(t) ((t)>>_TAG_IMMED1_SIZE)
typedef LbBeamOp BeamOp;
typedef LbBeamArg BeamOpArg;
typedef struct { Eterm module, function; Uint arity; } LbMFA;
typedef struct { BeamInstr op; void *breakpoint; LbMFA mfa; } LbCodeInfo;
typedef struct { const BeamInstr *addresses[3]; } LbDispatchable;
typedef struct LbExport {
    LbDispatchable dispatch;
    int bif_number, is_bif_traced;
    Eterm lambda;
    LbCodeInfo info;
    struct { BeamInstr op, address; } trampoline;
    LbCodeModule *owner;
    struct LbExport *next;
} Export;
typedef struct { Eterm module, function; Uint arity; } BeamFile_ImportEntry;
typedef struct { size_t pos; Sint offset; unsigned packed; } LabelPatch;
typedef struct { size_t value, count, capacity; int looprec_targeted; LabelPatch *patches; } Label;
typedef struct LbPatch { size_t pos; struct LbPatch *next; } LbPatch;
struct LbCodeSpace {
    LbEngine *engine;
    LbAllocDomain *domain; /* borrowed Engine allocation substrate */
    LbAtomTable *atoms;
    LbCodeModule *modules;
    Export *exports;
    Export natives[LB_NATIVE_COUNT];
    int loading;
};
struct LbCodeModule {
    LbCodeSpace *space;
    LbBeamProgram *program;
    LbCodeModule *next;
    Eterm name;
    BeamInstr *words;
    size_t word_count, users, importers;
    Export *exports;
    size_t export_count;
    Export **imports;
    LbCodeModule **dependencies;
    size_t import_count;
    unsigned char md5[16];
    Eterm attributes, compile;
    int published;
};
struct LbCodeEntry { LbCodeModule *module; Export *entry; };
typedef struct LoaderState_ {
    LbBeamProgram *program;
    LbCodeModule *module_code;
    LbCodeSpace *space;
    struct { struct { size_t count; BeamFile_ImportEntry *entries; } imports; } beam;
    const BifEntry **bif_imports;
    BeamOp *genop, *free_ops;
    Label *labels;
    size_t label_count;
    BeamInstr *codev;
    size_t ci, capacity, function_number, last_func_start, last_label;
    int specific_op;
    Eterm module, function;
    unsigned arity;
    LbPatch *literal_patches, *string_patches, *import_patches, *lambda_patches;
    LbCodeStatus status;
} LoaderState;
static inline void lb_release(LbAllocDomain *d,void *p) { if(p && lb_alloc_domain_release(d,p)!=LB_ALLOC_OK) abort(); }
BeamOp *lb_load_new_op(LoaderState *);
void lb_load_free_op(LoaderState *,BeamOp *);
Export *lb_export_find(LbCodeSpace *,Eterm,Eterm,unsigned);
int lb_transform(LoaderState *);
LbCodeStatus lb_emit(LoaderState *,BeamOp *);
LbCodeStatus lb_finish_emit(LoaderState *);
LbCodeStatus lb_compile_code(LbCodeModule *);
LbCodeStatus lb_verify_program(LoaderState *);
int lb_instruction_supported(unsigned);
Eterm lb_bif_module_info_1(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_module_info_2(LbProcess *,Eterm *,const BeamInstr *);
#endif
