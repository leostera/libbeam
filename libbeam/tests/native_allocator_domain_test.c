/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#define ERTS_WANT_MEM_MAPPERS 1
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "sys.h"
#include "erl_vm.h"
#include "global.h"
#include "erl_embed.h"
#include "erl_engine.h"
#include "erl_allocator_domain.h"
#include "erl_mmap.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "allocator ownership check failed at %d: %s\n", __LINE__, #c); _Exit(2); } } while (0)

int main(int argc, char **argv)
{
    ErtsEngine *engine = erl_engine_alloc();
    ErtsAllocatorOwnership baseline, current;
    int i;
    CHECK(engine && erl_prepare_runtime(engine, argc, argv) == 0);
    CHECK(engine->allocators && erts_allocator_ownership(engine, &baseline) == 0);
    CHECK(baseline.control_bytes && baseline.backing_bytes && baseline.started_instances);
    CHECK(baseline.permanent_blocks && baseline.permanent_bytes);
    CHECK(erts_allocator_domain_discard(engine->allocators) == 1);
    CHECK(erts_allocator_domain_discard(NULL) == 1);
#if HAVE_ERTS_MSEG
    CHECK(erts_dflt_mmapper);
    CHECK(erts_allocator_bootstrap_release(engine->allocators, erts_dflt_mmapper) == 1);
#endif
    for (i = 0; i < 32; ++i) {
        ErtsEngine *foreign = erl_engine_alloc();
        ErtsAllocatorDomain *peer = erts_allocator_domain_create();
        Uint alignment = i & 1 ? 4096 : 64;
        Uint size = 37 + i * 129;
        unsigned char *p, *ordinary, *bootstrap;
        CHECK(foreign && peer);
        foreign->allocators = erts_allocator_domain_create();
        CHECK(foreign->allocators);
        CHECK(erl_engine_discard_uninitialized(foreign) == 1);
        CHECK(erts_allocator_ownership(foreign, &current) == 0);
        CHECK(!current.backing_bytes && !current.started_instances && !current.permanent_blocks);
        CHECK(!erts_allocator_bootstrap_alloc(foreign->allocators, 10, 3) && errno == EINVAL);
        CHECK(!erts_allocator_bootstrap_alloc(foreign->allocators, (UWord)-1, 64) && errno == ENOMEM);
        bootstrap = erts_allocator_bootstrap_alloc(foreign->allocators, 4096, 128);
        CHECK(bootstrap && !((UWord)bootstrap & 127));
        CHECK(bootstrap[0] == 0 && bootstrap[4095] == 0);
        sys_memset(bootstrap, 0x31, 4096);
        CHECK(erts_allocator_domain_discard(foreign->allocators) == 1);
        CHECK(erts_allocator_bootstrap_release(peer, bootstrap) == 1);
        CHECK(erts_allocator_bootstrap_release(foreign->allocators, bootstrap + 1) == 1);
        CHECK(erts_allocator_ownership(foreign, &current) == 0);
        CHECK(current.bootstrap_blocks == 1 && current.backing_bytes == 4096 + 127);
        CHECK(bootstrap[4095] == 0x31);
        CHECK(erts_allocator_bootstrap_release(foreign->allocators, bootstrap) == 0);
        CHECK(erts_allocator_bootstrap_release(foreign->allocators, bootstrap) == 1);
        p = erts_alloc_permanent_aligned(ERTS_ALC_T_POLLSET, size, alignment);
        CHECK(p && !((UWord)p & (alignment - 1)));
        sys_memset(p, 0x5a, size);
        CHECK(erts_allocator_ownership(engine, &current) == 0);
        CHECK(current.permanent_blocks == baseline.permanent_blocks + 1);
        CHECK(current.permanent_bytes == baseline.permanent_bytes + size + alignment - 1);
        CHECK(erts_allocator_release_permanent(foreign, ERTS_ALC_T_POLLSET, p) == 1);
        CHECK(erts_allocator_release_permanent(engine, ERTS_ALC_T_BINARY, p) == 1);
        CHECK(erts_allocator_release_permanent(engine, ERTS_ALC_T_POLLSET, p + 1) == 1);
        CHECK(p[0] == 0x5a && p[size - 1] == 0x5a);
        CHECK(erts_allocator_domain_discard(peer) == 0);
        CHECK(erts_allocator_domain_discard(foreign->allocators) == 0);
        foreign->allocators = NULL;
        CHECK(erl_engine_discard_uninitialized(foreign) == 0);
        CHECK(erts_allocator_release_permanent(engine, ERTS_ALC_T_POLLSET, p) == 0);
        CHECK(erts_allocator_release_permanent(engine, ERTS_ALC_T_POLLSET, p) == 1);
        CHECK(erts_allocator_ownership(engine, &current) == 0);
        CHECK(current.permanent_blocks == baseline.permanent_blocks);
        CHECK(current.permanent_bytes == baseline.permanent_bytes);
        /* Real dispatch and backing instances, including a single-block-sized
         * allocation and debug-wrapper realloc/type checking. */
        ordinary = erts_alloc(ERTS_ALC_T_BINARY, 3 * 1024 * 1024);
        sys_memset(ordinary, 0x73, 3 * 1024 * 1024);
        ordinary = erts_realloc(ERTS_ALC_T_BINARY, ordinary, 5 * 1024 * 1024);
        CHECK(ordinary[0] == 0x73 && ordinary[3 * 1024 * 1024 - 1] == 0x73);
        erts_free(ERTS_ALC_T_BINARY, ordinary);
    }
    {
        ErtsAllocatorDomain *held[4096];
        int round, count, capacity = -1;
        /* Real TLS-key exhaustion: failed construction must release its
         * mutex/backing, and successful domains must return their keys. */
        for (round = 0; round < 2; ++round) {
            for (count = 0; count < 4096; ++count) {
                errno = 0;
                held[count] = erts_allocator_domain_create();
                if (!held[count]) break;
            }
            CHECK(count > 0 && count < 4096 && errno == EAGAIN);
            if (capacity < 0) capacity = count;
            CHECK(count == capacity);
            while (count) CHECK(erts_allocator_domain_discard(held[--count]) == 0);
        }
        held[0] = erts_allocator_domain_create();
        CHECK(held[0] && erts_allocator_domain_discard(held[0]) == 0);
    }
    CHECK(erts_allocator_domain_discard(engine->allocators) == 1);
    puts("NATIVE_ALLOCATOR_DOMAIN_OK owned_dispatch=true retained_bases=true permanent_release=true engine_shutdown=false");
    fflush(stdout);
    _Exit(0);
}
