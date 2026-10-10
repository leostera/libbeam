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
#include "erl_global_literals.h"
#include "erl_bits.h"
#include "erl_binary.h"
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "constant arena check failed: %d: %s\n", __LINE__, #c); _Exit(2); } } while (0)

static Eterm tuple(ErtsGlobalLiteralArena *arena, int n)
{
    struct erl_off_heap_header **off_heap;
    Eterm *hp = erts_global_literal_arena_allocate(arena, 2, &off_heap);
    Eterm result;
    CHECK(hp);
    CHECK(erts_global_literal_arena_discard(arena) == 1); /* allocation in progress */
    result = TUPLE1(hp, make_small(n));
    erts_global_literal_arena_register(arena, &result);
    return result;
}

int main(int argc, char **argv)
{
    ErtsEngine *engine = erl_engine_alloc();
    int i, j;
    CHECK(engine && erl_prepare_runtime(engine, argc, argv) == 0);
    CHECK(engine->global_literals);
    CHECK(erts_global_literal_arena_discard(engine->global_literals) == 1);
    CHECK(!erts_global_literal_arena_create(0));
    CHECK(!erts_global_literal_arena_create(~(Uint)0));
    for (i = 0; i < 32; ++i) {
        ErtsGlobalLiteralArena *a = erts_global_literal_arena_create(8);
        ErtsGlobalLiteralArena *b = erts_global_literal_arena_create(8);
        Eterm peer = tuple(b, 42);
        Uint before = erts_global_literal_arena_bytes(a);
        Binary *retained;
        struct erl_off_heap_header **off_heap;
        ErlOffHeap oh;
        ErtsHeapFactory factory;
        byte data[1024];
        Eterm value, *hp;
        for (j = 0; j < 1024; ++j) {
            Eterm term = tuple(a, j);
            CHECK(signed_val(tuple_val(term)[1]) == j);
        }
        CHECK(erts_global_literal_arena_bytes(a) > before);
        sys_memset(data, 0x5a, sizeof(data));
        hp = erts_global_literal_arena_allocate(a, ERL_REFC_BITS_SIZE, &off_heap);
        ERTS_INIT_OFF_HEAP(&oh);
        erts_factory_static_init(&factory, hp, ERL_REFC_BITS_SIZE, &oh);
        value = erts_hfact_new_binary_from_data(&factory, 0, sizeof(data), data);
        *off_heap = oh.first;
        retained = ((BinRef *)oh.first)->val;
        erts_refc_inc(&retained->intern.refc, 1); /* external observer */
        erts_global_literal_arena_register(a, &value);
        CHECK(erts_global_literal_arena_discard(a) == 0);
        CHECK(erts_refc_read(&retained->intern.refc, 1) == 1);
        CHECK(retained->orig_bytes[0] == 0x5a);
        erts_bin_release(retained);
        CHECK(signed_val(tuple_val(peer)[1]) == 42);
        CHECK(erts_global_literal_arena_discard(b) == 0);
    }
    CHECK(is_tuple(ERTS_GLOBAL_LIT_EMPTY_TUPLE));
    puts("NATIVE_CONSTANT_ARENA_OK retained_bases=true offheap_release=true peer_survival=true engine_shutdown=false");
    fflush(stdout);
    _Exit(0);
}
