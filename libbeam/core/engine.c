/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Reversible C ownership for the actual interpreter/native-dispatch substrate.
 * No OTP startup, process table, ethread library, tenant TLS or singleton.
 * A joinable worker is started lazily by actual asynchronous task admission.
 */
#include "code_internal.h"

static void release_catalog(LbEngine *engine)
{
    lb_executor_shutdown(engine);
    lb_release(engine->domain,(void *)engine->bifs);
    engine->bifs=NULL; engine->closed=1;
}
static void release_control(LbEngine *engine)
{
    LbAllocDomain *domain=engine->domain;
    assert(!engine->owner_live && !engine->spaces && !engine->tasks);
    release_catalog(engine);
    lb_release(domain,engine);
    if(lb_alloc_domain_destroy(domain)!=LB_ALLOC_OK) abort();
}
LbEngineStatus lb_engine_create(const LbSystemAllocator *allocator,LbEngine **out)
{
    LbAllocDomain *domain=NULL;
    LbEngine *engine;
    BifEntry *catalog;
    void *memory;
    LbAllocStatus status;
    const unsigned ids[]={BIF_get_module_info_1,BIF_get_module_info_2};
    const LbBifFn functions[]={lb_bif_module_info_1,lb_bif_module_info_2};
    size_t i;
    _Static_assert(sizeof(ids)/sizeof(ids[0])==LB_NATIVE_COUNT,"native catalog size");
    _Static_assert(sizeof(functions)/sizeof(functions[0])==LB_NATIVE_COUNT,"native implementations");
    _Static_assert(BIF_get_module_info_1<BIF_SIZE && BIF_get_module_info_2<BIF_SIZE,"native identities");
    if(!out) return LB_ENGINE_INVALID;
    *out=NULL;
    status=lb_alloc_domain_create(allocator,&domain);
    if(status!=LB_ALLOC_OK) return status==LB_ALLOC_INVALID?LB_ENGINE_INVALID:LB_ENGINE_NO_MEMORY;
    if(lb_alloc_domain_allocate(domain,sizeof(*engine),&memory)!=LB_ALLOC_OK) {
        if(lb_alloc_domain_destroy(domain)!=LB_ALLOC_OK) abort();
        return LB_ENGINE_NO_MEMORY;
    }
    engine=memory; memset(engine,0,sizeof(*engine)); engine->domain=domain;
    if(lb_alloc_domain_allocate(domain,BIF_SIZE*sizeof(BifEntry),&memory)!=LB_ALLOC_OK) {
        release_control(engine); return LB_ENGINE_NO_MEMORY;
    }
    catalog=memory; engine->bifs=catalog; memset(catalog,0,BIF_SIZE*sizeof(*catalog));
    for(i=0;i<LB_NATIVE_COUNT;++i) {
        /* These real implementations may collect. Generated numeric identities
         * are not permissions: only these populated descriptors are admitted. */
        engine->native_ids[i]=ids[i];
        catalog[ids[i]]=(BifEntry){am_erlang,am_get_module_info,(int)i+1,functions[i],BIF_KIND_HEAVY};
    }
    if(lb_executor_init(engine)!=LB_ENGINE_OK) {
        release_control(engine); return LB_ENGINE_NO_MEMORY;
    }
    engine->owner_live=1; *out=engine; return LB_ENGINE_OK;
}
int lb_engine_is_open(const LbEngine *engine)
{
    return engine && engine->owner_live && !engine->closed;
}
LbEngineStatus lb_engine_shutdown(LbEngine *engine)
{
    if(!engine) return LB_ENGINE_INVALID;
    if(!lb_engine_is_open(engine)) return LB_ENGINE_CLOSED;
    if(engine->spaces || engine->tasks || engine->control_borrow) return LB_ENGINE_BUSY;
    release_catalog(engine); return LB_ENGINE_OK;
}
void lb_engine_release(LbEngine *engine)
{
    if(!engine) return;
    if(!engine->owner_live) abort(); /* duplicate consumption of an internal handle */
    engine->owner_live=0;
    lb_engine_release_if_detached(engine);
}
void lb_engine_release_if_detached(LbEngine *engine)
{
    if(!engine->owner_live && !engine->spaces && !engine->tasks && !engine->control_borrow) release_control(engine);
}
void lb_engine_space_published(LbEngine *engine)
{
    if(!lb_engine_is_open(engine) || engine->spaces==SIZE_MAX) abort();
    ++engine->spaces;
}
void lb_engine_space_released(LbEngine *engine)
{
    if(!engine->spaces) abort();
    --engine->spaces;
    lb_engine_release_if_detached(engine);
}
