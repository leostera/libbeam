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
static void run_named_zero_arity(LbCodeSpace *space,const char *name)
{
    LbCodeEntry *entry=NULL; LbProcess *process=NULL; LbProcessStatus status=LB_PROCESS_YIELDED;
    Eterm module=atom(space,name),function=atom(space,"run"); size_t steps;
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
static void run_zero_arity(LbCodeSpace *space) { run_named_zero_arity(space,"batch_alpha"); }
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
    /* Large rollback: hundreds of independently prepared controls precede
     * duplicate-name refusal. No staged atom or program may survive teardown. */
    LbCodeInput repeated[256];
    for(size_t i=0;i<256;++i) repeated[i]=(LbCodeInput){alpha,alpha_size};
    status=lb_code_load_batch(space,repeated,256,&error,&report);
    CHECK(status==LB_CODE_EXISTS && !space->modules && report.has_module_index && report.module_index==1);
    CHECK(memory->live==live && memory->bytes==bytes && lb_atoms_count(space->atoms)==atoms);
    /* A failed partial graph is retryable in the same still-loading world. */
    LbCodeInput group[2]={{beta,beta_size},{alpha,alpha_size}};
    CHECK(lb_code_load_batch(space,group,2,&error,&report)==LB_CODE_OK);
    CHECK(space->modules && space->modules->next);
    CHECK(lb_code_unload_all(space)==LB_CODE_OK);
    CHECK(lb_code_space_destroy(space)==LB_CODE_OK);
}
static void store_u32(unsigned char *target,size_t value)
{
    CHECK(value<=UINT32_MAX);
    target[0]=(unsigned char)(value>>24); target[1]=(unsigned char)(value>>16);
    target[2]=(unsigned char)(value>>8); target[3]=(unsigned char)value;
}
static unsigned char *padded_image(const unsigned char *original,size_t size,size_t target)
{
    /* Unknown IFF chunks are valid BEAM. Preserve every original chunk and
     * instruction while exercising the real encoded-byte admission boundary. */
    CHECK(size%4==0 && target%4==0 && target>=size+8);
    unsigned char *bytes=calloc(1,target+1); CHECK(bytes);
    memcpy(bytes,original,size); store_u32(bytes+4,target-8);
    memcpy(bytes+size,"LbPd",4); store_u32(bytes+size+4,target-size-8);
    return bytes;
}
static void exact_batch_limits(LbEngine *engine,Memory *memory,const unsigned char *alpha,size_t alpha_size,
                               const unsigned char *beta,size_t beta_size)
{
    LbCodeSpace *space=NULL; LbBeamError error; LbLoadReport report;
    CHECK(LB_CODE_BATCH_MODULE_LIMIT==4096u);
    CHECK(LB_CODE_BATCH_BYTES_LIMIT==64u*1024u*1024u);
    LbCodeInput *repeated=calloc(4097,sizeof(*repeated)); CHECK(repeated);
    for(size_t i=0;i<4097;++i) repeated[i]=(LbCodeInput){alpha,alpha_size};
    CHECK(lb_code_space_create(engine,&space)==LB_CODE_OK);
    size_t live=memory->live,bytes=memory->bytes,atoms=lb_atoms_count(space->atoms),calls=memory->calls;
    CHECK(lb_code_load_batch(space,repeated,0,&error,&report)==LB_CODE_INVALID);
    CHECK(lb_code_load_batch(space,repeated,4097,&error,&report)==LB_CODE_LIMIT);
    CHECK(!strcmp(report.stage,"batch module limit") && memory->calls==calls);
    for(size_t count=4095;count<=4096;++count) {
        /* Within the count boundary, actual parsing/duplicate detection must
         * run. A lower hidden count cap must not masquerade as acceptance. */
        CHECK(lb_code_load_batch(space,repeated,count,&error,&report)==LB_CODE_EXISTS);
        CHECK(report.has_module_index && report.module_index==1);
        CHECK(!space->modules && !space->loading);
        CHECK(memory->live==live && memory->bytes==bytes && lb_atoms_count(space->atoms)==atoms);
    }
    CHECK(lb_code_space_destroy(space)==LB_CODE_OK); free(repeated);

    const size_t boundary=64u*1024u*1024u;
    const size_t encoded_sizes[2]={boundary-4,boundary};
    for(size_t i=0;i<2;++i) {
        const size_t alpha_bytes=encoded_sizes[i]-beta_size;
        unsigned char *owned_alpha=padded_image(alpha,alpha_size,alpha_bytes);
        unsigned char *owned_beta=malloc(beta_size); CHECK(owned_beta); memcpy(owned_beta,beta,beta_size);
        LbCodeInput group[2]={{owned_alpha,alpha_bytes},{owned_beta,beta_size}};
        CHECK(lb_code_space_create(engine,&space)==LB_CODE_OK);
        if(encoded_sizes[i]==boundary) {
            live=memory->live; bytes=memory->bytes; atoms=lb_atoms_count(space->atoms); calls=memory->calls;
            group[0].size=alpha_bytes+1; /* backed by a real extra byte */
            CHECK(lb_code_load_batch(space,group,2,&error,&report)==LB_CODE_LIMIT);
            CHECK(!strcmp(report.stage,"batch byte limit") && memory->calls==calls);
            CHECK(!space->modules && !space->loading);
            CHECK(memory->live==live && memory->bytes==bytes && lb_atoms_count(space->atoms)==atoms);
            group[0].size=alpha_bytes;
        }
        CHECK(lb_code_load_batch(space,group,2,&error,&report)==LB_CODE_OK);
        /* Published code and literals cannot borrow either caller buffer. */
        memset(owned_alpha,0xa5,alpha_bytes); memset(owned_beta,0xa5,beta_size);
        free(owned_alpha); free(owned_beta);
        run_zero_arity(space);
        CHECK(lb_code_unload_all(space)==LB_CODE_OK && lb_code_space_destroy(space)==LB_CODE_OK);
    }
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
static void existing_allocation_prefixes(LbEngine *engine,Memory *memory,
                                        const LbCodeInput base[2],const LbCodeInput *addition)
{
    LbCodeSpace *space=NULL,*peer=NULL; LbBeamError error; LbLoadReport report;
    CHECK(lb_code_space_create(engine,&peer)==LB_CODE_OK);
    CHECK(lb_code_load_batch(peer,base,2,&error,&report)==LB_CODE_OK);
    CHECK(lb_code_space_create(engine,&space)==LB_CODE_OK);
    CHECK(lb_code_load_batch(space,base,2,&error,&report)==LB_CODE_OK);
    size_t before=memory->calls;
    CHECK(lb_code_load_batch(space,addition,1,&error,&report)==LB_CODE_OK);
    const size_t allocations=memory->calls-before; CHECK(allocations>0);
    run_named_zero_arity(space,"batch_gamma");
    CHECK(lb_code_unload_all(space)==LB_CODE_OK && lb_code_space_destroy(space)==LB_CODE_OK);
    for(size_t i=1;i<=allocations;++i) {
        CHECK(lb_code_space_create(engine,&space)==LB_CODE_OK);
        CHECK(lb_code_load_batch(space,base,2,&error,&report)==LB_CODE_OK);
        LbCodeModule *head=space->modules,*next=head->next;
        CHECK(next && !next->next);
        size_t live=memory->live,bytes=memory->bytes,atoms=lb_atoms_count(space->atoms);
        size_t head_importers=head->importers,next_importers=next->importers;
        memory->fail=memory->calls+i;
        CHECK(lb_code_load_batch(space,addition,1,&error,&report)==LB_CODE_NO_MEMORY);
        memory->fail=0;
        CHECK(!space->loading && space->modules==head && head->next==next && !next->next);
        CHECK(head->importers==head_importers && next->importers==next_importers);
        CHECK(memory->live==live && memory->bytes==bytes && lb_atoms_count(space->atoms)==atoms);
        run_zero_arity(space); run_zero_arity(peer);
        CHECK(lb_code_load_batch(space,addition,1,&error,&report)==LB_CODE_OK);
        run_named_zero_arity(space,"batch_gamma");
        CHECK(lb_code_unload_all(space)==LB_CODE_OK && lb_code_space_destroy(space)==LB_CODE_OK);
    }
    run_zero_arity(peer);
    CHECK(lb_code_unload_all(peer)==LB_CODE_OK && lb_code_space_destroy(peer)==LB_CODE_OK);
}
static void conflicting_code_rollback(LbEngine *engine,Memory *memory,const LbCodeInput base[2],
                                      const LbCodeInput *addition,const LbCodeInput *conflict)
{
    LbCodeSpace *space=NULL; LbBeamError error; LbLoadReport report;
    CHECK(lb_code_space_create(engine,&space)==LB_CODE_OK);
    CHECK(lb_code_load_batch(space,base,2,&error,&report)==LB_CODE_OK);
    LbCodeModule *head=space->modules,*next=head->next;
    size_t live=memory->live,bytes=memory->bytes,atoms=lb_atoms_count(space->atoms);
    LbCodeInput attempted[2]={*addition,*conflict};
    CHECK(lb_code_load_batch(space,attempted,2,&error,&report)==LB_CODE_EXISTS);
    CHECK(report.has_module_index && report.module_index==1 && !space->loading);
    CHECK(space->modules==head && head->next==next && !next->next);
    CHECK(memory->live==live && memory->bytes==bytes && lb_atoms_count(space->atoms)==atoms);
    /* The conflicting real compiler output returns 43, not 42. Neither that
     * replacement nor its batch peer may have escaped the failed transaction. */
    run_zero_arity(space);
    CHECK(lb_code_load_batch(space,addition,1,&error,&report)==LB_CODE_OK);
    run_named_zero_arity(space,"batch_gamma");
    CHECK(lb_code_unload_all(space)==LB_CODE_OK && lb_code_space_destroy(space)==LB_CODE_OK);
}
int main(int argc,char **argv)
{
    Memory memory={0}; LbSystemAllocator allocator={&memory,allocate,release}; LbEngine *engine=NULL;
    LbCodeSpace *first=NULL,*second=NULL; LbCodeInput group[2]; LbBeamError error; LbLoadReport report;
    LbCodeModule *module; size_t alpha_size,beta_size,unsupported_size,gamma_size,conflict_size;
    unsigned char *alpha,*beta,*unsupported,*gamma,*conflict;
    CHECK(argc==6); alpha=read_image(argv[1],&alpha_size); beta=read_image(argv[2],&beta_size);
    unsupported=read_image(argv[3],&unsupported_size); gamma=read_image(argv[4],&gamma_size);
    conflict=read_image(argv[5],&conflict_size);
    group[0]=(LbCodeInput){beta,beta_size}; group[1]=(LbCodeInput){alpha,alpha_size};
    CHECK(lb_engine_create(&allocator,&engine)==LB_ENGINE_OK);
    failures(engine,&memory,alpha,alpha_size,beta,beta_size,unsupported,unsupported_size);
    allocation_prefixes(engine,&memory,alpha,alpha_size,beta,beta_size);
    exact_batch_limits(engine,&memory,alpha,alpha_size,beta,beta_size);
    LbCodeInput addition={gamma,gamma_size};
    existing_allocation_prefixes(engine,&memory,group,&addition);
    LbCodeInput conflicting={conflict,conflict_size};
    conflicting_code_rollback(engine,&memory,group,&addition,&conflicting);
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
    free(alpha); free(beta); free(unsupported); free(gamma); free(conflict);
    puts("CORE_BATCH_ADMISSION_OK forward_imports=true mutual_imports=true rollback=true allocation_prefixes=true independent_spaces=true physical_retirement=true");
    return 0;
}
