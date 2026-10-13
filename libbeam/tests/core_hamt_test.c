/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#include "process_internal.h"
#include "map_hamt.h"
#include "flatmap.h"
#include "term_compare.h"
#include "world_internal.h"
#include "work_budget_internal.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while(0)
typedef struct { LbAllocDomain *domain; void *blocks[512]; size_t count,fail; } Arena;
static Eterm *allocate(void *context,size_t words)
{
    Arena *arena=context; void *memory=NULL;
    if(arena->count==arena->fail) return NULL;
    CHECK(arena->count<512);
    CHECK(lb_alloc_domain_allocate(arena->domain,words*sizeof(Eterm),&memory)==LB_ALLOC_OK);
    arena->blocks[arena->count++]=memory; return memory;
}
static void clear(Arena *arena)
{ while(arena->count) CHECK(lb_alloc_domain_release(arena->domain,arena->blocks[--arena->count])==LB_ALLOC_OK); }
static Eterm bytes(Eterm *storage,size_t size)
{
    Eterm tail=NIL;
    for(size_t i=size;i;--i) { storage[2*(i-1)]=make_small(96+i); storage[2*(i-1)+1]=tail; tail=make_list(storage+2*(i-1)); }
    return tail;
}
static void pairs_init(LbHamtPair *pairs)
{
    for(size_t i=0;i<64;++i) {
        pairs[i].key=make_small(i); pairs[i].value=make_small(i*3);
        CHECK(lb_hamt_hash_key(pairs[i].key,&pairs[i].hash)==LB_HAMT_OK);
    }
}
static void work_boundaries(void)
{
    LbEngine *engine; LbWorld *world; LbReclamation *reclamation;
    CHECK(lb_engine_create(NULL,&engine)==LB_ENGINE_OK);
    CHECK(lb_world_create(engine,&world)==LB_WORLD_OK);
    lb_engine_enter(engine);
    size_t baseline=lb_alloc_domain_usage(&world->domain).current;
    Arena arena={.domain=&world->domain,.fail=SIZE_MAX};
    LbHamtPair pairs[64]; Eterm map,value;
    pairs_init(pairs);
    /* Add one byte-list key so lookup must charge hashing as well as traversal. */
    Eterm storage[10],key=bytes(storage,5);
    pairs[0].key=key; CHECK(lb_hamt_hash_key(key,&pairs[0].hash)==LB_HAMT_OK);
    CHECK(lb_hamt_build(pairs,64,allocate,&arena,0,&map)==LB_HAMT_OK);
    LbCodeSpace space={.domain=&world->domain}; LbCodeModule module={.space=&space};
    LbProcess process={.entry_module=&module,.status=LB_PROCESS_READY};
    LbWorkBudget *budget=lb_work_budget_create(world); CHECK(budget);
    uint64_t limit=budget->remaining;
    lb_work_domain_borrow(&world->domain,budget);
    CHECK(lb_hamt_get(&process,map,key,&value)==LB_HAMT_OK && value==make_small(0));
    uint64_t required=limit-budget->remaining; CHECK(required>5);
    lb_work_domain_borrow(&world->domain,NULL); lb_work_budget_release(budget);
    for(uint64_t credit=0;credit<=required;++credit) {
        budget=lb_work_budget_create(world); CHECK(budget);
        budget->limit=budget->remaining=credit;
        lb_work_domain_borrow(&world->domain,budget);
        process.status=LB_PROCESS_READY; value=make_small(123);
        LbHamtStatus status=lb_hamt_get(&process,map,key,&value);
        CHECK(status==(credit==required?LB_HAMT_OK:LB_HAMT_WORK_LIMIT));
        if(credit==required) CHECK(value==make_small(0) && budget->remaining==0);
        else CHECK(is_non_value(value) && budget->exhausted);
        lb_work_domain_borrow(&world->domain,NULL);
        lb_work_domain_clear_failure(&world->domain);
        lb_alloc_domain_begin_operation(&world->domain);
        lb_work_budget_release(budget);
    }
    clear(&arena);
    CHECK(lb_alloc_domain_usage(&world->domain).current==baseline);
    lb_engine_leave(engine);
    CHECK(lb_world_stop(world,&reclamation)==LB_WORLD_OK);
    CHECK(lb_reclamation_poll(reclamation)==LB_WORLD_OK);
    CHECK(lb_reclamation_consume(reclamation)==LB_WORLD_OK);
    lb_reclamation_release(reclamation); lb_world_release(world);
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine);
}
int main(void)
{
    /* Independent OTP 28.4.1 erts_debug internal_hash observations. */
    Eterm storage[10]; uint64_t hash;
    const uint64_t expected[]={UINT64_C(12819940937779820664),UINT64_C(3855468832738538248),UINT64_C(9652828623497655895)};
    const size_t lengths[]={1,4,5};
    for(size_t i=0;i<3;++i) { CHECK(lb_hamt_hash_key(bytes(storage,lengths[i]),&hash)==LB_HAMT_OK); CHECK(hash==expected[i]); }
    CHECK(lb_hamt_hash_key(make_small(0),&hash)==LB_HAMT_OK && hash==UINT64_C(15677855740172624429));
    CHECK(lb_hamt_hash_key(NIL,&hash)==LB_HAMT_OK && hash==UINT64_C(16046243290777390298));
    LbAllocDomain *domain; CHECK(lb_alloc_domain_create(NULL,&domain)==LB_ALLOC_OK);
    Arena arena={.domain=domain,.fail=SIZE_MAX}; LbHamtPair pairs[64]; Eterm map,value;
    pairs_init(pairs); CHECK(lb_hamt_build(pairs,64,allocate,&arena,0,&map)==LB_HAMT_OK);
    CHECK(lb_hamt_size(map)==64);
    LbCodeSpace space={.domain=domain}; LbCodeModule module={.space=&space}; LbProcess process={.entry_module=&module,.status=LB_PROCESS_READY};
    for(size_t i=0;i<64;++i) {
        CHECK(lb_hamt_get(&process,map,make_small(i),&value)==LB_HAMT_OK && value==make_small(i*3));
        /* Canonical tries terminate singleton branches immediately. */
        CHECK(lb_hamt_hash_key(make_small(i),&hash)==LB_HAMT_OK);
        Eterm node=map;
        for(unsigned depth=0;;++depth) {
            CHECK(depth<16); Eterm *p=boxed_val(node); unsigned bitmap=(unsigned)(p[0]>>16)&65535,slot=(hash>>(4*depth))&15,index=0;
            CHECK(bitmap&(1u<<slot));
            for(unsigned bit=0;bit<slot;++bit) index+=(bitmap>>bit)&1;
            node=p[lb_map_object_child_offset(p)+index];
            if(is_list(node)) { CHECK(list_val(node)[0]==make_small(i)); break; }
        }
    }
    CHECK(lb_hamt_get(&process,map,make_small(999),&value)==LB_HAMT_MISSING);
    size_t count=arena.count; CHECK(count<150);
    /* Copy into owned heap, reclaim the source arena, then move every node. */
    size_t words=0; CHECK(lb_flat_size(domain,map,&words));
    CHECK(lb_alloc_domain_allocate(domain,(words+S_RESERVED+16)*sizeof(Eterm),(void **)&process.heap)==LB_ALLOC_OK);
    process.htop=process.heap; process.hend=process.heap+words+S_RESERVED+16; process.stop=process.hend;
    process.fvalue=process.exception_value=process.construction_root=process.receive_root=NIL;
    for(size_t i=0;i<MAX_REG;++i) process.reg[i]=NIL;
    process.reg[0]=lb_copy_flat(map,&process.htop,&process.off_heap);
    CHECK(!is_non_value(process.reg[0]) && (size_t)(process.htop-process.heap)==words);
    int equal=0; CHECK(lb_term_equal(domain,map,process.reg[0],&equal)==LB_ALLOC_OK && equal);
    clear(&arena);
    CHECK(lb_process_collect_live(&process,1,1));
    for(size_t i=0;i<64;++i)
        CHECK(lb_hamt_get(&process,process.reg[0],make_small(i),&value)==LB_HAMT_OK && value==make_small(i*3));
    CHECK(lb_alloc_domain_release(domain,process.heap)==LB_ALLOC_OK);
    /* Every allocator prefix remains private and can be physically reclaimed. */
    for(size_t fail=0;fail<count;++fail) {
        arena.fail=fail; pairs_init(pairs); map=make_small(123);
        CHECK(lb_hamt_build(pairs,64,allocate,&arena,0,&map)==LB_HAMT_NO_MEMORY);
        CHECK(map==make_small(123)); clear(&arena);
    }
    arena.fail=SIZE_MAX; pairs_init(pairs); pairs[1].hash=pairs[0].hash;
    CHECK(lb_hamt_build(pairs,64,allocate,&arena,0,&map)==LB_HAMT_UNSUPPORTED && arena.count==0);
    CHECK(lb_alloc_domain_destroy(domain)==LB_ALLOC_OK);
    work_boundaries();
    puts("CORE_HAMT_OK hash_oracle=true low_nibble_first=true singleton_leaves=true lookup=true copy_gc=true work_threshold=true allocation_prefixes=true collision_refusal=true");
    return 0;
}
