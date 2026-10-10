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
#include <stdlib.h>

struct ErtsIsolateNamespaceState {
    ErtsEngine *engine;
    ErtsAtomNamespace *atoms;
    ErtsModuleTable *modules[ERTS_NUM_CODE_IX];
    ErtsExportLiterals *export_literals;
    ErtsExportNamespace *exports;
    int bound; /* Diagnostic execution references prevent unpublished disposal. */
};

static ErtsIsolateNamespaceState *create_state(ErtsEngine *engine,
                                              int atom_limit, int module_limit,
                                              int export_limit)
{
    ErtsIsolateNamespaceState *state;
    int i;
    if (!engine || module_limit <= 0 || export_limit <= 0)
        return NULL;
    state = calloc(1, sizeof(*state));
    if (!state)
        return NULL;
    state->engine = engine;
    state->atoms = erts_atom_namespace_create(atom_limit);
    if (!state->atoms) {
        free(state);
        return NULL;
    }
    state->export_literals = erts_export_literals_create();
    state->exports = erts_export_namespace_create(state->export_literals, export_limit);
    ASSERT(state->exports);
    for (i = 0; i < ERTS_NUM_CODE_IX; ++i) {
        state->modules[i] = erts_module_table_create(module_limit);
        ASSERT(state->modules[i]); /* validated limit; ERTS allocation is fatal */
    }
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
    ASSERT(erl_runtime_startup_phase(engine) == ERL_RUNTIME_PREPARING);
    state = create_state(engine, atom_limit,
                         module_limit > 0 ? module_limit : erts_module_table_default_limit(),
                         export_limit > 0 ? export_limit : erts_export_namespace_default_limit());
    if (state)
        state->bound = 1;
    return state;
}

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

int erts_isolate_namespace_discard(ErtsIsolateNamespaceState *state)
{
    int i;
    if (!state || state->bound ||
        !erts_export_literals_can_discard(state->export_literals) ||
        !erts_export_namespace_can_discard(state->exports))
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
    erts_export_namespace_discard(state->exports);
    erts_export_literals_discard(state->export_literals);
    erts_atom_namespace_discard_unpublished(state->atoms);
    free(state);
    return 0;
}
