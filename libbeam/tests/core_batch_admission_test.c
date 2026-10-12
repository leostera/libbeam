/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#include "process.h"
#include "engine_internal.h"
#include "code_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
static void release(void *context,void *ptr)
{
    Memory *m=context; Header *h=(Header *)ptr-1;
    CHECK(m->live && m->bytes>=h->size); --m->live; m->bytes-=h->size;
    memset(ptr,0xa5,h->size); free(h);
}
static unsigned char *read_image(const char *path,size_t *size)
{
    FILE *file=fopen(path,"rb"); long length; unsigned char *bytes;
    CHECK(file && !fseek(file,0,SEEK_END)); length=ftell(file);
    CHECK(length>0 && length<1024*1024 && !fseek(file,0,SEEK_SET));
    bytes=malloc((size_t)length); CHECK(bytes && fread(bytes,1,(size_t)length,file)==(size_t)length && !fclose(file));
    *size=(size_t)length; return bytes;
}
static Eterm atom(LbCodeSpace *space,const char *name)
{
    Eterm result; CHECK(lb_atoms_find(space->atoms,name,strlen(name),&result)==LB_ATOM_OK); return result;
}
static void run_zero_arity(LbCodeSpace *space)
{
    LbCodeEntry *entry=NULL; LbProcess *process=NULL; LbProcessStatus status=LB_PROCESS_YIELDED;
    Eterm module=atom(space,"batch_alpha"),function=atom(space,"run"); size_t steps;
    LbCodeEntry *binary_entry=NULL;
    CHECK(lb_code_entry_acquire(space,module,atom(space,"entry"),1,&binary_entry)==LB_CODE_OK);
    lb_code_entry_release(binary_entry);
    CHECK(lb_code_entry_acquire(space,module,function,0,&entry)==LB_CODE_OK);
    CHECK(lb_process_create(entry,NULL,0,8,&process)==LB_PROCESS_READY);
    lb_code_entry_release(entry);
    CHECK(lb_code_unload_all(space)==LB_CODE_BUSY);
    for(steps=0;steps<1000 && status==LB_PROCESS_YIELDED;++steps) status=lb_process_run(process,1,100);
    CHECK(status==LB_PROCESS_DONE);
    Eterm result=lb_process_result(process);
    CHECK(is_tuple(result) && is_literal_ptr(result) && arityval(*tuple_val(result))==2);
    CHECK(tuple_val(result)[1]==make_small(42) && tuple_val(result)[2]==atom(space,"batch_value"));
    lb_process_destroy(process);
}
static void expect_name(const unsigned char *actual,size_t actual_size,const char *expected)
{
    CHECK(actual_size==strlen(expected) && !memcmp(actual,expected,actual_size));
}
static void failures(LbEngine *engine,Memory *memory,const unsigned char *alpha,size_t alpha_size,
                     const unsigned char *beta,size_t beta_size,const unsigned char *unsupported,size_t unsupported_size)
{
    LbCodeSpace *space=NULL; LbBeamError error; LbLoadReport report;
    static const unsigned char invalid_image[]={'b','a','d'};
    LbCodeInput duplicate[2]={{alpha,alpha_size},{alpha,alpha_size}};
    LbCodeInput missing={alpha,alpha_size},corrupt[2]={{alpha,alpha_size},{invalid_image,sizeof(invalid_image)}};
    LbCodeInput refused[3]={{alpha,alpha_size},{beta,beta_size},{unsupported,unsupported_size}};
    size_t live,bytes,atoms; LbCodeStatus status;
    CHECK(lb_code_space_create(engine,&space)==LB_CODE_OK);
    live=memory->live; bytes=memory->bytes; atoms=lb_atoms_count(space->atoms);
    status=lb_code_load_batch(space,&missing,1,&error,&report);
    CHECK(status==LB_CODE_UNRESOLVED && !space->modules && report.has_import && report.has_module_index && report.module_index==0);
    expect_name(report.module,report.module_size,"batch_beta");
    expect_name(report.function,report.function_size,"beta");
    CHECK(memory->live==live && memory->bytes==bytes && lb_atoms_count(space->atoms)==atoms);
    status=lb_code_load_batch(space,duplicate,2,&error,&report);
    CHECK(status==LB_CODE_EXISTS && !space->modules && report.has_module_index && report.module_index==1);
    CHECK(memory->live==live && memory->bytes==bytes && lb_atoms_count(space->atoms)==atoms);
    status=lb_code_load_batch(space,corrupt,2,&error,&report);
    CHECK(status==LB_CODE_FORMAT && !space->modules && report.has_module_index && report.module_index==1);
    CHECK(memory->live==live && memory->bytes==bytes && lb_atoms_count(space->atoms)==atoms);
    status=lb_code_load_batch(space,refused,3,&error,&report);
    CHECK(status==LB_CODE_UNSUPPORTED && !space->modules && report.has_module_index && report.module_index==2);
    CHECK(memory->live==live && memory->bytes==bytes && lb_atoms_count(space->atoms)==atoms);
    /* A failed partial graph is retryable in the same still-loading world. */
    LbCodeInput group[2]={{beta,beta_size},{alpha,alpha_size}};
    CHECK(lb_code_load_batch(space,group,2,&error,&report)==LB_CODE_OK);
    CHECK(space->modules && space->modules->next);
    CHECK(lb_code_unload_all(space)==LB_CODE_OK);
    CHECK(lb_code_space_destroy(space)==LB_CODE_OK);
}
static void allocation_prefixes(LbEngine *engine,Memory *memory,const unsigned char *alpha,size_t alpha_size,
                                const unsigned char *beta,size_t beta_size)
{
    LbCodeInput group[2]={{alpha,alpha_size},{beta,beta_size}};
    LbCodeSpace *space=NULL; LbBeamError error; LbLoadReport report; LbCodeStatus status;
    size_t before,calls,i,live,bytes,atoms;
    CHECK(lb_code_space_create(engine,&space)==LB_CODE_OK);
    before=memory->calls; CHECK(lb_code_load_batch(space,group,2,&error,&report)==LB_CODE_OK);
    calls=memory->calls-before; CHECK(calls>0);
    run_zero_arity(space);
    CHECK(lb_code_unload_all(space)==LB_CODE_OK && lb_code_space_destroy(space)==LB_CODE_OK);
    for(i=1;i<=calls;++i) {
        CHECK(lb_code_space_create(engine,&space)==LB_CODE_OK);
        live=memory->live; bytes=memory->bytes; atoms=lb_atoms_count(space->atoms);
        memory->fail=memory->calls+i;
        status=lb_code_load_batch(space,group,2,&error,&report);
        if(status!=LB_CODE_NO_MEMORY)
            fprintf(stderr,"batch allocation prefix %zu/%zu status=%d stage=%s\n",i,calls,status,error.stage);
        CHECK(status==LB_CODE_NO_MEMORY && !space->modules);
        CHECK(memory->live==live && memory->bytes==bytes && lb_atoms_count(space->atoms)==atoms);
        memory->fail=0;
        CHECK(lb_code_load_batch(space,group,2,&error,&report)==LB_CODE_OK);
        CHECK(lb_code_unload_all(space)==LB_CODE_OK && lb_code_space_destroy(space)==LB_CODE_OK);
    }
}
int main(int argc,char **argv)
{
    Memory memory={0}; LbSystemAllocator allocator={&memory,allocate,release}; LbEngine *engine=NULL;
    LbCodeSpace *first=NULL,*second=NULL; LbCodeInput group[2]; LbBeamError error; LbLoadReport report;
    LbCodeModule *module; size_t alpha_size,beta_size,unsupported_size;
    unsigned char *alpha,*beta,*unsupported;
    CHECK(argc==4); alpha=read_image(argv[1],&alpha_size); beta=read_image(argv[2],&beta_size);
    unsupported=read_image(argv[3],&unsupported_size);
    group[0]=(LbCodeInput){beta,beta_size}; group[1]=(LbCodeInput){alpha,alpha_size};
    CHECK(lb_engine_create(&allocator,&engine)==LB_ENGINE_OK);
    failures(engine,&memory,alpha,alpha_size,beta,beta_size,unsupported,unsupported_size);
    allocation_prefixes(engine,&memory,alpha,alpha_size,beta,beta_size);
    CHECK(lb_code_space_create(engine,&first)==LB_CODE_OK && lb_code_space_create(engine,&second)==LB_CODE_OK);
    CHECK(lb_code_load_batch(first,group,2,&error,&report)==LB_CODE_OK);
    CHECK(lb_code_load_batch(second,group,2,&error,&report)==LB_CODE_OK);
    run_zero_arity(first); run_zero_arity(second);
    module=first->modules;
    CHECK(module && module->importers>0);
    CHECK(lb_code_unload(module)==LB_CODE_BUSY); /* the peer in this cycle imports it */
    CHECK(lb_code_space_destroy(first)==LB_CODE_BUSY);
    CHECK(lb_code_unload_all(first)==LB_CODE_OK);
    CHECK(lb_code_space_destroy(first)==LB_CODE_OK);
    CHECK(second->modules && second->modules->next); /* peer remains independent */
    run_zero_arity(second);
    CHECK(lb_code_unload_all(second)==LB_CODE_OK && lb_code_space_destroy(second)==LB_CODE_OK);
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine);
    CHECK(!memory.live && !memory.bytes);
    free(alpha); free(beta); free(unsupported);
    puts("CORE_BATCH_ADMISSION_OK forward_imports=true mutual_imports=true rollback=true allocation_prefixes=true independent_spaces=true physical_retirement=true");
    return 0;
}
