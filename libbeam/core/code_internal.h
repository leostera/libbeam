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
typedef struct LbPatch { size_t pos,pc; struct LbPatch *next; } LbPatch;
typedef struct LbLiteralFunUse {
    size_t pc;
    const struct LbFunEntry *entry;
    const Export *external;
    struct LbLiteralFunUse *next;
} LbLiteralFunUse;
typedef struct LbStringPatch { size_t pos,bytes; struct LbStringPatch *next; } LbStringPatch;
typedef struct LbPidTable LbPidTable;
typedef struct LbRegistry LbRegistry;
struct LbCodeSpace {
    LbEngine *engine;
    LbAllocDomain *domain; /* borrowed Engine allocation substrate */
    LbAtomTable *atoms;
    Eterm node_name; /* Unnamed, non-distributed namespace; no rename/distribution API. */
    LbPidTable *pids; /* lazy identity directory; retained empty to prevent ID reuse */
    int preloading; /* internal protected-module bootstrap only */
    LbRegistry *registry; /* removed on logical process exit, never a moving root */
    LbCodeModule *modules;
    Export *exports;
    Export natives[LB_NATIVE_COUNT];
    Export apply_export; /* erlang:apply/3 is an instruction, not a value BIF */
    size_t messages; /* owned prepared/queued fragments pin atoms and namespace */
    int loading,has_fun_literals;
};
struct LbFunEntry;
struct LbCodeModule {
    LbCodeSpace *space;
    LbBeamProgram *program;
    LbCodeModule *next;
    Eterm name;
    BeamInstr *words;
    size_t word_count, users, importers, pending_importers;
    Export *exports;
    size_t export_count;
    Export **imports;
    LbCodeModule **dependencies;
    size_t import_count, external_fun_count, dependency_count;
    unsigned char md5[16];
    struct LbFunEntry *funs;
    size_t fun_count;
    LbLiteralFunUse *literal_fun_uses;
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
    Sint *lambda_literals;
    BeamOp *genop, *free_ops;
    Label *labels;
    size_t label_count;
    BeamInstr *codev;
    size_t ci, capacity, function_number, last_func_start, last_label,emitting_pc;
    int specific_op;
    Eterm module, function;
    unsigned arity;
    LbPatch *literal_patches, *import_patches, *lambda_patches;
    LbStringPatch *string_patches;
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
LbCodeStatus lb_code_prepare_module(LbCodeModule *,LoaderState *);
LbCodeStatus lb_code_resolve_imports(LoaderState *,LbCodeModule *const *,size_t);
LbCodeStatus lb_code_compile_prepared(LoaderState *);
LbCodeStatus lb_code_space_create_in_domain(LbEngine *,LbAllocDomain *,LbCodeSpace **);
LbCodeStatus lb_verify_program(LoaderState *);
int lb_instruction_supported(unsigned);
Eterm lb_bif_floor_1(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_ceil_1(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_trunc_1(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_round_1(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_float_1(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_list_to_tuple_1(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_tuple_to_list_1(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_make_tuple_2(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_error_2(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_exit_1(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_get_1(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_put_2(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_erase_1(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_node_0(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_nodes_0(LbProcess *,Eterm *,const BeamInstr *);
#define LB_DENIAL(id, name, arity, category) Eterm lb_bif_deny_##name##_##arity(LbProcess *,Eterm *,const BeamInstr *);
#include "capability_denials.def"
#undef LB_DENIAL
Eterm lb_bif_module_loaded_1(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_load_module_2(LbProcess *,Eterm *,const BeamInstr *);
#define LB_TYPE_PREDICATE(name, predicate) Eterm lb_bif_##name##_1(LbProcess *,Eterm *,const BeamInstr *);
#include "type_predicates.def"
#undef LB_TYPE_PREDICATE
Eterm lb_bif_tuple_size_1(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_binary_to_term_1(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_process_info_2(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_function_exported_3(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_module_info_1(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_module_info_2(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_mixed_plus(LbProcess *,Eterm,Eterm);
Eterm lb_mixed_times(LbProcess *,Eterm,Eterm);
Eterm lb_bif_times_2(LbProcess *,Eterm *,const BeamInstr *);
int lb_int_div_rem(LbProcess *,Eterm,Eterm,Eterm *,Eterm *);
Eterm lb_bif_intdiv_2(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_rem_2(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_abs_1(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_unary_minus(LbProcess *,Eterm);
Eterm lb_bif_minus_1(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_plus_2(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_minus_2(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_bsr_2(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_band_2(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_bsl_2(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_binary_to_integer_1(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_list_to_integer_1(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_binary_to_float_1(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_binary_to_atom_2(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_binary_to_existing_atom_2(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_binary_to_atom_1(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_binary_to_existing_atom_1(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_plusplus_2(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_atom_to_binary_2(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_atom_to_list_1(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_atom_to_binary_1(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_integer_to_binary_2(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_bor_2(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_bxor_2(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_bnot_1(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_float_to_binary_2(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_minusminus_2(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_split_binary_2(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_seqeq_2(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_sneqeq_2(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_element_2(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_hd_1(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_tl_1(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_length_1(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_sge_2(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_sgt_2(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_sle_2(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_slt_2(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_seq_2(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_sneq_2(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_map_get_2(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_is_map_key_2(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_map_size_1(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_raise_3(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_max_2(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_min_2(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_is_integer_1(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_is_list_1(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_not_1(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_is_atom_1(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_is_binary_1(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_setelement_3(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_integer_to_binary_1(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_self_0(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_make_ref_0(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_spawn_3(LbProcess *,Eterm *,const BeamInstr *);
LbCodeStatus lb_code_preload(LbCodeSpace *);
Eterm lb_bif_error_1(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_error_3(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_register_2(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_whereis_1(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_unregister_1(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_spawn_opt_4(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_demonitor_2(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_exit_2(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_iolist_to_binary_1(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_byte_size_1(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_binary_part_3(LbProcess *,Eterm *,const BeamInstr *);
Eterm lb_bif_throw_1(LbProcess *,Eterm *,const BeamInstr *);
#endif
