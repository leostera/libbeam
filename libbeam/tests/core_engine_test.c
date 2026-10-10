/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#include "code_internal.h"
#include <stdio.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"CHECK %s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while(0)
typedef struct { size_t calls,fail,live,bytes; } Memory;
typedef union { max_align_t alignment; size_t size; } Header;
static void *allocate(void *context,size_t size)
{
    Memory *m=context; Header *h;
    if(++m->calls==m->fail) return NULL;
    h=malloc(sizeof(*h)+size); if(!h) return NULL;
    h->size=size; ++m->live; m->bytes+=size; return h+1;
}
static void release(void *context,void *pointer)
{
    Memory *m=context; Header *h=(Header *)pointer-1;
    CHECK(m->live && m->bytes>=h->size); --m->live; m->bytes-=h->size;
    memset(pointer,0xa5,h->size); free(h);
}
static void repeat_lifetime(void)
{
    Memory m={0}; LbSystemAllocator cb={&m,allocate,release}; LbEngine *engine;
    LbCodeSpace *space; size_t i,steps;
    CHECK(lb_engine_create(NULL,NULL)==LB_ENGINE_INVALID);
    CHECK(lb_engine_shutdown(NULL)==LB_ENGINE_INVALID && !lb_engine_is_open(NULL));
    lb_engine_release(NULL);
    CHECK(lb_engine_create(&cb,&engine)==LB_ENGINE_OK); steps=m.calls;
    CHECK(!engine->spaces && engine->bifs && lb_engine_is_open(engine));
    CHECK(engine->bifs[BIF_get_module_info_1].f==lb_bif_module_info_1);
    CHECK(engine->bifs[BIF_get_module_info_2].f==lb_bif_module_info_2);
    CHECK(!engine->bifs[BIF_error_1].f); /* identity metadata grants no native capability */
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK && !engine->bifs && !lb_engine_is_open(engine));
    CHECK(m.live==2); /* finite control plus its bookkeeping domain, not execution resources */
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_CLOSED);
    CHECK(lb_code_space_create(engine,&space)==LB_CODE_CLOSED && !space);
    lb_engine_release(engine); CHECK(!m.live && !m.bytes);
    for(i=1;i<=steps;++i) {
        m.fail=m.calls+i;
        CHECK(lb_engine_create(&cb,&engine)==LB_ENGINE_NO_MEMORY && !engine && !m.live && !m.bytes);
        m.fail=0;
        CHECK(lb_engine_create(&cb,&engine)==LB_ENGINE_OK);
        CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine);
        CHECK(!m.live && !m.bytes);
    }
    {
        LbEngine *survivor; size_t live,bytes;
        CHECK(lb_engine_create(&cb,&survivor)==LB_ENGINE_OK);
        CHECK(lb_code_space_create(survivor,&space)==LB_CODE_OK); live=m.live; bytes=m.bytes;
        for(i=1;i<=steps;++i) {
            m.fail=m.calls+i;
            CHECK(lb_engine_create(&cb,&engine)==LB_ENGINE_NO_MEMORY && !engine);
            CHECK(m.live==live && m.bytes==bytes && lb_engine_is_open(survivor) && survivor->spaces==1);
            m.fail=0; CHECK(lb_engine_create(&cb,&engine)==LB_ENGINE_OK); lb_engine_release(engine);
            CHECK(m.live==live && m.bytes==bytes);
        }
        CHECK(lb_code_space_destroy(space)==LB_CODE_OK); lb_engine_release(survivor);
        CHECK(!m.live && !m.bytes);
    }
    for(i=0;i<32;++i) {
        LbEngine *peer; size_t live,bytes;
        CHECK(lb_engine_create(&cb,&peer)==LB_ENGINE_OK); live=m.live; bytes=m.bytes;
        CHECK(lb_engine_create(&cb,&engine)==LB_ENGINE_OK); lb_engine_release(engine);
        CHECK(m.live==live && m.bytes==bytes && lb_engine_is_open(peer));
        lb_engine_release(peer); CHECK(!m.live && !m.bytes);
    }
    cb.release=NULL;
    CHECK(lb_engine_create(&cb,&engine)==LB_ENGINE_INVALID && !engine && !m.live);
    printf("CORE_ENGINE_LIFETIME_OK failure_prefixes=%zu repeated=true no_singleton=true no_implicit_world=true\n",steps);
}
static void children_and_failure(void)
{
    Memory m={0}; LbSystemAllocator cb={&m,allocate,release}; LbEngine *engine;
    LbCodeSpace *first,*second; size_t i,steps,live,bytes;
    CHECK(lb_engine_create(&cb,&engine)==LB_ENGINE_OK); live=m.live; bytes=m.bytes;
    steps=m.calls; CHECK(lb_code_space_create(engine,&first)==LB_CODE_OK); steps=m.calls-steps;
    CHECK(first->engine==engine && engine->spaces==1);
    CHECK(first->natives[0].trampoline.address && first->natives[1].trampoline.address);
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_BUSY && lb_engine_is_open(engine));
    CHECK(lb_code_space_destroy(first)==LB_CODE_OK && engine->spaces==0);
    CHECK(m.live==live && m.bytes==bytes);
    engine->spaces=SIZE_MAX;
    CHECK(lb_code_space_create(engine,&first)==LB_CODE_LIMIT && !first);
    CHECK(m.live==live && m.bytes==bytes); engine->spaces=0;
    for(i=1;i<=steps;++i) {
        m.fail=m.calls+i;
        CHECK(lb_code_space_create(engine,&first)==LB_CODE_NO_MEMORY && !first && !engine->spaces);
        CHECK(m.live==live && m.bytes==bytes && lb_engine_is_open(engine)); m.fail=0;
        CHECK(lb_code_space_create(engine,&first)==LB_CODE_OK && engine->spaces==1);
        CHECK(lb_code_space_destroy(first)==LB_CODE_OK && !engine->spaces);
        CHECK(m.live==live && m.bytes==bytes);
    }
    CHECK(lb_code_space_create(engine,&first)==LB_CODE_OK && lb_code_space_create(engine,&second)==LB_CODE_OK);
    CHECK(first->atoms!=second->atoms && first->engine==second->engine && engine->spaces==2);
    CHECK(lb_atoms_retain(first->atoms)==LB_ATOM_OK);
    CHECK(lb_code_space_destroy(first)==LB_CODE_BUSY && engine->spaces==2);
    CHECK(lb_atoms_release(first->atoms)==LB_ATOM_OK);
    /* Out-of-order owner drop: no force-free, no catalog loss, no resurrection. */
    lb_engine_release(engine);
    CHECK(!lb_engine_is_open(engine) && engine->bifs && engine->spaces==2);
    { LbCodeSpace *extra; CHECK(lb_code_space_create(engine,&extra)==LB_CODE_CLOSED && !extra); }
    CHECK(lb_code_space_destroy(first)==LB_CODE_OK && engine->spaces==1);
    CHECK(lb_code_space_destroy(second)==LB_CODE_OK && !m.live && !m.bytes);
    printf("CORE_ENGINE_CHILDREN_OK construction_failure_prefixes=%zu retry_each=true shared_catalog=true deferred_owner_drop=true\n",steps);
}
int main(void)
{
    repeat_lifetime(); children_and_failure(); return 0;
}
