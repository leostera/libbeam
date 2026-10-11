/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1999-2025. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef LIBBEAM_CORE_BINARY_ERROR_H
#define LIBBEAM_CORE_BINARY_ERROR_H
#include "alloc.h"
#include "term.h"
typedef struct LbProcess LbProcess;
/* Native BSC_* descriptor identities; these are not execution permissions. */
typedef enum {
    LB_BSC_APPEND=0, LB_BSC_PRIVATE_APPEND=1, LB_BSC_BINARY=2,
    LB_BSC_BINARY_FIXED_SIZE=3, LB_BSC_BINARY_ALL=4, LB_BSC_FLOAT=5,
    LB_BSC_FLOAT_FIXED_SIZE=6, LB_BSC_INTEGER=7, LB_BSC_INTEGER_FIXED_SIZE=8,
    LB_BSC_STRING=9, LB_BSC_UTF8=10, LB_BSC_UTF16=11, LB_BSC_UTF32=12
} LbBinarySegmentKind;
/* Construct the native #{cause => {Segment,Op,Info,Value},
 * function => format_bs_fail, module => erl_erts_errors} payload only.
 * No exception flags/reason, stacktrace, catch handling or opcode admission.
 * Info is an owned atom; Value belongs to the process or its retained literals.
 * Output is a stable C/control slot, not moving heap storage. Caller roots the
 * result before collection. Failure leaves output, heap and old roots unchanged;
 * success preserves heap credit and may move X/Y/exception roots. */
LbAllocStatus lb_binary_error_payload(LbProcess *,LbBinarySegmentKind,Uint segment,
                                     Eterm info,Eterm value,Eterm *out);
#endif
