/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1996-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Metadata layouts adapted from erl_vm.h and beam_load.h.
 */
#ifndef LIBBEAM_CORE_OPCODES_H
#define LIBBEAM_CORE_OPCODES_H
#include "term.h"
#ifndef ARCH_64
#define ARCH_64 1
#endif
#define gen_opc lb_gen_opc
#define opc lb_specific_opc
#define tag_to_letter lb_tag_to_letter
#include "lb_opcodes_generated.h"
typedef struct { const char *name; uint32_t mask[3]; unsigned involves_r; int sz, adjust; const char *pack, *sign; } OpEntry;
typedef struct { const char *name; int arity, specific, num_specific, transform; } GenOpEntry;
extern const OpEntry opc[NUM_SPECIFIC_OPS];
extern const GenOpEntry gen_opc[NUM_GENERIC_OPS];
#endif
