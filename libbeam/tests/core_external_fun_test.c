/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@earendil-works.com>
 */
#include "process_internal.h"
#include "fun.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"CHECK %s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while(0)
static Eterm atom(LbCodeSpace *space,const char *name)
{
    Eterm value;
    CHECK(lb_atoms_find(space->atoms,name,strlen(name),&value)==LB_ATOM_OK);
    return value;
}
static LbProcess *start(LbCodeSpace *space,LbCodeModule *module,const char *name)
{
    LbCodeEntry *entry=NULL; LbProcess *process=NULL;
    CHECK(lb_code_entry_acquire(space,module->name,atom(space,name),0,&entry)==LB_CODE_OK);
    CHECK(lb_process_create(entry,NULL,0,5,&process)==LB_PROCESS_READY);
    lb_code_entry_release(entry);
    return process;
}
static void run(LbProcess *process)
{
    LbProcessStatus status=LB_PROCESS_READY;
    for(size_t i=0;i<1000 && (status==LB_PROCESS_READY || status==LB_PROCESS_YIELDED);++i)
        status=lb_process_run(process,1,100);
    CHECK(status==LB_PROCESS_DONE);
}
int main(int argc,char **argv)
{
    FILE *file; long length; unsigned char *bytes;
    LbEngine *engine=NULL; LbCodeSpace *space=NULL; LbCodeModule *module=NULL; LbBeamError error;
    CHECK(argc==2 && (file=fopen(argv[1],"rb")) && !fseek(file,0,SEEK_END));
    length=ftell(file); CHECK(length>0 && length<1024*1024 && !fseek(file,0,SEEK_SET));
    bytes=malloc((size_t)length); CHECK(bytes && fread(bytes,1,(size_t)length,file)==(size_t)length && !fclose(file));
    CHECK(lb_engine_create(NULL,&engine)==LB_ENGINE_OK);
    CHECK(lb_code_space_create(engine,&space)==LB_CODE_OK);
    LbCodeStatus loaded=lb_code_load(space,bytes,(size_t)length,&module,&error);
    if(loaded!=LB_CODE_OK) fprintf(stderr,"load status=%d stage=%s offset=%zu\n",loaded,error.stage,error.offset);
    CHECK(loaded==LB_CODE_OK);
    LbProcess *made=start(space,module,"make"); run(made);
    Eterm closure=lb_process_result(made); const LbFun *fun=lb_fun(closure);
    CHECK(fun && lb_fun_is_external(fun) && lb_fun_arity(fun)==0 && lb_fun_free(fun)==0);
    CHECK(fun->entry.exp && fun->entry.exp->info.mfa.module==am_erlang &&
          fun->entry.exp->info.mfa.function==am_self && fun->entry.exp->info.mfa.arity==0);
    CHECK(lb_process_collect(made,0)==LB_PROCESS_DONE && lb_fun(lb_process_result(made)));
    lb_process_destroy(made);
    LbProcess *called=start(space,module,"call"); run(called);
    CHECK(is_pid(lb_process_result(called)));
    lb_process_destroy(called);
    CHECK(lb_code_unload(module)==LB_CODE_OK);
    CHECK(lb_code_space_destroy(space)==LB_CODE_OK);
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine);
    free(bytes); puts("CORE_EXTERNAL_FUN_OK exact_mfa=true gc_rooted=true native_dispatch=true");
    return 0;
}
