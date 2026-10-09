// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#include <erl_embed.h>
#include <erl_module_table.h>
#include <cstdio>
#include <cstdlib>

#define CHECK(expr) do { if (!(expr)) { \
    std::fprintf(stderr, "module table check failed at line %d: %s\n", __LINE__, #expr); \
    return 1; } } while (0)

// Real ERTS module/index/hash components. No private atom interning, bytecode
// loading, application execution, or public Isolate handles are exercised here.
int main(int argc, char** argv) {
    ErtsEngine* engine = erl_engine_alloc();
    CHECK(engine && erl_prepare_runtime(engine, argc, argv) == 0);
    CHECK(erts_module_table_discard_unpublished(nullptr) == 1);
    CHECK(!erts_module_table_create(0));
    CHECK(!erts_module_table_create(-1));
    auto* b = erts_module_table_create(4096);
    CHECK(b);
    // Untagged namespace-local atom index, not a claim to have loaded probe.beam.
    constexpr int shared_key = 17;
    auto* b_entry = erts_module_table_put(b, shared_key);
    CHECK(b_entry && erts_module_table_count(b) == 1);
    for (int cycle = 0; cycle < 16; ++cycle) {
        auto* a = erts_module_table_create(4096);
        CHECK(a && a != b && erts_module_table_count(a) == 0);
        CHECK(!erts_module_table_find(a, shared_key));
        CHECK(!erts_module_table_put(a, -1));
        auto* a_entry = erts_module_table_put(a, shared_key);
        CHECK(a_entry && a_entry != b_entry);
        CHECK(erts_module_table_put(a, shared_key) == a_entry);
        CHECK(erts_module_table_count(a) == 1);
        const int capacity = erts_module_table_capacity(a);
        CHECK(capacity >= 4096);
        // Exercise hash growth and multiple index pages, then the full-table path.
        for (int key = 0; key < capacity; ++key)
            CHECK(erts_module_table_put(a, key));
        CHECK(erts_module_table_count(a) == capacity);
        CHECK(!erts_module_table_put(a, capacity));
        CHECK(erts_module_table_count(a) == capacity);
        CHECK(erts_module_table_put(a, shared_key) == a_entry);
        CHECK(!erts_module_table_find(b, capacity - 1));
        CHECK(erts_module_table_count(b) == 1);
        CHECK(erts_module_table_discard_unpublished(a) == 0);
        CHECK(erts_module_table_find(b, shared_key) == b_entry);
        CHECK(erts_module_table_count(b) == 1);
    }
    CHECK(erts_module_table_discard_unpublished(b) == 0);
    auto* empty = erts_module_table_create(1);
    CHECK(empty && erts_module_table_discard_unpublished(empty) == 0);
    ErlPreparedRuntimeInventory inventory{};
    CHECK(erl_prepared_runtime_inventory(engine, &inventory) == 0);
    CHECK(!inventory.processes && !inventory.ports && !inventory.loaded_code_bytes &&
          !inventory.init_process_created && !inventory.system_process_roots);
    CHECK(erl_engine_discard_uninitialized(engine) == 1);
    std::puts("NATIVE_MODULE_TABLE_OK independent_records=true growth_full_and_discard=true cycles=16 loaded_beam=0 isolates=0");
    std::fflush(stdout);
    // Only table components were reclaimed. Preparation remains process-lifetime.
    std::_Exit(0);
}
