/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef LIBBEAM_CORE_CODE_H
#define LIBBEAM_CORE_CODE_H
#include "beam_program.h"
#include "load_report.h"
#include "engine.h"
typedef struct LbCodeSpace LbCodeSpace;
typedef struct LbCodeModule LbCodeModule;
typedef struct LbCodeEntry LbCodeEntry;
typedef struct { const unsigned char *data; size_t size; } LbCodeInput;
/* The host supplies bytes for a missing record-definition module. Bytes remain
 * borrowed until the synchronous load finishes. Returning {NULL,0} means no
 * module is available; the guest then receives badrecord. */
typedef LbCodeInput (*LbRecordModuleResolver)(void *context, Eterm module);
#define LB_CODE_BATCH_MODULE_LIMIT 4096u
#define LB_CODE_BATCH_BYTES_LIMIT (64u * 1024u * 1024u)
typedef enum { LB_CODE_OK, LB_CODE_INVALID, LB_CODE_FORMAT, LB_CODE_UNSUPPORTED,
               LB_CODE_NO_MEMORY, LB_CODE_LIMIT, LB_CODE_BUSY, LB_CODE_NOT_FOUND,
               LB_CODE_EXISTS, LB_CODE_UNRESOLVED, LB_CODE_CLOSED } LbCodeStatus;
typedef enum { LB_CODE_ON_LOAD_REJECT, LB_CODE_ON_LOAD_DEFER_DENIED_NIF } LbCodeOnLoadPolicy;
/* Serialized C ownership boundary. No implicit OTP services or filesystem load.
 * All functions/imports must pass the single admitted interpreter profile.
 * Load is atomic, including atom identities and namespace backing. No hot reload.
 * Native effects are the explicit builtin catalog, never a name-based fallback.
 * With an asynchronous Engine worker, host raw C accesses require the Engine
 * control borrow (executor.h); this includes final space destruction. */
/* A space retains its Engine through physical destruction. */
LbCodeStatus lb_code_space_create(LbEngine *, LbCodeSpace **);
LbCodeStatus lb_code_space_destroy(LbCodeSpace *);
LbAtomTable *lb_code_space_atoms(LbCodeSpace *); /* borrowed; no concurrent writer */
/* Optional, explicit host resolution for OTP external native-record creation.
 * Register before running processes; callback must not reenter this code space. */
LbCodeStatus lb_code_space_set_record_resolver(LbCodeSpace *, LbRecordModuleResolver, void *context);
LbCodeStatus lb_code_load_report(LbCodeSpace *, const void *, size_t, LbCodeModule **, LbBeamError *, LbLoadReport *);
LbCodeStatus lb_code_load(LbCodeSpace *, const void *, size_t, LbCodeModule **, LbBeamError *);
/* At most 4096 modules and 64 MiB aggregate encoded input per group. Atom
 * identities and all code exports become visible in one no-fail commit. Inputs
 * are copied into the world-owned preparation arena. Dependencies may
 * refer forward or form cycles within this exact-name batch. */
LbCodeStatus lb_code_load_batch(LbCodeSpace *, const LbCodeInput *, size_t, LbBeamError *, LbLoadReport *);
LbCodeStatus lb_code_load_batch_with_policy(LbCodeSpace *, const LbCodeInput *, size_t,
                                            LbCodeOnLoadPolicy, LbBeamError *, LbLoadReport *);
LbCodeStatus lb_code_unload(LbCodeModule *); /* refuses frames/entries/importers */
/* Whole-space retirement handles mutually importing module groups atomically. */
LbCodeStatus lb_code_unload_all(LbCodeSpace *);
LbCodeStatus lb_code_entry_acquire(LbCodeSpace *, Eterm module, Eterm function, unsigned arity, LbCodeEntry **);
void lb_code_entry_release(LbCodeEntry *);
/* Entry views retain the actual module, not a successfully allocated empty token. */
const Uint *lb_code_entry_address(const LbCodeEntry *);
const Uint *lb_code_module_words(const LbCodeModule *, size_t *);
Eterm lb_code_module_name(const LbCodeModule *);
size_t lb_code_module_function_count(const LbCodeModule *);
#endif
