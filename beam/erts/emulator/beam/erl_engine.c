/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#include "erl_engine.h"
#include <stdlib.h>

ErtsEngine *
erl_engine_alloc(void)
{
    /* The native allocator is not initialized yet. This control object has a
     * separate, symmetric system allocation lifetime. No VM initialization. */
    ErtsEngine *engine = calloc(1, sizeof(*engine));
    if (engine)
        engine->startup_phase = ERL_RUNTIME_UNCLAIMED;
    return engine;
}

int
erl_engine_discard_uninitialized(ErtsEngine *engine)
{
    if (!engine || engine->startup_phase != ERL_RUNTIME_UNCLAIMED ||
        engine->scheduler_threads || engine->io_poll_group || engine->thread_progress ||
        engine->allocators || engine->namespace_states || engine->namespace_count ||
        engine->diagnostic_namespace || engine->thread_keys || engine->global_literals)
        return 1;
    free(engine);
    return 0;
}

enum ErlRuntimeStartupPhase
erl_runtime_startup_phase(const ErtsEngine *engine)
{
    return engine->startup_phase;
}
