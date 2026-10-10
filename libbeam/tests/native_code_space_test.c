/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Real namespace tables and transactions; synthetic code addresses are never run.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "sys.h"
#include "erl_vm.h"
#include "global.h"
#include "erl_isolate_state.h"
#include "erl_code_table.h"
#include "erl_record.h"
#include "erl_module_namespace.h"
#include "beam_catches.h"
#include "beam_ranges.h"
#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "code-space check failed at line %d: %s\n", __LINE__, #expr); \
    return 1; } } while (0)

int main(int argc, char **argv)
{
    ErtsEngine *engine = erl_engine_alloc();
    ErtsIsolateNamespaceState *a, *b, *fresh;
    ErtsCodeSpace *ca, *cb;
    ErlFunEntry key, *fa, *fb;
    ErtsRecordEntry record_key, *ra, *rb;
    ErtsCodeTable *small;
    Module *mod;
    UWord code_a[32] = {0}, code_b[32] = {0};
    const BeamCodeHeader *ha = (const BeamCodeHeader *)code_a;
    const BeamCodeHeader *hb = (const BeamCodeHeader *)code_b;
    unsigned head_a = BEAM_CATCHES_NIL, head_b;
    size_t bytes_a;
    int i;
    CHECK(engine && erl_prepare_runtime(engine, argc, argv) == 0);
    a = erts_isolate_namespace_create(engine, 8192, 4096);
    b = erts_isolate_namespace_create(engine, 8192, 4096);
    CHECK(a && b);
    ca = erts_isolate_namespace_code_space(a);
    cb = erts_isolate_namespace_code_space(b);
    CHECK(erts_code_space_active(ca) == 0 && erts_code_space_staging(ca) == 1);
    CHECK(erts_code_space_commit(ca) == 1);
#ifndef BEAMASM
    {
        struct erl_module_instance ma, mb;
        erts_module_instance_init(&ma);
        erts_module_instance_init(&mb);
        CHECK(erts_module_namespace_unseal(erts_isolate_namespace_module_state(a), &ma) == 0);
        CHECK(erts_module_namespace_unseal(erts_isolate_namespace_module_state(b), &mb) == 0);
        CHECK(erts_isolate_namespace_discard(a) == 1);
        CHECK(erts_code_space_start(ca, 0) == 1);
        CHECK(erts_module_namespace_seal(erts_isolate_namespace_module_state(a), &mb) == 1);
        CHECK(erts_module_namespace_seal(erts_isolate_namespace_module_state(a), &ma) == 0);
        CHECK(mb.unsealed);
        CHECK(erts_module_namespace_seal(erts_isolate_namespace_module_state(b), &mb) == 0);
    }
#endif
    sys_memset(&key, 0, sizeof(key));
    key.module = am_erlang; key.arity = 1; key.index = key.old_index = 7;
    record_key.module = am_erlang; record_key.name = am_self;
    fa = erts_code_table_put(erts_isolate_namespace_funs(a), &key, 0);
    fb = erts_code_table_put(erts_isolate_namespace_funs(b), &key, 0);
    ra = erts_code_table_put(erts_isolate_namespace_records(a), &record_key, 0);
    rb = erts_code_table_put(erts_isolate_namespace_records(b), &record_key, 0);
    CHECK(fa && fb && fa != fb && ra && rb && ra != rb);
    CHECK(erts_code_table_put(erts_isolate_namespace_funs(a), &key, 0) == fa);
    {
        ErlFunEntry bulk = key;
        ErtsRecordEntry bulk_record = record_key;
        int atoms = erts_atom_namespace_count(erts_isolate_namespace_atoms(a));
        CHECK(atoms > 1);
        for (i = 0; i < 1500; ++i) {
            bulk.index = 100 + i;
            CHECK(erts_code_table_put(erts_isolate_namespace_funs(a), &bulk, 0));
            bulk_record.module = make_atom(i / atoms);
            bulk_record.name = make_atom(i % atoms);
            CHECK(erts_code_table_put(erts_isolate_namespace_records(a), &bulk_record, 0));
        }
    }
    bytes_a = erts_code_table_entry_bytes(erts_isolate_namespace_funs(a));
    CHECK(bytes_a > erts_code_table_entry_bytes(erts_isolate_namespace_funs(b)));
    ra->definitions[2] = make_small(1);
    CHECK(erts_isolate_namespace_discard(a) == 1);
    ra->definitions[2] = THE_NON_VALUE;
    fa->pend_purge_address = code_a;
    CHECK(erts_isolate_namespace_discard(a) == 1);
    CHECK(erts_code_space_start(ca, 0) == 1);
    fa->pend_purge_address = NULL;
    /* Last-child refusal must not start or copy earlier tables. */
    CHECK(erts_range_namespace_start_staging(erts_isolate_namespace_ranges(a), 0, 1, 0) == 0);
    CHECK(erts_code_space_start(ca, 0) == 1);
    CHECK(!erts_code_table_get(erts_isolate_namespace_funs(a), &key, 1));
    CHECK(erts_catch_namespace_can_discard(erts_isolate_namespace_catches(a)));
    CHECK(erts_range_namespace_end_staging(erts_isolate_namespace_ranges(a), 0) == 0);
    CHECK(erts_code_space_start(ca, 1) == 0);
    CHECK(erts_code_space_start(ca, 0) == 1);
    mod = erts_module_table_put(erts_isolate_namespace_module_at(a, 1), atom_val(am_erlang));
    CHECK(mod);
    mod->curr.code_length = 1;
    CHECK(erts_code_space_end(ca, 0) == 1); /* No force-free of new module resources. */
    CHECK(erts_code_space_active(ca) == 0);
    mod->curr.code_length = 0;
    CHECK(erts_range_namespace_update(erts_isolate_namespace_ranges(a), ha, sizeof(code_a)) == 0);
    CHECK(erts_code_space_end(ca, 0) == 0);
    CHECK(erts_module_table_count(erts_isolate_namespace_module_at(a, 1)) == 0);
    CHECK(!erts_range_namespace_find(erts_isolate_namespace_ranges(a), 1, code_a + 1));
    CHECK(erts_code_space_active(ca) == 0 && erts_code_space_active(cb) == 0);
    CHECK(erts_code_space_start(ca, 1) == 0);
    for (i = 0; i < 1100; ++i)
        head_a = erts_catch_namespace_cons(erts_isolate_namespace_catches(a), 1, code_a + 1, head_a, NULL);
    CHECK(head_a == 1099); /* Growth beyond the initial catch vector. */
    CHECK(erts_range_namespace_update(erts_isolate_namespace_ranges(a), ha, sizeof(code_a)) == 0);
    CHECK(erts_range_namespace_update(erts_isolate_namespace_ranges(a), hb, sizeof(code_b)) == 1);
    CHECK(erts_code_space_end(ca, 1) == 0);
    CHECK(erts_isolate_namespace_discard(a) == 1); /* Ready is not committed or retired. */
    CHECK(erts_code_space_commit(ca) == 0);
    CHECK(erts_code_space_active(ca) == 1 && erts_code_space_active(cb) == 0);
    CHECK(erts_code_table_get(erts_isolate_namespace_funs(a), &key, 1) == fa);
    CHECK(erts_code_table_entry_bytes(erts_isolate_namespace_funs(a)) == bytes_a);
    CHECK(erts_code_space_start(cb, 1) == 0);
    head_b = erts_catch_namespace_cons(erts_isolate_namespace_catches(b), 1, code_b + 1, BEAM_CATCHES_NIL, NULL);
    CHECK(head_b == 0);
    CHECK(erts_range_namespace_update(erts_isolate_namespace_ranges(b), hb, sizeof(code_b)) == 0);
    CHECK(erts_code_space_end(cb, 1) == 0 && erts_code_space_commit(cb) == 0);
    CHECK(erts_catch_namespace_car(erts_isolate_namespace_catches(a), 1, 0) == code_a + 1);
    CHECK(erts_catch_namespace_car(erts_isolate_namespace_catches(b), 1, 0) == code_b + 1);
    for (i = 0; i < 5; ++i) {
        CHECK(erts_code_space_start(ca, 0) == 0);
        CHECK(erts_code_space_end(ca, 1) == 0);
        CHECK(erts_code_space_commit(ca) == 0);
    }
    CHECK(erts_code_space_active(ca) == 0 && erts_code_space_active(cb) == 1);
    CHECK(erts_isolate_namespace_discard(a) == 1); /* Live range/catch references. */
    for (i = 0; i < ERTS_NUM_CODE_IX; ++i)
        if (erts_range_namespace_find(erts_isolate_namespace_ranges(a), i, code_a + 1))
            CHECK(erts_range_namespace_remove(erts_isolate_namespace_ranges(a), i, ha) == 0);
    erts_catch_namespace_delmod(erts_isolate_namespace_catches(a), head_a, ha, sizeof(code_a), erts_code_space_active(ca));
    CHECK(erts_isolate_namespace_discard(a) == 0);
    CHECK(erts_range_namespace_find(erts_isolate_namespace_ranges(b), 1, code_b + 1) == hb);
    CHECK(erts_code_table_get(erts_isolate_namespace_records(b), &record_key, 1) == rb);
    CHECK(erts_range_namespace_remove(erts_isolate_namespace_ranges(b), 1, hb) == 0);
    erts_catch_namespace_delmod(erts_isolate_namespace_catches(b), head_b, hb, sizeof(code_b), 1);
    CHECK(erts_isolate_namespace_discard(b) == 0);
    small = erts_fun_namespace_create(1);
    CHECK(erts_code_table_put(small, &key, 0));
    ++key.index;
    CHECK(!erts_code_table_put(small, &key, 0));
    CHECK(erts_code_table_discard(small) == 0);
    for (i = 0; i < 16; ++i) {
        fresh = erts_isolate_namespace_create(engine, 8192, 4096);
        CHECK(fresh);
        CHECK(erts_code_space_active(erts_isolate_namespace_code_space(fresh)) == 0);
        CHECK(erts_code_space_start(erts_isolate_namespace_code_space(fresh), 0) == 0);
        CHECK(erts_code_space_end(erts_isolate_namespace_code_space(fresh), 0) == 0);
        CHECK(erts_isolate_namespace_discard(fresh) == 0);
    }
    puts("NATIVE_CODE_SPACE_OK independent_tables=true coordinated_metadata_transactions=true guarded_disposal=true private_execution=false");
    fflush(stdout);
    _Exit(0);
}
