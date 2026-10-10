/*
 * %CopyrightBegin%
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright Ericsson AB 1996-2025. All Rights Reserved.
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

/* Immutable canonical values and platform constants, owned by an Engine.
 * Atom names, export literals and persistent terms have separate owners.
 * Allocating a constant once does not make its backing process-lifetime storage.
 */

#ifndef __ERL_GLOBAL_LITERALS_H__
#define __ERL_GLOBAL_LITERALS_H__
#include "erl_embed.h"
typedef struct ErtsGlobalLiteralArena ErtsGlobalLiteralArena;
/* Independent unpublished storage. Paired allocate/register holds a lock.
 * Discard requires exclusive ownership and no escaped terms; bound arenas refuse. */
ErtsGlobalLiteralArena *erts_global_literal_arena_create(Uint initial_size);
Eterm *erts_global_literal_arena_allocate(ErtsGlobalLiteralArena *, Uint,
                                         struct erl_off_heap_header ***);
void erts_global_literal_arena_register(ErtsGlobalLiteralArena *, Eterm *);
Uint erts_global_literal_arena_bytes(ErtsGlobalLiteralArena *);
int erts_global_literal_arena_discard(ErtsGlobalLiteralArena *);

extern Eterm ERTS_GLOBAL_LIT_OS_TYPE;
extern Eterm ERTS_GLOBAL_LIT_OS_VERSION;
extern Eterm ERTS_GLOBAL_LIT_DFLAGS_RECORD;
extern Eterm ERTS_GLOBAL_LIT_ERL_FILE_SUFFIX;
extern Eterm ERTS_GLOBAL_LIT_EMPTY_TUPLE;
extern Eterm ERTS_GLOBAL_LIT_EMPTY_BINARY;

/* Initializes global literals. Note that the literals terms mentioned in the 
 * examples above may be created elsewhere, and are only kept here for clarity.
 */
void init_global_literals(ErtsEngine *);

/* Allocates space for global literals. Users must call erts_global_literal_register
 * when done creating the literal. 
 */
Eterm *erts_global_literal_allocate(Uint sz, struct erl_off_heap_header ***ohp);

/* Registers the pointed-to term as a global literal. Must be called for terms 
 * allocated using erts_global_literal_allocate.*/
void erts_global_literal_register(Eterm *variable);

/* Iterates between global literal areas. Can only be used when crash dumping. 
 * Iteration is started by passing NULL, then successively calling this function
 * until it returns NULL.
 */
ErtsLiteralArea *erts_global_literal_iterate_area(ErtsLiteralArea *prev);

#endif
