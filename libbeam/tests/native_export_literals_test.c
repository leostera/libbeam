/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Literal storage only: local export descriptors are never dispatched/published.
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
    fprintf(stderr, "export literal check failed at line %d: %s\n", __LINE__, #expr); \
    return 1; } } while (0)

int main(int argc, char **argv)
{
    ErtsEngine *engine = erl_engine_alloc();
    ErtsIsolateNamespaceState *a, *b, *fresh;
    ErtsExportLiterals *pa, *pb;
    Export ea, eb;
    Eterm fa, fb;
    int i;
    CHECK(engine && erl_prepare_runtime(engine, argc, argv) == 0);
    a = erts_isolate_namespace_create(engine, 8192, 4096);
    b = erts_isolate_namespace_create(engine, 8192, 4096);
    CHECK(a && b);
    pa = erts_isolate_namespace_export_literals(a);
    pb = erts_isolate_namespace_export_literals(b);
    sys_memset(&ea, 0, sizeof(ea));
    sys_memset(&eb, 0, sizeof(eb));
    ea.info.mfa.module = eb.info.mfa.module = am_erlang;
    ea.info.mfa.function = eb.info.mfa.function = am_self;
    ea.info.mfa.arity = eb.info.mfa.arity = 0;
    fa = erts_export_literal_create(pa, &ea);
    fb = erts_export_literal_create(pb, &eb);
    CHECK(fa != fb && is_any_fun(fa) && is_any_fun(fb));
    CHECK(((ErlFunThing *) fun_val(fa))->entry.exp == &ea &&
          ((ErlFunThing *) fun_val(fb))->entry.exp == &eb);
    CHECK(erts_export_literals_count(pa) == 1 && erts_export_literals_count(pb) == 1);
    for (i = 0; i < 256; ++i) {
        Eterm extra = erts_export_literal_create(pa, &ea);
        CHECK(is_any_fun(extra));
    }
    CHECK(erts_export_literals_count(pa) == 257);
    CHECK(erts_isolate_namespace_discard(a) == 0);
    CHECK(((ErlFunThing *) fun_val(fb))->entry.exp == &eb &&
          erts_export_literals_count(pb) == 1);
    for (i = 0; i < 16; ++i) {
        fresh = erts_isolate_namespace_create(engine, 8192, 4096);
        CHECK(fresh);
        CHECK(erts_export_literals_count(erts_isolate_namespace_export_literals(fresh)) == 0);
        CHECK(erts_isolate_namespace_discard(fresh) == 0);
    }
    CHECK(erts_isolate_namespace_discard(b) == 0);
    fresh = erts_isolate_namespace_create(engine, 8192, 4096);
    CHECK(fresh);
    pa = erts_isolate_namespace_export_literals(fresh);
    erts_export_literals_bind(pa);
    CHECK(erts_export_literals_discard(pa) == 1);
    CHECK(erts_isolate_namespace_discard(fresh) == 1);
    CHECK(erts_atom_namespace_count(erts_isolate_namespace_atoms(fresh)) > 0);
    puts("NATIVE_EXPORT_LITERALS_OK independent_areas=true peer_survives=true bound_retained=true private_execution=false");
    fflush(stdout);
    _Exit(0); /* Bound test owner and diagnostic preparation remain live. */
}
