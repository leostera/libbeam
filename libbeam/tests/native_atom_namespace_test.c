/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Namespace-state test, not BEAM loading or execution.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "sys.h"
#include "erl_vm.h"
#include "global.h"
#include "module.h"
#include "erl_isolate_state.h"

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "atom namespace check failed at line %d: %s\n", __LINE__, #expr); \
    return 1; } } while (0)
#define INTERN(ns, text) erts_atom_namespace_put((ns), (const unsigned char *)(text), sizeof(text)-1)

int main(int argc, char **argv)
{
    ErtsEngine *engine = erl_engine_alloc();
    ErtsIsolateNamespaceState *a, *b, *fresh;
    ErtsAtomNamespace *aa, *bb, *small;
    Module *ma, *mb;
    int global_count, predefined, ax, bx, i, index;
    Uint global_text, after_text;
    size_t baseline;
    unsigned char out[1024], unicode[1020];
    unsigned char bad[] = {0xc0, 0xaf};
    unsigned char mutable_name[] = "libbeam_owned_name_buffer";
    Eterm global_atom;
    CHECK(engine);
    CHECK(!erts_isolate_namespace_create(engine, 8192, 4096));
    CHECK(erl_prepare_runtime(engine, argc, argv) == 0);
    global_count = atom_table_size();
    erts_atom_get_text_space_sizes(NULL, &global_text);
    CHECK(!erts_isolate_namespace_create(engine, 1, 4096));
    CHECK(!erts_isolate_namespace_create(engine, 8192, 0));
    a = erts_isolate_namespace_create(engine, 8192, 4096);
    b = erts_isolate_namespace_create(engine, 8192, 4096);
    CHECK(a && b && a != b);
    aa = erts_isolate_namespace_atoms(a);
    bb = erts_isolate_namespace_atoms(b);
    predefined = erts_atom_namespace_count(aa);
    baseline = erts_atom_namespace_text_bytes(aa);
    CHECK(erts_atom_namespace_count(bb) == predefined);
    CHECK(INTERN(aa, "true") == atom_val(am_true));
    CHECK(INTERN(bb, "false") == atom_val(am_false));
    ax = INTERN(aa, "libbeam_private_alpha");
    bx = INTERN(bb, "libbeam_private_beta");
    CHECK(ax == predefined && bx == ax);
    CHECK(erts_atom_namespace_name(aa, ax, out, sizeof(out)) == 21);
    CHECK(!memcmp(out, "libbeam_private_alpha", 21));
    CHECK(erts_atom_namespace_name(bb, bx, out, sizeof(out)) == 20);
    CHECK(!memcmp(out, "libbeam_private_beta", 20));
    CHECK(erts_atom_namespace_text_bytes(aa) == baseline + 21);
    CHECK(INTERN(aa, "libbeam_private_alpha") == ax);
    CHECK(erts_atom_namespace_count(aa) == predefined + 1);
    CHECK(!erts_atom_get("libbeam_private_alpha", 21, &global_atom, ERTS_ATOM_ENC_UTF8));
    ma = erts_module_table_put(erts_isolate_namespace_modules(a), ax);
    mb = erts_module_table_put(erts_isolate_namespace_modules(b), bx);
    CHECK(ma && mb && ma != mb);
    CHECK(erts_atom_namespace_put(aa, bad, sizeof(bad)) == -1);
    CHECK(erts_atom_namespace_put(aa, NULL, 1) == -1);
    CHECK(erts_atom_namespace_put(aa, bad, 1021) == -2);
    CHECK(erts_atom_namespace_count(aa) == predefined + 1);
    index = erts_atom_namespace_put(aa, mutable_name, sizeof(mutable_name)-1);
    memset(mutable_name, 'z', sizeof(mutable_name)-1);
    CHECK(erts_atom_namespace_name(aa, index, out, sizeof(out)) == 25);
    CHECK(!memcmp(out, "libbeam_owned_name_buffer", 25));
    for (i = 0; i < 255; ++i) {
        unicode[4*i] = 0xf0; unicode[4*i+1] = 0x9f;
        unicode[4*i+2] = 0x98; unicode[4*i+3] = 0x80;
    }
    index = erts_atom_namespace_put(aa, unicode, sizeof(unicode));
    CHECK(index >= 0);
    CHECK(erts_atom_namespace_name(aa, index, out, sizeof(out)) == sizeof(unicode));
    CHECK(!memcmp(out, unicode, sizeof(unicode)));
    CHECK(erts_atom_namespace_name(aa, index, out, 8) == -1);
    CHECK(erts_atom_namespace_name(aa, -1, out, sizeof(out)) == -1);
    small = erts_atom_namespace_create(predefined + 1);
    CHECK(small && INTERN(small, "libbeam_capacity_a") >= 0);
    CHECK(INTERN(small, "libbeam_capacity_b") == -3);
    CHECK(INTERN(small, "libbeam_capacity_a") == predefined);
    CHECK(erts_atom_namespace_count(small) == predefined + 1);
    erts_atom_namespace_discard_unpublished(small);
    /* Synthetic retention marker: failed parent disposal must preserve atoms. */
    ma->on_load = &ma->curr;
    CHECK(erts_isolate_namespace_discard(a) == 1);
    CHECK(INTERN(aa, "libbeam_private_alpha") == ax);
    ma->on_load = NULL;
    CHECK(erts_isolate_namespace_discard(a) == 0);
    CHECK(erts_atom_namespace_name(bb, bx, out, sizeof(out)) == 20);
    CHECK(!memcmp(out, "libbeam_private_beta", 20));
    CHECK(erts_module_table_find(erts_isolate_namespace_modules(b), bx) == mb);
    for (i = 0; i < 16; ++i) {
        fresh = erts_isolate_namespace_create(engine, 8192, 4096);
        CHECK(fresh);
        CHECK(erts_atom_namespace_count(erts_isolate_namespace_atoms(fresh)) == predefined);
        CHECK(erts_module_table_count(erts_isolate_namespace_modules(fresh)) == 0);
        CHECK(INTERN(erts_isolate_namespace_atoms(fresh), "libbeam_fresh") == predefined);
        CHECK(erts_isolate_namespace_discard(fresh) == 0);
    }
    CHECK(erts_isolate_namespace_discard(b) == 0);
    CHECK(atom_table_size() == global_count);
    erts_atom_get_text_space_sizes(NULL, &after_text);
    CHECK(global_text == after_text);
    puts("NATIVE_ATOM_NAMESPACE_OK local_indices=true owned_names=true peer_survives=true fresh_state=true global_atoms_unchanged=true loaded_beam=0");
    fflush(stdout);
    _Exit(0); /* Only unpublished namespace state, not the engine, was reclaimed. */
}
