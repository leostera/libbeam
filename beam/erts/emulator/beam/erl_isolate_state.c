/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#include "erl_isolate_state.h"
#include <stdlib.h>

struct ErtsIsolateNamespaceState {
    ErtsEngine *engine;
    ErtsAtomNamespace *atoms;
    ErtsModuleTable *modules;
};

ErtsIsolateNamespaceState *erts_isolate_namespace_create(ErtsEngine *engine,
                                                       int atom_limit,
                                                       int module_limit)
{
    ErtsIsolateNamespaceState *state;
    enum ErlRuntimeStartupPhase phase;
    if (!engine || module_limit <= 0)
        return NULL;
    phase = erl_runtime_startup_phase(engine);
    if (phase != ERL_RUNTIME_PREPARED && phase != ERL_RUNTIME_THREADS_STARTED)
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
    state->modules = erts_module_table_create(module_limit);
    if (!state->modules) {
        erts_atom_namespace_discard_unpublished(state->atoms);
        free(state);
        return NULL;
    }
    return state;
}

ErtsAtomNamespace *erts_isolate_namespace_atoms(ErtsIsolateNamespaceState *state)
{
    return state->atoms;
}

ErtsModuleTable *erts_isolate_namespace_modules(ErtsIsolateNamespaceState *state)
{
    return state->modules;
}

int erts_isolate_namespace_discard(ErtsIsolateNamespaceState *state)
{
    if (!state || erts_module_table_discard_unpublished(state->modules))
        return 1;
    erts_atom_namespace_discard_unpublished(state->atoms);
    free(state);
    return 0;
}
