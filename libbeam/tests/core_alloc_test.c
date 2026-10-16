/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#include "alloc_internal.h"
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
            for (;count<4;++count) {
                CHECK(lb_alloc_domain_allocate(domain,17,&blocks[count]) ==
                      (count+2==fail?LB_ALLOC_NO_MEMORY:LB_ALLOC_OK));
                if(!blocks[count]) break;
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

static void accounted_scopes(void)
{
    Host host={0}; LbSystemAllocator system={&host,allocate,release};
    LbAllocDomain *root; LbAllocDomain a={0},b={0}; void *p,*q; size_t callbacks;
    CHECK(lb_alloc_domain_create(&system,&root)==LB_ALLOC_OK);
    CHECK(lb_alloc_scope_init(root,&a,128)==LB_ALLOC_OK);
    CHECK(lb_alloc_scope_init(root,&b,256)==LB_ALLOC_OK);
    CHECK(lb_alloc_domain_destroy(root)==LB_ALLOC_BUSY && host.live==1);
    CHECK(lb_alloc_domain_allocate(&a,1,&p)==LB_ALLOC_OK);
    size_t header=lb_alloc_domain_usage(&a).current-1;
    CHECK(header>=sizeof(void *) && header%_Alignof(max_align_t)==0);
    CHECK(lb_alloc_domain_usage(root).current==0); // Shared backing is not double-charged.
    CHECK(lb_alloc_domain_release(&a,p)==LB_ALLOC_OK);
    CHECK(lb_alloc_domain_allocate(&a,128-header,&p)==LB_ALLOC_OK);
    callbacks=host.calls;
    CHECK(lb_alloc_domain_allocate(&a,1,&q)==LB_ALLOC_LIMIT && !q && host.calls==callbacks);
    CHECK(lb_alloc_domain_failure(&a)==LB_ALLOC_LIMIT && lb_alloc_domain_usage(&a).current==128);
    CHECK(lb_alloc_domain_allocate(&b,32,&q)==LB_ALLOC_OK); void *peer=q;
    memset(peer,0x5a,32);
    CHECK(lb_alloc_domain_release(&a,peer)==LB_ALLOC_INVALID); // Peer ownership is unchanged.
    CHECK(lb_alloc_domain_release(&a,p)==LB_ALLOC_OK && lb_alloc_domain_usage(&a).current==0);
    CHECK(lb_alloc_domain_usage(&a).peak==128);
    lb_alloc_domain_begin_operation(&a); host.fail_at=host.calls+1;
    CHECK(lb_alloc_domain_allocate(&a,1,&p)==LB_ALLOC_NO_MEMORY && !p);
    CHECK(lb_alloc_domain_failure(&a)==LB_ALLOC_NO_MEMORY && lb_alloc_domain_usage(&a).current==0);
    host.fail_at=0; lb_alloc_domain_begin_operation(&a);
    CHECK(lb_alloc_domain_allocate(&a,16,&p)==LB_ALLOC_OK);
    callbacks=host.calls;
    CHECK(lb_alloc_domain_allocate(&a,SIZE_MAX,&q)==LB_ALLOC_LIMIT && !q && host.calls==callbacks);
    CHECK(lb_alloc_domain_release(&a,p)==LB_ALLOC_OK);
    CHECK(lb_alloc_domain_allocate(&a,64,&p)==LB_ALLOC_OK);
    callbacks=host.calls;
    CHECK(lb_alloc_domain_allocate(&a,64,&q)==LB_ALLOC_LIMIT && !q && callbacks==host.calls);
    CHECK(lb_alloc_domain_release(&a,p)==LB_ALLOC_OK);
    CHECK(lb_alloc_domain_allocate(&a,64,&p)==LB_ALLOC_OK); // Succeeds after old backing is actually freed.
    CHECK(lb_alloc_domain_release(&a,p)==LB_ALLOC_OK);
    CHECK(lb_alloc_domain_destroy(&a)==LB_ALLOC_OK);
    CHECK(lb_alloc_domain_destroy(&b)==LB_ALLOC_BUSY);
    CHECK(lb_alloc_domain_release(&b,&b)==LB_ALLOC_INVALID);
    CHECK(((unsigned char *)peer)[31]==0x5a && lb_alloc_domain_usage(&b).current==32+header);
    CHECK(lb_alloc_domain_destroy(root)==LB_ALLOC_BUSY);
    CHECK(lb_alloc_domain_release(&b,peer)==LB_ALLOC_OK);
    CHECK(lb_alloc_domain_destroy(&b)==LB_ALLOC_OK && lb_alloc_domain_destroy(root)==LB_ALLOC_OK);
    CHECK(host.live==0);
}
int main(void)
{
    size_t cycle;
    failure_prefixes(); accounted_scopes();
    for (cycle = 0; cycle < 64; ++cycle) {
        Host host = {0};
        LbSystemAllocator system = {&host, allocate, release};
        LbAllocDomain *a, *b;
        void *p, *q, *out;
        CHECK(lb_alloc_domain_create(&system, &a) == LB_ALLOC_OK);
        CHECK(lb_alloc_domain_create(&system, &b) == LB_ALLOC_OK);
        CHECK(a->block_count==0 && b->block_count==0);
        CHECK(lb_alloc_domain_allocate(a, 71, &p) == LB_ALLOC_OK);
        CHECK(lb_alloc_domain_allocate(b, 19, &q) == LB_ALLOC_OK);
        CHECK(a->block_count==1 && b->block_count==1);
        CHECK((uintptr_t)p % _Alignof(max_align_t) == 0);
        memset(q, 0x5a, 19);
        CHECK(lb_alloc_domain_destroy(a) == LB_ALLOC_BUSY && host.live == 4);
        CHECK(lb_alloc_domain_release(b, p) == LB_ALLOC_INVALID);
        CHECK(lb_alloc_domain_release(a, (char *)p + 1) == LB_ALLOC_INVALID);
        CHECK(a->block_count==1 && b->block_count==1);
        CHECK(lb_alloc_domain_allocate(a, SIZE_MAX, &out) == LB_ALLOC_NO_MEMORY && !out);
        CHECK(lb_alloc_domain_allocate(a, 0, &out) == LB_ALLOC_INVALID && !out);
        CHECK(lb_alloc_domain_release(a, p) == LB_ALLOC_OK && a->block_count==0);
        CHECK(lb_alloc_domain_destroy(a) == LB_ALLOC_OK && host.live == 2);
        CHECK(((unsigned char *)q)[18] == 0x5a);
        CHECK(lb_alloc_domain_release(b, q) == LB_ALLOC_OK && b->block_count==0);
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
