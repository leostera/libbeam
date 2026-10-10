/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "sys.h"
#include "erl_vm.h"
#include "global.h"
#include "erl_isolate_state.h"
#include "erl_engine.h"
#include "erl_code_table.h"
#include "erl_record.h"
#include "erl_fun.h"
#include "beam_catches.h"
#include "beam_ranges.h"
#include "erl_module_namespace.h"
#include <stdlib.h>

struct ErtsIsolateNamespaceState {
    ErtsEngine *engine;
    ErtsIsolateNamespaceState *engine_prev, *engine_next;
    ErtsRegistry *registry;
    ErtsPersistentTermState *persistent;
    erts_atomic_t borrowers;
    ErtsAtomNamespace *atoms;
    ErtsModuleTable *modules[ERTS_NUM_CODE_IX];
    ErtsExportLiterals *export_literals;
    ErtsExportNamespace *exports;
    ErtsCodeTable *funs, *records;
    ErtsCatchNamespace *catches;
    ErtsRangeNamespace *ranges;
    ErtsModuleNamespace *module_state;
    ErtsCodeSpace *code_space;
    int bound; /* Diagnostic execution references prevent unpublished disposal. */
};

static ErtsIsolateNamespaceState *create_state(ErtsEngine *engine,
                                              int atom_limit, int module_limit,
                                              int export_limit)
{
    ErtsIsolateNamespaceState *state;
    int i;
    if (!engine || engine->namespace_admission_closed ||
        module_limit <= 0 || export_limit <= 0)
        return NULL;
    state = calloc(1, sizeof(*state));
    if (!state)
        return NULL;
    state->engine = engine;
    erts_atomic_init_nob(&state->borrowers, 0);
    state->atoms = erts_atom_namespace_create(atom_limit);
    if (!state->atoms) {
        free(state);
        return NULL;
    }
    state->export_literals = erts_export_literals_create();
    state->exports = erts_export_namespace_create(state->export_literals, export_limit);
    ASSERT(state->exports);
    state->funs = erts_fun_namespace_create(0);
    state->records = erts_record_namespace_create(0);
    state->catches = erts_catch_namespace_create();
    state->ranges = erts_range_namespace_create();
    for (i = 0; i < ERTS_NUM_CODE_IX; ++i) {
        state->modules[i] = erts_module_table_create(module_limit);
        ASSERT(state->modules[i]); /* validated limit; ERTS allocation is fatal */
    }
    state->module_state = erts_module_namespace_create(state->modules);
    state->code_space = erts_code_space_create(state);
    state->registry = erts_registry_create(state);
    state->persistent = erts_persistent_state_create(state);
    state->engine_next = engine->namespace_states;
    if (state->engine_next) state->engine_next->engine_prev = state;
    engine->namespace_states = state;
    engine->namespace_count++;
    return state;
}

ErtsIsolateNamespaceState *erts_isolate_namespace_create(ErtsEngine *engine,
                                                       int atom_limit,
                                                       int module_limit)
{
    enum ErlRuntimeStartupPhase phase;
    if (!engine)
        return NULL;
    phase = erl_runtime_startup_phase(engine);
    if (phase != ERL_RUNTIME_PREPARED && phase != ERL_RUNTIME_THREADS_STARTED)
        return NULL;
    return create_state(engine, atom_limit, module_limit,
                         erts_export_namespace_default_limit());
}

ErtsIsolateNamespaceState *erts_isolate_namespace_create_diagnostic(
    ErtsEngine *engine, int atom_limit, int module_limit, int export_limit)
{
    ErtsIsolateNamespaceState *state;
    if (!engine || erl_runtime_startup_phase(engine) != ERL_RUNTIME_PREPARING ||
        engine->diagnostic_namespace)
        return NULL;
    state = create_state(engine, atom_limit,
                         module_limit > 0 ? module_limit : erts_module_table_default_limit(),
                         export_limit > 0 ? export_limit : erts_export_namespace_default_limit());
    if (state) {
        ASSERT(!engine->diagnostic_namespace);
        state->bound = 1;
        engine->diagnostic_namespace = state;
    }
    return state;
}

size_t erts_engine_namespace_count(const ErtsEngine *engine)
{
    return engine ? engine->namespace_count : 0;
}

void erts_engine_close_namespace_admission(ErtsEngine *engine)
{
    ASSERT(engine);
    engine->namespace_admission_closed = 1;
}

ErtsEngine *erts_isolate_namespace_engine(ErtsIsolateNamespaceState *state)
{
    return state ? state->engine : NULL;
}

ErtsPersistentTermState *erts_isolate_namespace_persistent(ErtsIsolateNamespaceState *state)
{
    return state ? state->persistent : NULL;
}
ErtsRegistry *erts_isolate_namespace_registry(ErtsIsolateNamespaceState *state)
{
    return state ? state->registry : NULL;
}

ErtsCodeSpace *erts_isolate_namespace_code_space(ErtsIsolateNamespaceState *state) { return state->code_space; }
ErtsModuleNamespace *erts_isolate_namespace_module_state(ErtsIsolateNamespaceState *state) { return state->module_state; }
ErtsRangeNamespace *erts_isolate_namespace_ranges(ErtsIsolateNamespaceState *state) { return state->ranges; }
ErtsCatchNamespace *erts_isolate_namespace_catches(ErtsIsolateNamespaceState *state) { return state->catches; }
ErtsCodeTable *erts_isolate_namespace_funs(ErtsIsolateNamespaceState *state) { return state->funs; }
ErtsCodeTable *erts_isolate_namespace_records(ErtsIsolateNamespaceState *state) { return state->records; }

ErtsExportNamespace *erts_isolate_namespace_exports(ErtsIsolateNamespaceState *state)
{
    return state->exports;
}

ErtsExportLiterals *erts_isolate_namespace_export_literals(ErtsIsolateNamespaceState *state)
{
    return state->export_literals;
}

ErtsAtomNamespace *erts_isolate_namespace_atoms(ErtsIsolateNamespaceState *state)
{
    return state->atoms;
}

ErtsModuleTable *erts_isolate_namespace_module_at(ErtsIsolateNamespaceState *state,
                                                unsigned index)
{
    return index < ERTS_NUM_CODE_IX ? state->modules[index] : NULL;
}

ErtsModuleTable *erts_isolate_namespace_modules(ErtsIsolateNamespaceState *state)
{
    return state->modules[0];
}

ErtsModuleTable **erts_isolate_namespace_module_slots(ErtsIsolateNamespaceState *state)
{
    return state->modules;
}

void erts_isolate_namespace_acquire(ErtsIsolateNamespaceState *state)
{
    ASSERT(state);
    erts_atomic_inc_nob(&state->borrowers);
}

void erts_isolate_namespace_release(ErtsIsolateNamespaceState *state)
{
    erts_aint_t remaining;
    ASSERT(state);
    remaining = erts_atomic_dec_read_relb(&state->borrowers);
    ASSERT(remaining >= 0);
    (void) remaining;
}

erts_aint_t erts_isolate_namespace_borrowers(ErtsIsolateNamespaceState *state)
{
    return erts_atomic_read_acqb(&state->borrowers);
}

int erts_isolate_namespace_discard(ErtsIsolateNamespaceState *state)
{
    int i;
    if (!state || state->bound || erts_isolate_namespace_borrowers(state) ||
        !erts_export_literals_can_discard(state->export_literals) ||
        !erts_export_namespace_can_discard(state->exports) ||
        !erts_code_table_can_discard(state->funs) ||
        !erts_code_table_can_discard(state->records) ||
        !erts_catch_namespace_can_discard(state->catches) ||
        !erts_range_namespace_can_discard(state->ranges) ||
        !erts_module_namespace_can_discard(state->module_state) ||
        !erts_code_space_can_discard(state->code_space) ||
        !erts_registry_can_discard(state->registry) ||
        !erts_persistent_state_can_discard(state->persistent))
        return 1;
    /* Preflight ALL slots before freeing any of them. No partial destruction
     * if a later slot retains code or metadata references. */
    for (i = 0; i < ERTS_NUM_CODE_IX; ++i)
        if (!erts_module_table_can_discard_unpublished(state->modules[i]))
            return 1;
    for (i = 0; i < ERTS_NUM_CODE_IX; ++i) {
        int result = erts_module_table_discard_unpublished(state->modules[i]);
        ASSERT(!result);
        (void) result;
    }
    erts_persistent_state_discard(state->persistent);
    erts_registry_discard(state->registry);
    erts_code_space_discard(state->code_space);
    erts_module_namespace_discard(state->module_state);
    erts_range_namespace_discard(state->ranges);
    erts_catch_namespace_discard(state->catches);
    erts_code_table_discard(state->funs);
    erts_code_table_discard(state->records);
    erts_export_namespace_discard(state->exports);
    erts_export_literals_discard(state->export_literals);
    erts_atom_namespace_discard_unpublished(state->atoms);
    if (state->engine_prev) state->engine_prev->engine_next = state->engine_next;
    else state->engine->namespace_states = state->engine_next;
    if (state->engine_next) state->engine_next->engine_prev = state->engine_prev;
    ASSERT(state->engine->namespace_count);
    state->engine->namespace_count--;
    free(state);
    return 0;
}
