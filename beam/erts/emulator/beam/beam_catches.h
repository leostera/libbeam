/*
 * %CopyrightBegin%
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright Ericsson AB 2000-2025. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * %CopyrightEnd%
 */

#ifndef __BEAM_CATCHES_H
#define __BEAM_CATCHES_H

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif
#include "sys.h"
#include "code_ix.h"
#include "beam_code.h"

#define BEAM_CATCHES_NIL	(-1)

typedef struct ErtsCatchNamespace ErtsCatchNamespace;
ErtsCatchNamespace *erts_catch_namespace_create(void);
int erts_catch_namespace_check_staging(ErtsCatchNamespace *, ErtsCodeIndex, ErtsCodeIndex);
int erts_catch_namespace_start_staging(ErtsCatchNamespace *, ErtsCodeIndex, ErtsCodeIndex);
int erts_catch_namespace_end_staging(ErtsCatchNamespace *, ErtsCodeIndex);
unsigned erts_catch_namespace_cons(ErtsCatchNamespace *, ErtsCodeIndex, ErtsCodePtr, unsigned, ErtsCodePtr **);
ErtsCodePtr erts_catch_namespace_car(ErtsCatchNamespace *, ErtsCodeIndex, unsigned);
void erts_catch_namespace_delmod(ErtsCatchNamespace *, unsigned, const BeamCodeHeader *, unsigned, ErtsCodeIndex);
int erts_catch_namespace_can_discard(ErtsCatchNamespace *);
int erts_catch_namespace_discard(ErtsCatchNamespace *);
void beam_catches_init(ErtsCatchNamespace *);
unsigned beam_catches_cons(ErtsCodePtr cp, unsigned cdr, ErtsCodePtr **);
ErtsCodePtr beam_catches_car(unsigned i);
void beam_catches_delmod(unsigned head,
                         const BeamCodeHeader *hdr,
                         unsigned code_bytes,
                         ErtsCodeIndex code_ix);
ErtsCodePtr beam_catches_car_staging(unsigned i);
#define catch_pc(x)	beam_catches_car(catch_val((x)))

#endif	/* __BEAM_CATCHES_H */
