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
typedef struct { size_t allocations,live; } Host;
static void *allocate(void *context,size_t bytes)
{
    Host *host=context; void *p=malloc(bytes);
    if(p) { ++host->allocations; ++host->live; } return p;
}
static void release(void *context,void *p)
{ Host *host=context; CHECK(host->live); --host->live; free(p); }
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
    load(exact,bytes,(size_t)length); load(lower,bytes,(size_t)length); free(bytes);
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
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine); CHECK(!host.live);
    printf("CORE_MEMORY_GC_OK exact_peak=%zu old_plus_new=true denial_before_forwarding=true roots_preserved=true peer_collects=true final_balance=true execution=false\n",peak);
}
