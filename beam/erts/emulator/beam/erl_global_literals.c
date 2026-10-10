/*
 * %CopyrightBegin%
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright Ericsson AB 2020-2026. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * %CopyrightEnd%
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "sys.h"
#include "global.h"
#include "erl_global_literals.h"
#include "erl_mmap.h"
#include "erl_engine.h"


#define GLOBAL_LITERAL_INITIAL_SIZE (1<<16)
#define GLOBAL_LITERAL_EXPAND_SIZE 512


/*
 * Global Constant Literals
 */
Eterm ERTS_WRITE_UNLIKELY(ERTS_GLOBAL_LIT_OS_TYPE);
Eterm ERTS_WRITE_UNLIKELY(ERTS_GLOBAL_LIT_OS_VERSION);
Eterm ERTS_WRITE_UNLIKELY(ERTS_GLOBAL_LIT_DFLAGS_RECORD);
Eterm ERTS_WRITE_UNLIKELY(ERTS_GLOBAL_LIT_ERL_FILE_SUFFIX);
Eterm ERTS_WRITE_UNLIKELY(ERTS_GLOBAL_LIT_EMPTY_TUPLE);
Eterm ERTS_WRITE_UNLIKELY(ERTS_GLOBAL_LIT_EMPTY_BINARY);

/* This lock is taken in the beginning of erts_global_literal_allocate,
 * released at the end of erts_global_literal_register. It protects the 
 * allocated literal chunk, and the heap pointer from concurrent access until 
 * the literal tag is set.
 */
struct ErtsGlobalLiteralArena {
    erts_mtx_t lock;
    struct global_literal_chunk *chunks;
    Uint build_size;
    Uint bytes;
    int bound;
};
static ErtsGlobalLiteralArena *diagnostic_literals;

/* Bump allocator for global literal chunks, allocating them in
 * reasonably large chunks to simplify crash dumping and avoid fragmenting the
 * literal heap too much.
 *
 * This is protected by the global literal lock. */
struct global_literal_chunk {
    struct global_literal_chunk *next;
    Eterm *chunk_end;
    void *allocation_base;

    ErtsLiteralArea area;
};


ErtsLiteralArea *erts_global_literal_iterate_area(ErtsLiteralArea *prev)
{
    struct global_literal_chunk *next;

    ASSERT(ERTS_IS_CRASH_DUMPING);

    if (prev != NULL) {
        struct global_literal_chunk *chunk = ErtsContainerStruct(prev,
                                                    struct global_literal_chunk,
                                                    area);
        next = chunk->next;

        if (next == NULL) {
            return NULL;
        }
    } else {
        next = diagnostic_literals ? diagnostic_literals->chunks : NULL;
    }

    return next ? &next->area : NULL;
}

static void expand_shared_global_literal_area(ErtsGlobalLiteralArena *arena, Uint heap_size)
{
    const size_t size = (offsetof(struct global_literal_chunk, area)
                         + ERTS_LITERAL_AREA_ALLOC_SIZE(heap_size));
    struct global_literal_chunk *chunk;
                        
#ifndef DEBUG 
    chunk = (struct global_literal_chunk *) erts_alloc(ERTS_ALC_T_LITERAL, size);
    chunk->allocation_base = chunk;
    arena->bytes += size;
#else
    /* erts_mem_guard requires the memory area to be page aligned. Overallocate
     * and align the address to ensure that is the case. */
    void *base = erts_alloc(ERTS_ALC_T_LITERAL, size + sys_page_size * 2);
    UWord address = ((UWord)base + (sys_page_size - 1)) & ~(sys_page_size - 1);
    chunk = (struct global_literal_chunk *) address;
    chunk->allocation_base = base;
    arena->bytes += size + sys_page_size * 2;

    for (Uint i = 0; i < heap_size; i++) {
        chunk->area.start[i] = ERTS_HOLE_MARKER;
    }
#endif

    chunk->area.end = &(chunk->area.start[0]);
    chunk->chunk_end = &(chunk->area.start[heap_size]);
    chunk->area.retained_namespace = NULL; /* Immutable engine constants. */
    chunk->area.off_heap = NULL;
    chunk->next = arena->chunks;

    arena->chunks = chunk;
}

Eterm *erts_global_literal_arena_allocate(ErtsGlobalLiteralArena *arena, Uint heap_size,
                                        struct erl_off_heap_header ***ohp)
{
    struct global_literal_chunk *chunk;
    if (!arena || !heap_size || heap_size > ((~(Uint)0) / sizeof(Eterm)) / 2)
        return NULL;
    erts_mtx_lock(&arena->lock);

    chunk = arena->chunks;
    ASSERT(chunk->area.end <= chunk->chunk_end && chunk->area.end >= chunk->area.start);
    if (chunk->chunk_end - chunk->area.end < heap_size) {
        expand_shared_global_literal_area(arena, heap_size + GLOBAL_LITERAL_EXPAND_SIZE);
        chunk = arena->chunks;
    }

    *ohp = &chunk->area.off_heap;

#ifdef DEBUG
    {
        erts_mem_guard(chunk,
                       (byte*)(chunk->area.end + heap_size) - (byte*)chunk,
                       1, 
                       1);
    }
#endif

    arena->build_size = heap_size;

    return chunk->area.end;
}

void erts_global_literal_arena_register(ErtsGlobalLiteralArena *arena, Eterm *variable) {
    struct global_literal_chunk *chunk = arena->chunks;

    ASSERT(arena->build_size && ptr_val(*variable) >= chunk->area.end &&
           ptr_val(*variable) < (chunk->area.end + arena->build_size));

    erts_set_literal_tag(variable, chunk->area.end, arena->build_size);
    chunk->area.end += arena->build_size;
    arena->build_size = 0;

    ASSERT(chunk->area.end <= chunk->chunk_end &&
           chunk->area.end >= chunk->area.start);
    ASSERT(chunk->area.end == chunk->chunk_end ||
           chunk->area.end[0] == ERTS_HOLE_MARKER);

#ifdef DEBUG
    erts_mem_guard(chunk,
                   (byte*)chunk->chunk_end - (byte*)chunk,
                   1,
                   0);
#endif

    erts_mtx_unlock(&arena->lock);
}

Eterm *erts_global_literal_allocate(Uint size, struct erl_off_heap_header ***ohp)
{
    return erts_global_literal_arena_allocate(diagnostic_literals, size, ohp);
}

void erts_global_literal_register(Eterm *variable)
{
    erts_global_literal_arena_register(diagnostic_literals, variable);
}

ErtsGlobalLiteralArena *erts_global_literal_arena_create(Uint initial_size)
{
    ErtsGlobalLiteralArena *arena;
    if (!initial_size || initial_size > ((~(Uint)0) / sizeof(Eterm)) / 2) return NULL;
    arena = erts_alloc(ERTS_ALC_T_LITERAL, sizeof(*arena));
    sys_memzero(arena, sizeof(*arena));
    arena->bytes = sizeof(*arena);
    erts_mtx_init(&arena->lock, "global_literals", NIL, ERTS_LOCK_FLAGS_CATEGORY_GENERIC);
    expand_shared_global_literal_area(arena, initial_size);
    return arena;
}

Uint erts_global_literal_arena_bytes(ErtsGlobalLiteralArena *arena)
{
    Uint bytes;
    if (!arena) return 0;
    erts_mtx_lock(&arena->lock);
    bytes = arena->bytes;
    erts_mtx_unlock(&arena->lock);
    return bytes;
}

int erts_global_literal_arena_discard(ErtsGlobalLiteralArena *arena)
{
    struct global_literal_chunk *chunk;
    if (!arena || arena->bound || arena->build_size) return 1;
    /* Exclusive unpublished disposal: no term pointers may have escaped. */
    chunk = arena->chunks;
    while (chunk) {
        struct global_literal_chunk *next;
        void *base;
        ErlOffHeap off_heap;
#ifdef DEBUG
        erts_mem_guard(chunk, (byte *)chunk->chunk_end - (byte *)chunk, 1, 1);
#endif
        next = chunk->next;
        base = chunk->allocation_base;
        ASSERT(!chunk->area.retained_namespace);
        ERTS_INIT_OFF_HEAP(&off_heap);
        off_heap.first = chunk->area.off_heap;
        erts_cleanup_offheap(&off_heap);
        erts_free(ERTS_ALC_T_LITERAL, base);
        chunk = next;
    }
    erts_mtx_destroy(&arena->lock);
    erts_free(ERTS_ALC_T_LITERAL, arena);
    return 0;
}

static void init_empty_tuple(void) {
    struct erl_off_heap_header **ohp;
    Eterm* hp = erts_global_literal_allocate(2, &ohp);
    Eterm tuple;
    hp[0] = make_arityval_zero();
    hp[1] = make_arityval_zero();
    tuple = make_tuple(hp);
    erts_global_literal_register(&tuple);
    ERTS_GLOBAL_LIT_EMPTY_TUPLE = tuple;
}

static void init_empty_binary(void)
{
    struct erl_off_heap_header **ohp;
    Uint size = heap_bits_size(0);
    Eterm *hp = erts_global_literal_allocate(size, &ohp);
    ErlOffHeap oh;
    ErtsHeapFactory factory;
    ERTS_INIT_OFF_HEAP(&oh);
    erts_factory_static_init(&factory, hp, size, &oh);
    ERTS_GLOBAL_LIT_EMPTY_BINARY = erts_hfact_new_binary_from_data(&factory, 0, 0,
                                                               (const byte *) "");
    *ohp = oh.first;
    erts_global_literal_register(&ERTS_GLOBAL_LIT_EMPTY_BINARY);
}

void
init_global_literals(ErtsEngine *engine)
{
    ASSERT(engine && !engine->global_literals && !diagnostic_literals);
    engine->global_literals = erts_global_literal_arena_create(GLOBAL_LITERAL_INITIAL_SIZE);
    diagnostic_literals = engine->global_literals;
    diagnostic_literals->bound = 1;
    init_empty_tuple();
    init_empty_binary();
}