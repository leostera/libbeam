/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#include "alloc.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%d: %s\n", __LINE__, #c); abort(); } } while (0)

typedef struct {
    size_t calls, fail_at, live;
    void *owned[16];
} Host;
static void *allocate(void *context, size_t size)
{
    Host *host = context;
    void *p;
    if (++host->calls == host->fail_at) return NULL;
    p = malloc(size);
    CHECK(p && host->live < 16);
    host->owned[host->live++] = p;
    return p;
}
static void release(void *context, void *p)
{
    Host *host = context;
    size_t i;
    for (i = 0; i < host->live; ++i) {
        if (host->owned[i] == p) {
            host->owned[i] = host->owned[--host->live];
            free(p);
            return;
        }
    }
    CHECK(0); /* wrong allocator, interior pointer or repeated physical free */
}

static void failure_prefixes(void)
{
    size_t fail;
    for (fail = 1; fail <= 5; ++fail) {
        Host host = {0};
        LbSystemAllocator system = {&host, allocate, release};
        LbAllocDomain *domain = NULL;
        void *blocks[4] = {0};
        size_t count = 0;
        LbAllocStatus status;
        host.fail_at = fail;
        status = lb_alloc_domain_create(&system, &domain);
        CHECK(status == (fail == 1 ? LB_ALLOC_NO_MEMORY : LB_ALLOC_OK));
        if (status == LB_ALLOC_OK) {
            for (; count < 4; ++count) {
                CHECK(lb_alloc_domain_allocate(domain, 17, &blocks[count]) ==
                      (count + 2 == fail ? LB_ALLOC_NO_MEMORY : LB_ALLOC_OK));
                if (!blocks[count]) break;
            }
            while (count) CHECK(lb_alloc_domain_release(domain, blocks[--count]) == LB_ALLOC_OK);
            CHECK(lb_alloc_domain_destroy(domain) == LB_ALLOC_OK);
        } else {
            CHECK(fail == 1 && domain == NULL);
        }
        CHECK(host.live == 0);
        /* Real retry with the same allocator context, not a fresh process. */
        host.fail_at = 0;
        CHECK(lb_alloc_domain_create(&system, &domain) == LB_ALLOC_OK);
        CHECK(lb_alloc_domain_allocate(domain, 32, &blocks[0]) == LB_ALLOC_OK);
        CHECK(lb_alloc_domain_release(domain, blocks[0]) == LB_ALLOC_OK);
        CHECK(lb_alloc_domain_destroy(domain) == LB_ALLOC_OK && host.live == 0);
    }
}

int main(void)
{
    size_t cycle;
    failure_prefixes();
    for (cycle = 0; cycle < 64; ++cycle) {
        Host host = {0};
        LbSystemAllocator system = {&host, allocate, release};
        LbAllocDomain *a, *b;
        void *p, *q, *out;
        CHECK(lb_alloc_domain_create(&system, &a) == LB_ALLOC_OK);
        CHECK(lb_alloc_domain_create(&system, &b) == LB_ALLOC_OK);
        CHECK(lb_alloc_domain_allocate(a, 71, &p) == LB_ALLOC_OK);
        CHECK(lb_alloc_domain_allocate(b, 19, &q) == LB_ALLOC_OK);
        CHECK((uintptr_t)p % _Alignof(max_align_t) == 0);
        memset(q, 0x5a, 19);
        CHECK(lb_alloc_domain_destroy(a) == LB_ALLOC_BUSY && host.live == 4);
        CHECK(lb_alloc_domain_release(b, p) == LB_ALLOC_INVALID);
        CHECK(lb_alloc_domain_release(a, (char *)p + 1) == LB_ALLOC_INVALID);
        CHECK(lb_alloc_domain_allocate(a, SIZE_MAX, &out) == LB_ALLOC_NO_MEMORY && !out);
        CHECK(lb_alloc_domain_allocate(a, 0, &out) == LB_ALLOC_INVALID && !out);
        CHECK(lb_alloc_domain_release(a, p) == LB_ALLOC_OK);
        CHECK(lb_alloc_domain_destroy(a) == LB_ALLOC_OK && host.live == 2);
        CHECK(((unsigned char *)q)[18] == 0x5a);
        CHECK(lb_alloc_domain_release(b, q) == LB_ALLOC_OK);
        CHECK(lb_alloc_domain_destroy(b) == LB_ALLOC_OK && host.live == 0);
    }
    {
        LbAllocDomain *domain;
        LbSystemAllocator invalid = {0};
        CHECK(lb_alloc_domain_create(&invalid, &domain) == LB_ALLOC_INVALID && !domain);
        CHECK(lb_alloc_domain_create(NULL, &domain) == LB_ALLOC_OK);
        CHECK(lb_alloc_domain_destroy(domain) == LB_ALLOC_OK);
    }
    puts("CORE_ALLOC_OK failure_prefixes=true retry=true peer_survival=true engine_lifecycle=false");
    return 0;
}
