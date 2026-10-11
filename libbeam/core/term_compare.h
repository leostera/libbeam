/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1996-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef LIBBEAM_CORE_TERM_COMPARE_H
#define LIBBEAM_CORE_TERM_COMPARE_H
#include "binary.h"
/* Valid owned terms from ONE namespace; not a validator for hostile raw words.
 * No GC/yield/mutation. Scratch allocation is fallible and always released.
 * On allocation refusal, equal is untouched; failure is never inequality. */
LbAllocStatus lb_term_equal(LbAllocDomain *,Eterm a,Eterm b,int *equal);
/* Valid borrowed views. Native bit comparison, including unaligned tails. */
int lb_bitstrings_equal(LbBitstringView a,LbBitstringView b);
#endif
