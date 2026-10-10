/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef LIBBEAM_CORE_CODE_H
#define LIBBEAM_CORE_CODE_H
#include "beam_program.h"
typedef struct LbCodeSpace LbCodeSpace;
typedef struct LbCodeModule LbCodeModule;
typedef struct LbCodeEntry LbCodeEntry;
typedef enum { LB_CODE_OK, LB_CODE_INVALID, LB_CODE_FORMAT, LB_CODE_UNSUPPORTED,
               LB_CODE_NO_MEMORY, LB_CODE_LIMIT, LB_CODE_BUSY, LB_CODE_NOT_FOUND,
               LB_CODE_EXISTS, LB_CODE_UNRESOLVED } LbCodeStatus;
/* Serialized C ownership boundary. No implicit OTP services or lazy module load.
 * All functions/imports must pass the single admitted interpreter profile.
 * Load is atomic, including atom identities and namespace backing. No hot reload.
 * Native effects are the explicit builtin catalog, never a name-based fallback. */
LbCodeStatus lb_code_space_create(LbAllocDomain *, LbCodeSpace **);
LbCodeStatus lb_code_space_destroy(LbCodeSpace *);
LbAtomTable *lb_code_space_atoms(LbCodeSpace *); /* borrowed; no concurrent writer */
LbCodeStatus lb_code_load(LbCodeSpace *, const void *, size_t, LbCodeModule **, LbBeamError *);
LbCodeStatus lb_code_unload(LbCodeModule *); /* refuses frames/entries/importers */
LbCodeStatus lb_code_entry_acquire(LbCodeSpace *, Eterm module, Eterm function, unsigned arity, LbCodeEntry **);
void lb_code_entry_release(LbCodeEntry *);
/* Entry views retain the actual module, not a successfully allocated empty token. */
const Uint *lb_code_entry_address(const LbCodeEntry *);
const Uint *lb_code_module_words(const LbCodeModule *, size_t *);
Eterm lb_code_module_name(const LbCodeModule *);
size_t lb_code_module_function_count(const LbCodeModule *);
#endif
