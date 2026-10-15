/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1997-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Selected erl_bits.h matching context representation.
 */
#ifndef LIBBEAM_CORE_BINARY_MATCH_H
#define LIBBEAM_CORE_BINARY_MATCH_H
#include "binary.h"
#include "process.h"
/* Native MATCH_CONTEXT is mutable without VOLATILE; writers use both bits. */
static inline int lb_is_match_context(const LbSubBits *sub)
{ return (sub->base_flags&LB_SUB_WRITABLE)==LB_SUB_MUTABLE; }
LbSubBits *lb_bs_start_match(LbProcess *,Eterm);
/* Compare an admitted, code-owned literal with the current match cursor.
 * Return 1 on match (and advance), 0 on mismatch (leave cursor unchanged),
 * -1 on an invalid cursor. The caller charges bounded work before comparing. */
int lb_bs_match_string(LbSubBits *,const unsigned char *literal,size_t bits);
#endif
