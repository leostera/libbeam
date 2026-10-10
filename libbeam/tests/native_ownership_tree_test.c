/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "sys.h"
#include "erl_vm.h"
#include "global.h"
#include "erl_engine.h"
#include "erl_engine_thread_keys.h"
#include "erl_isolate_state.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "ownership tree check failed at %d: %s\n", __LINE__, #c); _Exit(2); } } while (0)

int main(int argc, char **argv)
{
    ErtsEngine *engine = erl_engine_alloc();
    ErtsIsolateNamespaceState *a, *b, *c, *diagnostic;
    int i;
    CHECK(engine);
    for (i = 0; i < 32; ++i) {
        ErtsEngine *candidate = erl_engine_alloc();
        CHECK(candidate && erts_engine_init_thread_keys(candidate) == 0);
        CHECK(erts_engine_thread_key_count(candidate) == 0);
        CHECK(erl_engine_discard_uninitialized(candidate) == 1);
        CHECK(erts_engine_discard_empty_thread_keys(candidate) == 0);
        CHECK(erl_engine_discard_uninitialized(candidate) == 0);
    }
    CHECK(erl_prepare_runtime(engine, argc, argv) == 0);
    diagnostic = erts_diagnostic_namespace();
    CHECK(diagnostic && engine->diagnostic_namespace == diagnostic);
    CHECK(erts_isolate_namespace_engine(diagnostic) == engine);
    CHECK(erts_engine_namespace_count(engine) == 1);
    {
        size_t keys = erts_engine_thread_key_count(engine);
        CHECK(keys > 0 && erts_engine_thread_keys_shared_bytes() > 0);
        CHECK(erts_engine_discard_empty_thread_keys(engine) == 1);
        for (i = 0; i < 64; ++i) {
            erts_tsd_key_t key;
            erts_tsd_key_create(&key, "libbeam_owned_key_test");
            CHECK(erts_engine_thread_key_count(engine) == keys + 1);
            CHECK(erts_tsd_get(key) == NULL);
            erts_tsd_set(key, &i);
            CHECK(erts_tsd_get(key) == &i);
            erts_tsd_set(key, NULL);
            erts_tsd_key_delete(key);
            CHECK(erts_engine_thread_key_count(engine) == keys);
        }
        {
            erts_tsd_key_t raw;
            CHECK(ethr_tsd_key_create(&raw, "separately_owned_key") == 0);
            CHECK(erts_engine_thread_key_count(engine) == keys);
            erts_tsd_key_delete(raw);
            CHECK(erts_engine_thread_key_count(engine) == keys);
        }
    }
    CHECK(!erts_registry_can_discard(erts_isolate_namespace_registry(diagnostic)));
    CHECK(erts_isolate_namespace_discard(diagnostic) == 1);
    CHECK(!erts_isolate_namespace_create_diagnostic(engine, 8192, 4096, 4096));
    CHECK(!erts_isolate_namespace_create(engine, 8192, 0));
    CHECK(!erts_isolate_namespace_create(engine, 1, 4096));
    CHECK(erts_engine_namespace_count(engine) == 1);
    for (i = 0; i < 32; ++i) {
        a = erts_isolate_namespace_create(engine, 8192, 4096);
        b = erts_isolate_namespace_create(engine, 8192, 4096);
        c = erts_isolate_namespace_create(engine, 8192, 4096);
        CHECK(a && b && c && engine->namespace_states == c);
        CHECK(erts_engine_namespace_count(engine) == 4);
        CHECK(erts_isolate_namespace_engine(a) == engine);
        CHECK(erts_isolate_namespace_registry(a) != erts_isolate_namespace_registry(b));
        CHECK(erts_registry_count(erts_isolate_namespace_registry(a)) == 0);
        CHECK(erts_registry_count(erts_isolate_namespace_registry(b)) == 0);
        CHECK(erts_isolate_namespace_persistent(a) != erts_isolate_namespace_persistent(b));
        CHECK(erts_persistent_state_can_discard(erts_isolate_namespace_persistent(a)));
        CHECK(is_non_value(erts_persistent_term_get(a, am_undefined)));
        CHECK(is_non_value(erts_persistent_term_get(b, am_undefined)));
        erts_isolate_namespace_acquire(b);
        CHECK(!erts_persistent_state_can_discard(erts_isolate_namespace_persistent(b)));
        CHECK(erts_isolate_namespace_discard(b) == 1);
        CHECK(erts_engine_namespace_count(engine) == 4);
        erts_isolate_namespace_release(b);
        CHECK(erts_isolate_namespace_discard(b) == 0); /* middle */
        CHECK(erts_engine_namespace_count(engine) == 3);
        CHECK(erts_isolate_namespace_discard(a) == 0); /* before diagnostic tail */
        CHECK(erts_isolate_namespace_discard(c) == 0); /* head */
        CHECK(engine->namespace_states == diagnostic);
        CHECK(erts_engine_namespace_count(engine) == 1);
    }
    a = erts_isolate_namespace_create(engine, 8192, 4096);
    CHECK(a);
    erts_engine_close_namespace_admission(engine);
    CHECK(!erts_isolate_namespace_create(engine, 8192, 4096));
    CHECK(erts_engine_namespace_count(engine) == 2);
    CHECK(erts_isolate_namespace_discard(a) == 0);
    CHECK(erts_engine_namespace_count(engine) == 1);
    CHECK(erl_engine_discard_uninitialized(engine) == 1);
    puts("NATIVE_OWNERSHIP_TREE_OK parent_membership=true registry_children=true owned_thread_keys=true guarded_retirement=true private_execution=false");
    fflush(stdout);
    _Exit(0);
}
