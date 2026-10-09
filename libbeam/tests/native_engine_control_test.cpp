// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#include <erl_embed.h>
#include <cstdio>

// Control-object allocation/free only. This is NOT engine startup, two running
// engines, isolate construction, or the RFD's application-world churn test.
int main() {
    if (erl_engine_discard_uninitialized(nullptr) != 1) return 1;
    for (int i = 0; i < 128; ++i) {
        ErtsEngine* a = erl_engine_alloc();
        ErtsEngine* b = erl_engine_alloc();
        if (!a || !b || a == b) return 2;
        if (erl_runtime_startup_phase(a) != ERL_RUNTIME_UNCLAIMED ||
            erl_runtime_startup_phase(b) != ERL_RUNTIME_UNCLAIMED) return 3;
        if (erl_engine_discard_uninitialized(a) != 0 ||
            erl_runtime_startup_phase(b) != ERL_RUNTIME_UNCLAIMED ||
            erl_engine_discard_uninitialized(b) != 0) return 4;
    }
    std::puts("NATIVE_ENGINE_CONTROL_OK pairs=128 initialized_runtimes=0 isolates=0");
    return 0;
}
