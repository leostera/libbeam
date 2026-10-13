/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#include "process_internal.h"
#include "alloc_internal.h"
#include "work_budget_internal.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"CHECK %s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while(0)
static Eterm atom(LbCodeSpace *space,const char *name)
{
    Eterm value; CHECK(lb_atoms_find(space->atoms,name,strlen(name),&value)==LB_ATOM_OK); return value;
}
static LbProcess *start(LbCodeSpace *space,LbCodeModule *module,const char *function)
{
    LbCodeEntry *entry; LbProcess *process; Eterm arg=make_small(42);
    CHECK(lb_code_entry_acquire(space,module->name,atom(space,function),1,&entry)==LB_CODE_OK);
    CHECK(lb_process_create(entry,&arg,1,64,&process)==LB_PROCESS_READY);
    lb_code_entry_release(entry); return process;
}
static LbProcessStatus run(LbProcess *process)
{
    for(size_t i=0;i<1000 && (process->status==LB_PROCESS_READY || process->status==LB_PROCESS_YIELDED);++i) {
        lb_process_run(process,1,4);
        if(process->status==LB_PROCESS_YIELDED) CHECK(lb_process_collect(process,0)==LB_PROCESS_YIELDED);
    }
    return process->status;
}
static void bytes(Eterm term,const char *expected);
static LbProcessStatus execute(LbCodeSpace *space,LbCodeModule *module,const char *function,
                               const char *expected,uint64_t limit,uint64_t *used)
{
    LbProcess *process=start(space,module,function);
    LbWorkBudget budget={0}; LbAllocDomain *domain=space->domain;
    budget.domain=domain; budget.references=1; budget.limit=budget.remaining=limit;
    lb_alloc_domain_begin_operation(domain); lb_work_domain_borrow(domain,&budget);
    for(size_t i=0;i<1000 && (process->status==LB_PROCESS_READY || process->status==LB_PROCESS_YIELDED);++i) {
        lb_process_run(process,100000,100000);
        if(process->status==LB_PROCESS_YIELDED) CHECK(lb_process_collect(process,0)==LB_PROCESS_YIELDED);
    }
    LbProcessStatus status=process->status;
    if(used) *used=limit-budget.remaining;
    if(status==LB_PROCESS_DONE && expected) bytes(lb_process_result(process),expected);
    lb_work_domain_borrow(domain,NULL);
    if(domain->failed_work) lb_work_domain_clear_failure(domain);
    lb_alloc_domain_begin_operation(domain);
    lb_process_destroy(process); return status;
}
static void bytes(Eterm term,const char *expected)
{
    LbBitstringView view; size_t length=strlen(expected);
    CHECK(lb_bitstring_view(term,&view) && !view.bit_offset && view.bit_size==length*8);
    CHECK(!memcmp(view.data,expected,length));
}
static unsigned char *read_file(const char *path,size_t *size)
{
    FILE *file=fopen(path,"rb"); long length; unsigned char *data;
    CHECK(file && !fseek(file,0,SEEK_END) && (length=ftell(file))>0 && length<8*1024*1024);
    CHECK(!fseek(file,0,SEEK_SET)); data=malloc((size_t)length); CHECK(data);
    CHECK(fread(data,1,(size_t)length,file)==(size_t)length && !fclose(file)); *size=(size_t)length; return data;
}
int main(int argc,char **argv)
{
    static const char *functions[]={"new_binary","add_created_binary","update_created_binary","single_created_binary","dynamic_binary"};
    static const char *expected[]={"map-ok","assoc-ok","exact-ok","single-ok","dynamic-ok"};
    LbEngine *engine; LbCodeSpace *space; LbCodeModule *module; LbBeamError error; size_t size;
    CHECK(argc==2); unsigned char *image=read_file(argv[1],&size);
    CHECK(lb_engine_create(NULL,&engine)==LB_ENGINE_OK);
    CHECK(lb_code_space_create(engine,&space)==LB_CODE_OK);
    CHECK(lb_code_load(space,image,size,&module,&error)==LB_CODE_OK);
    for(size_t i=0;i<sizeof(functions)/sizeof(functions[0]);++i) {
        LbProcess *process=start(space,module,functions[i]);
        CHECK(run(process)==LB_PROCESS_DONE); bytes(lb_process_result(process),expected[i]);
        lb_process_destroy(process);
    }
    for(size_t i=0;i<sizeof(functions)/sizeof(functions[0]);++i) {
        uint64_t used=0,exact_used=0;
        CHECK(execute(space,module,functions[i],expected[i],UINT64_MAX,&used)==LB_PROCESS_DONE && used>0);
        CHECK(execute(space,module,functions[i],expected[i],used,&exact_used)==LB_PROCESS_DONE && exact_used==used);
        CHECK(execute(space,module,functions[i],NULL,used-1,NULL)==LB_PROCESS_WORK_LIMIT);
    }
    LbProcess *missing=start(space,module,"exact_missing");
    CHECK(run(missing)==LB_PROCESS_EXCEPTION);
    CHECK(lb_process_exception(missing)==atom(space,"badkey"));
    lb_process_destroy(missing);
    CHECK(lb_code_unload(module)==LB_CODE_OK);
    CHECK(lb_code_space_destroy(space)==LB_CODE_OK);
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine); free(image);
    puts("CORE_MAP_EXECUTION_OK construction=literal,dynamic assoc=true exact=true get=single,multi missing_key=badkey");
    return 0;
}
