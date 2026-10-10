/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Real export-table/literal storage; no private code dispatch or code-index commit.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "sys.h"
#include "erl_vm.h"
#include "global.h"
#include "export.h"
#include "erl_isolate_state.h"
#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "export namespace check failed at line %d: %s\n", __LINE__, #expr); \
    return 1; } } while (0)

int main(int argc, char **argv)
{
    ErtsEngine *engine = erl_engine_alloc();
    ErtsIsolateNamespaceState *a, *b, *fresh;
    ErtsExportNamespace *ea, *eb, *small;
    ErtsExportLiterals *pool;
    Export *fa, *fb, *extra;
    Eterm live_b;
    size_t single_bytes;
    int m = atom_val(am_erlang), f = atom_val(am_self), i;
    CHECK(engine && erl_prepare_runtime(engine, argc, argv) == 0);
    a = erts_isolate_namespace_create(engine, 8192, 4096);
    b = erts_isolate_namespace_create(engine, 8192, 4096);
    CHECK(a && b);
    ea = erts_isolate_namespace_exports(a);
    eb = erts_isolate_namespace_exports(b);
    CHECK(!erts_export_namespace_find(ea, m, f, 0, 0));
    CHECK(!erts_export_namespace_put(ea, -1, f, 0, 0));
    CHECK(!erts_export_namespace_put(ea, m, f, 256, 0));
    CHECK(!erts_export_namespace_put(ea, m, f, 0, ERTS_NUM_CODE_IX));
    fa = erts_export_namespace_put(ea, m, f, 0, 0);
    fb = erts_export_namespace_put(eb, m, f, 0, 0);
    CHECK(fa && fb && fa != fb && fa->lambda != fb->lambda);
    single_bytes = erts_export_namespace_entry_bytes(ea);
    CHECK(single_bytes > 0 && erts_export_namespace_entry_bytes(eb) == single_bytes);
    CHECK(((ErlFunThing *) fun_val(fa->lambda))->entry.exp == fa);
    CHECK(erts_export_namespace_put(ea, m, f, 0, 0) == fa);
    CHECK(erts_export_literals_count(erts_isolate_namespace_export_literals(a)) == 1);
    CHECK(erts_export_namespace_start_staging(ea, 0, 1) == 0);
    CHECK(erts_export_namespace_find(ea, m, f, 0, 1) == fa);
    CHECK(erts_export_namespace_start_staging(ea, 0, 2) == 1);
    CHECK(erts_isolate_namespace_discard(a) == 1); /* Open staging retains all state. */
    CHECK(erts_export_namespace_end_staging(ea, 2) == 1);
    CHECK(erts_export_namespace_end_staging(ea, 1) == 0);
    CHECK(erts_export_namespace_start_staging(ea, 1, 2) == 0);
    CHECK(erts_export_namespace_end_staging(ea, 2) == 0);
    CHECK(erts_export_namespace_find(ea, m, f, 0, 2) == fa);
    CHECK(erts_export_literals_count(erts_isolate_namespace_export_literals(a)) == 1);
    CHECK(erts_export_namespace_count(eb, 1) == 0);
    CHECK(erts_export_namespace_entry_bytes(ea) == single_bytes);
    /* Synthetic resource marker: no code address is ever executed. */
    fa->bif_number = 0;
    CHECK(erts_isolate_namespace_discard(a) == 1);
    fa->bif_number = -1;
    fa->is_bif_traced = 1;
    CHECK(erts_isolate_namespace_discard(a) == 1);
    fa->is_bif_traced = 0;
    fa->info.gen_bp = (void *) 1;
    CHECK(erts_isolate_namespace_discard(a) == 1);
    fa->info.gen_bp = NULL;
    ((unsigned char *)&fa->info.u)[0] = 1;
    CHECK(erts_isolate_namespace_discard(a) == 1);
    ((unsigned char *)&fa->info.u)[0] = 0;
    fa->trampoline.not_loaded.deferred = 1;
    CHECK(erts_isolate_namespace_discard(a) == 1);
    fa->trampoline.not_loaded.deferred = 0;
    for (i = 0; i < ERTS_NUM_CODE_IX; ++i) {
        ErtsCodePtr saved = fa->dispatch.addresses[i];
        fa->dispatch.addresses[i] = NULL;
        CHECK(erts_isolate_namespace_discard(a) == 1);
        fa->dispatch.addresses[i] = saved;
    }
    CHECK(erts_export_namespace_find(ea, m, f, 0, 0) == fa);
    /* Cross index-page growth using valid predefined local atom indices. */
    for (i = 0; i < 2200; ++i)
        CHECK(erts_export_namespace_put(ea, i / 256, f, i % 256, 0));
    CHECK(erts_export_namespace_count(ea, 0) >= 2200);
    CHECK(erts_export_namespace_count(eb, 0) == 1);
    CHECK(erts_export_namespace_entry_bytes(ea) > single_bytes);
    CHECK(erts_export_namespace_entry_bytes(eb) == single_bytes);
    live_b = fb->lambda;
    CHECK(erts_isolate_namespace_discard(a) == 0);
    CHECK(erts_export_namespace_find(eb, m, f, 0, 0) == fb);
    CHECK(((ErlFunThing *) fun_val(live_b))->entry.exp == fb);
    pool = erts_export_literals_create();
    CHECK(!erts_export_namespace_create(pool, 0));
    small = erts_export_namespace_create(pool, 2);
    CHECK(small);
    CHECK(erts_export_namespace_put(small, m, f, 0, 0));
    CHECK(erts_export_namespace_put(small, m, f, 1, 0));
    CHECK(!erts_export_namespace_put(small, m, f, 2, 0));
    CHECK(erts_export_namespace_count(small, 0) == 2);
    extra = erts_export_namespace_put(small, m, f, 2, 1);
    CHECK(extra);
    CHECK(erts_export_namespace_start_staging(small, 0, 1) == 1);
    CHECK(erts_export_namespace_count(small, 1) == 1);
    CHECK(erts_export_namespace_find(small, m, f, 2, 1) == extra);
    CHECK(erts_export_namespace_can_discard(small));
    CHECK(erts_export_namespace_discard(small) == 0);
    CHECK(erts_export_literals_discard(pool) == 0);
    pool = erts_export_literals_create();
    small = erts_export_namespace_create(pool, 4);
    CHECK(small);
    CHECK(erts_export_namespace_put(small, m, f, 0, 0));
    extra = erts_export_namespace_put(small, m, f, 0, 1);
    CHECK(extra);
    /* Incompatible slot identities cannot be merged or partially staged. */
    CHECK(erts_export_namespace_start_staging(small, 0, 1) == 1);
    CHECK(erts_export_namespace_find(small, m, f, 0, 1) == extra);
    CHECK(erts_export_namespace_can_discard(small));
    CHECK(erts_export_namespace_discard(small) == 0);
    CHECK(erts_export_literals_discard(pool) == 0);
    for (i = 0; i < 16; ++i) {
        fresh = erts_isolate_namespace_create(engine, 8192, 4096);
        CHECK(fresh);
        CHECK(erts_export_namespace_count(erts_isolate_namespace_exports(fresh), 0) == 0);
        CHECK(erts_export_namespace_entry_bytes(erts_isolate_namespace_exports(fresh)) == 0);
        CHECK(erts_export_namespace_put(erts_isolate_namespace_exports(fresh), m, f, 0, 0));
        CHECK(erts_isolate_namespace_discard(fresh) == 0);
    }
    CHECK(erts_isolate_namespace_discard(b) == 0);
    puts("NATIVE_EXPORT_NAMESPACE_OK same_mfa_independent=true scoped_staging=true guarded_disposal=true private_execution=false");
    fflush(stdout);
    _Exit(0);
}
