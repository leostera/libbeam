/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#define _POSIX_C_SOURCE 200809L
#include "world_internal.h"
#include "process_internal.h"
#include "registry.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"GC roots %s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while(0)
typedef struct { size_t live,bytes; uintptr_t watched_heap; int watched_heap_released; } Memory;
typedef union { max_align_t alignment; size_t size; } Header;
static void *allocate(void *context,size_t size)
{
    Memory *m=context; Header *h=malloc(sizeof(*h)+size);
    if(!h) return NULL;
    h->size=size; ++m->live; m->bytes+=size; return h+1;
}
static void release(void *context,void *pointer)
{
    Memory *m=context; Header *h=(Header *)pointer-1;
    CHECK(m->live && m->bytes>=h->size); --m->live; m->bytes-=h->size;
    uintptr_t base=(uintptr_t)pointer;
    if(m->watched_heap>=base && m->watched_heap-base<h->size) m->watched_heap_released=1;
    memset(pointer,0xa5,h->size); free(h);
}
static LbBytesView text(const char *s) { return (LbBytesView){(const unsigned char *)s,strlen(s)}; }
static void tick(void) { const struct timespec delay={0,1000000}; nanosleep(&delay,NULL); }
static LbBytesView await(LbCall *call)
{
    LbBytesView result; LbWorldStatus status=LB_WORLD_PENDING;
    for(unsigned i=0;i<10000 && status==LB_WORLD_PENDING;++i) {
        status=lb_call_poll(call,&result); if(status==LB_WORLD_PENDING) tick();
    }
    if(status!=LB_WORLD_OK) fprintf(stderr,"GC root completion status=%d\n",status);
    CHECK(status==LB_WORLD_OK); return result;
}
static void consume(LbCall *call)
{
    CHECK(lb_call_consume(call)==LB_WORLD_OK && lb_call_consume(call)==LB_WORLD_CLOSED);
    lb_call_release(call);
}
static Eterm dictionary_value(const LbProcess *p,Eterm key)
{
    if(!p->dictionary) return THE_NON_VALUE;
    for(size_t i=0;i<LB_PD_SLOTS;++i) {
        Eterm bucket=p->dictionary->data[i];
        if(is_tuple(bucket)) {
            Eterm *pair=tuple_val(bucket);
            if(arityval(pair[0])==2 && pair[1]==key) return pair[2];
        } else while(is_list(bucket)) {
            Eterm pair=CAR(list_val(bucket)); bucket=CDR(list_val(bucket));
            CHECK(is_tuple(pair) && arityval(tuple_val(pair)[0])==2);
            if(tuple_val(pair)[1]==key) return tuple_val(pair)[2];
        }
    }
    return THE_NON_VALUE;
}
static size_t await_phase(LbWorld *world,LbCall *call,Memory *memory,const char *phase,int require_collection,size_t before)
{
    for(unsigned i=0;i<10000;++i) {
        lb_engine_enter(world->engine);
        Eterm name,key,wanted; Uint cost=0;
        CHECK(lb_atoms_find(world->space->atoms,"gc_probe",8,&name)==LB_ATOM_OK);
        CHECK(lb_atoms_find(world->space->atoms,"phase",5,&key)==LB_ATOM_OK);
        CHECK(lb_atoms_find(world->space->atoms,phase,strlen(phase),&wanted)==LB_ATOM_OK);
        LbProcess *p=lb_registry_lookup(world->space,name,&cost);
        int ready=p && p->waiting && dictionary_value(p,key)==wanted;
        size_t collections=p?p->collections:0;
        LbWorldStatus status=call->status;
        if(ready && !require_collection) {
            memory->watched_heap=(uintptr_t)p->heap;
            memory->watched_heap_released=0;
        }
        lb_engine_leave(world->engine);
        CHECK(status==LB_WORLD_PENDING);
        if(ready && (!require_collection || collections>before)) return collections;
        tick();
    }
    CHECK(0); return 0;
}
static void command(LbWorld *world,const char *name)
{
    LbCall *call; CHECK(lb_world_call(world,text("gc_roots"),text(name),(LbBytesView){NULL,0},&call)==LB_WORLD_OK);
    LbBytesView result=await(call); CHECK(result.size==strlen(name) && !memcmp(result.data,name,result.size));
    consume(call);
}
static void stop(LbWorld *world)
{
    LbReclamation *receipt; CHECK(lb_world_stop(world,&receipt)==LB_WORLD_OK);
    LbWorldStatus status=LB_WORLD_PENDING;
    for(unsigned i=0;i<10000 && status==LB_WORLD_PENDING;++i) {
        status=lb_reclamation_poll(receipt); if(status==LB_WORLD_PENDING) tick();
    }
    CHECK(status==LB_WORLD_OK && lb_reclamation_consume(receipt)==LB_WORLD_OK);
    lb_reclamation_release(receipt);
    LbWorldResourceUsage usage; CHECK(lb_world_resource_usage(world,&usage)==LB_WORLD_OK);
    CHECK(usage.closed && !world->space);
}
int main(int argc,char **argv)
{
    CHECK(argc==3);
    FILE *file=fopen(argv[1],"rb"); CHECK(file && !fseek(file,0,SEEK_END));
    long length=ftell(file); CHECK(length>0 && length<1024*1024 && !fseek(file,0,SEEK_SET));
    unsigned char *beam=malloc((size_t)length); CHECK(beam);
    CHECK(fread(beam,1,(size_t)length,file)==(size_t)length && !fclose(file));
    Memory memory={0}; LbSystemAllocator allocator={&memory,allocate,release};
    LbEngine *engine; LbWorld *world,*peer; LbCall *root,*other;
    CHECK(lb_engine_create(&allocator,&engine)==LB_ENGINE_OK);
    CHECK(lb_world_create(engine,&world)==LB_WORLD_OK && lb_world_create(engine,&peer)==LB_WORLD_OK);
    LbWorld *worlds[2]={world,peer};
    for(size_t i=0;i<2;++i) {
        LbWorldLoadReport report;
        LbWorldStatus status=lb_world_load_report(worlds[i],(LbBytesView){beam,(size_t)length},&report);
        if(status!=LB_WORLD_OK) fprintf(stderr,"GC root %s load status=%d stage=%s import=%.*s:%.*s/%u\n",argv[2],status,
            report.stage?report.stage:"",(int)report.module_size,report.module,(int)report.function_size,report.function,report.arity);
        CHECK(status==LB_WORLD_OK);
    }
    memset(beam,0xa5,(size_t)length); free(beam);
    unsigned char input[8192],expected[8192];
    for(size_t i=0;i<sizeof(input);++i) input[i]=(unsigned char)i;
    memcpy(expected,input,sizeof(input));
    CHECK(lb_world_start(world,text("gc_roots"),text(argv[2]),(LbBytesView){input,sizeof(input)},&root)==LB_WORLD_OK);
    memset(input,0xcc,sizeof(input));
    size_t before=await_phase(world,root,&memory,"before_gc",0,0);
    CHECK(lb_world_start(peer,text("gc_roots"),text("identity"),text("peer"),&other)==LB_WORLD_OK);
    LbBytesView value=await(other); CHECK(value.size==4 && !memcmp(value.data,"peer",4)); consume(other);
    command(world,"collect");
    int hibernating=!strncmp(argv[2],"hibernate_",10);
    size_t after=await_phase(world,root,&memory,hibernating?"before_gc":"after_gc",1,before);
    CHECK(after>before);
    lb_engine_enter(engine);
    /* Counter increments alone cannot fake collection: the old heap must
     * actually retire and be poisoned while the root is still suspended. */
    CHECK(memory.watched_heap_released);
    lb_engine_leave(engine);
    command(world,"resume");
    LbBytesView retained=await(root);
    CHECK(retained.size==sizeof(expected) && !memcmp(retained.data,expected,sizeof(expected)));
    stop(world);
    CHECK(retained.size==sizeof(expected) && !memcmp(retained.data,expected,sizeof(expected)));
    consume(root);
    LbWorldResourceUsage usage;
    CHECK(lb_world_resource_usage(world,&usage)==LB_WORLD_OK && !usage.current);
    lb_world_release(world);
    CHECK(lb_world_call(peer,text("gc_roots"),text("identity"),text("survived"),&other)==LB_WORLD_OK);
    value=await(other); CHECK(value.size==8 && !memcmp(value.data,"survived",8)); consume(other);
    stop(peer);
    CHECK(lb_world_resource_usage(peer,&usage)==LB_WORLD_OK && !usage.current);
    lb_world_release(peer);
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine);
    CHECK(!memory.live && !memory.bytes);
    printf("GC_ROOT_OK kind=%s collections_before=%zu collections_after=%zu poisoned_retirement=true peer_survived=true\n",argv[2],before,after);
    return 0;
}
