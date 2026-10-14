/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Collector preparation tests, not BEAM interpretation on the host thread.
 */
#include "world_internal.h"
#include "process_internal.h"
#include "binary.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"CHECK %d: %s\n",__LINE__,#c); abort(); } } while(0)
typedef struct { size_t allocations,live,calls,fail,watched_frees; void *watched; } Host;
typedef union { max_align_t alignment; size_t bytes; } Header;
static void *allocate(void *context,size_t bytes)
{
    Host *host=context; if(++host->calls==host->fail) return NULL;
    Header *header=malloc(sizeof(*header)+bytes); if(!header) return NULL;
    header->bytes=bytes; ++host->allocations; ++host->live; return header+1;
}
static void release(void *context,void *p)
{
    Host *host=context; Header *header=(Header *)p-1; CHECK(host->live); --host->live;
    /* The domain prepends its own allocation header; watch the containing
     * physical allocation rather than assuming its payload is the callback. */
    if(host->watched && (uintptr_t)host->watched>=(uintptr_t)p &&
       (uintptr_t)host->watched-(uintptr_t)p<header->bytes) {
        memset(p,0xa5,header->bytes); ++host->watched_frees;
    }
    free(header);
}
static LbBytesView text(const char *value) { return (LbBytesView){(const unsigned char *)value,strlen(value)}; }
static LbWorldResourceUsage usage(LbWorld *world)
{ LbWorldResourceUsage result; CHECK(lb_world_resource_usage(world,&result)==LB_WORLD_OK); return result; }
static void stop(LbWorld *world)
{
    LbReclamation *reclamation;
    CHECK(lb_world_stop(world,&reclamation)==LB_WORLD_OK);
    CHECK(lb_reclamation_poll(reclamation)==LB_WORLD_OK && lb_reclamation_consume(reclamation)==LB_WORLD_OK);
    CHECK(usage(world).current==0); lb_reclamation_release(reclamation); lb_world_release(world);
}
static void load(LbWorld *world,const void *bytes,size_t size)
{ CHECK(lb_world_load(world,(LbBytesView){bytes,size})==LB_WORLD_OK); }
static LbProcess *prepared(LbWorld *world)
{
    LbProcess *process; LbCodeEntry *entry; Eterm module,function;
    LbBytesView m=text("memory_slice"),f=text("identity");
    const unsigned char input[4]={1,2,3,4};
    lb_engine_enter(world->engine);
    CHECK(lb_atoms_find(world->space->atoms,m.data,m.size,&module)==LB_ATOM_OK);
    CHECK(lb_atoms_find(world->space->atoms,f.data,f.size,&function)==LB_ATOM_OK);
    CHECK(lb_code_entry_acquire(world->space,module,function,1,&entry)==LB_CODE_OK);
    LbWorkBudget *budget=lb_work_budget_create(world); CHECK(budget);
    LbWorkProcessLease lease; CHECK(lb_work_process_prepare(budget,&lease)==LB_ALLOC_OK);
    CHECK(lb_process_create_binary(entry,input,sizeof(input),1024*1024,&process)==LB_PROCESS_READY);
    lb_work_process_publish(process,&lease); lb_work_budget_release(budget);
    CHECK(!lease.birth && !lease.active && process->work_birth==process->work_active);
    lb_code_entry_release(entry); lb_engine_leave(world->engine); return process;
}
static void destroy(LbWorld *world,LbProcess *process)
{ lb_engine_enter(world->engine); lb_process_destroy(process); lb_engine_leave(world->engine); }
static void check_input(LbProcess *process)
{
    LbBitstringView view;
    CHECK(lb_bitstring_view(process->reg[0],&view) && view.bit_size==32 && view.bit_offset==0);
    CHECK(view.data[0]==1 && view.data[3]==4);
}
static void explicit_gc(LbEngine *engine,Host *host,const void *bytes,size_t size)
{
    for(unsigned exhausted=0;exhausted<2;++exhausted) {
        LbWorld *world; LbResourceOverrides limits={.has_work=1,.invocation_work=exhausted?1:10000000};
        CHECK(lb_world_create_with_overrides(engine,&limits,&world)==LB_WORLD_OK);
        load(world,bytes,size); LbProcess *p=prepared(world);
        lb_engine_enter(engine);
        Eterm *old=p->heap; Eterm root=p->reg[0]; p->construction_root=root; p->heap_reserved=7;
        host->watched=old; host->watched_frees=0;
        size_t allocations=host->allocations;
        lb_work_domain_borrow(&world->domain,p->work_active);
        if(!exhausted) host->fail=host->calls+1;
        CHECK(is_non_value(lb_bif_garbage_collect_0(p,p->reg,NULL)));
        CHECK(p->status==(exhausted?LB_PROCESS_WORK_LIMIT:LB_PROCESS_NO_MEMORY));
        CHECK(p->heap==old && p->reg[0]==root && p->construction_root==root && !p->collections);
        CHECK(!host->watched_frees && host->allocations==allocations && p->heap_reserved==7); check_input(p);
        host->fail=0; lb_work_domain_borrow(&world->domain,NULL);
        lb_work_domain_clear_failure(&world->domain); lb_alloc_domain_begin_operation(&world->domain);
        if(!exhausted) {
            lb_work_domain_borrow(&world->domain,p->work_active); p->status=LB_PROCESS_READY;
            CHECK(lb_bif_garbage_collect_0(p,p->reg,NULL)==am_true);
            CHECK(p->heap!=old && host->watched_frees==1 && p->collections==1 && p->reg[0]==NIL);
            CHECK(p->construction_root!=root && p->heap_reserved==7);
            CHECK((size_t)(p->stop-p->htop)>=7+S_RESERVED);
            p->reg[0]=p->construction_root; check_input(p);
        }
        host->watched=NULL;
        lb_work_domain_borrow(&world->domain,NULL); lb_engine_leave(engine);
        destroy(world,p); stop(world);
    }
    puts("CORE_EXPLICIT_GC_OK moving_side_root=true dead_x_cleared=true physical_release_poisoned=true allocation_retry=true work_before_forwarding=true reservation_preserved=true");
}
int main(int argc,char **argv)
{
    CHECK(argc==1 || argc==2);
    Host host={0}; LbSystemAllocator allocator={&host,allocate,release}; LbEngine *engine; LbWorld *probe;
    CHECK(lb_engine_create(&allocator,&engine)==LB_ENGINE_OK);
    CHECK(lb_world_create(engine,&probe)==LB_WORLD_OK);
    if(argc==1) {
        stop(probe); CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine);
        CHECK(!host.live); puts("CORE_MEMORY_GATE_OK scope_balance=true execution=false"); return 0;
    }
    FILE *file=fopen(argv[1],"rb"); CHECK(file && !fseek(file,0,SEEK_END)); long length=ftell(file);
    CHECK(length>0 && !fseek(file,0,SEEK_SET)); void *bytes=malloc((size_t)length);
    CHECK(bytes && fread(bytes,1,(size_t)length,file)==(size_t)length && !fclose(file));
    load(probe,bytes,(size_t)length); LbProcess *p=prepared(probe); Eterm *old=p->heap;
    lb_engine_enter(engine);
    CHECK(lb_process_collect_live(p,0,1) && p->heap!=old && p->collections==1); check_input(p);
    lb_engine_leave(engine);
    size_t peak=usage(probe).peak; destroy(probe,p); stop(probe);
    LbResourceOverrides limits={1,0,peak,0}; LbWorld *exact,*lower;
    CHECK(lb_world_create_with_overrides(engine,&limits,&exact)==LB_WORLD_OK);
    --limits.memory_bytes;
    CHECK(lb_world_create_with_overrides(engine,&limits,&lower)==LB_WORLD_OK);
    load(exact,bytes,(size_t)length); load(lower,bytes,(size_t)length);
    LbProcess *a=prepared(exact),*b=prepared(lower); old=b->heap; Eterm root=b->reg[0];
    size_t before=usage(lower).current,callbacks=host.allocations;
    lb_engine_enter(engine); lb_alloc_domain_begin_operation(&lower->domain);
    CHECK(!lb_process_collect_live(b,0,1));
    CHECK(lb_alloc_domain_failure(&lower->domain)==LB_ALLOC_LIMIT && host.allocations==callbacks);
    CHECK(b->heap==old && b->reg[0]==root && !b->collections); check_input(b);
    lb_engine_leave(engine); CHECK(usage(lower).current==before);
    lb_engine_enter(engine); CHECK(lb_process_collect_live(a,0,1)); check_input(a); lb_engine_leave(engine);
    CHECK(usage(exact).peak==peak); // Simultaneously retained old/new heaps, not logical live terms.
    destroy(lower,b); destroy(exact,a); stop(lower); stop(exact);
    // Exercise real collector credit before forwarding, independently of memory
    // admission. This is native collector testing, never host BEAM execution.
    LbWorld *work_probe; CHECK(lb_world_create(engine,&work_probe)==LB_WORLD_OK);
    load(work_probe,bytes,(size_t)length); p=prepared(work_probe);
    lb_engine_enter(engine); lb_alloc_domain_begin_operation(&work_probe->domain);
    uint64_t credit=lb_work_budget_usage(p->work_active).remaining;
    lb_work_domain_borrow(&work_probe->domain,p->work_active);
    CHECK(lb_process_collect_live(p,0,1)); check_input(p);
    uint64_t gc_work=credit-lb_work_budget_usage(p->work_active).remaining; CHECK(gc_work>MAX_REG);
    lb_work_domain_borrow(&work_probe->domain,NULL); lb_engine_leave(engine);
    destroy(work_probe,p); stop(work_probe);
    limits=(LbResourceOverrides){.has_work=1,.invocation_work=gc_work};
    CHECK(lb_world_create_with_overrides(engine,&limits,&exact)==LB_WORLD_OK);
    --limits.invocation_work; CHECK(lb_world_create_with_overrides(engine,&limits,&lower)==LB_WORLD_OK);
    load(exact,bytes,(size_t)length); load(lower,bytes,(size_t)length);
    a=prepared(exact); b=prepared(lower); old=b->heap; root=b->reg[0]; callbacks=host.allocations;
    lb_engine_enter(engine); lb_alloc_domain_begin_operation(&lower->domain);
    lb_work_domain_borrow(&lower->domain,b->work_active);
    CHECK(!lb_process_collect_live(b,0,1));
    CHECK(lb_alloc_domain_failure(&lower->domain)==LB_ALLOC_WORK_LIMIT && b->heap==old && b->reg[0]==root && !b->collections);
    check_input(b);
    // A denial at allocation preparation can allocate no block; any prefix
    // before that point is still physically released without forwarding roots.
    CHECK(host.allocations==callbacks);
    lb_work_domain_borrow(&lower->domain,NULL); lb_work_domain_clear_failure(&lower->domain);
    lb_work_domain_borrow(&exact->domain,a->work_active);
    CHECK(lb_process_collect_live(a,0,1) && !lb_work_budget_usage(a->work_active).remaining); check_input(a);
    lb_work_domain_borrow(&exact->domain,NULL); lb_engine_leave(engine);
    destroy(lower,b); destroy(exact,a); stop(lower); stop(exact);
    printf("CORE_WORK_GC_OK exact_units=%llu threshold=true before_forwarding=true roots_preserved=true peer_collects=true physical_release=true execution=false\n",(unsigned long long)gc_work);
    explicit_gc(engine,&host,bytes,(size_t)length); free(bytes);
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine); CHECK(!host.live);
    printf("CORE_MEMORY_GC_OK exact_peak=%zu old_plus_new=true denial_before_forwarding=true roots_preserved=true peer_collects=true final_balance=true execution=false\n",peak);
}
