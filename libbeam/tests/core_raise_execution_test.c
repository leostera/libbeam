/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#include "process_internal.h"
#include "raise_trace.h"
#include "stacktrace.h"
#include "term_compare.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while(0)
static Eterm atom(LbCodeSpace *space,const char *name)
{ Eterm result; CHECK(lb_atoms_find(space->atoms,name,strlen(name),&result)==LB_ATOM_OK); return result; }
static LbProcess *execute(LbCodeSpace *space,LbCodeModule *module,const char *function,LbProcessStatus expected)
{
    LbCodeEntry *entry; LbProcess *p;
    CHECK(lb_code_entry_acquire(space,module->name,atom(space,function),1,&entry)==LB_CODE_OK);
    CHECK(lb_process_create_binary(entry,"payload",7,16,&p)==LB_PROCESS_READY); lb_code_entry_release(entry);
    LbProcessStatus status=LB_PROCESS_READY;
    for(size_t i=0;i<1000 && (status==LB_PROCESS_READY || status==LB_PROCESS_YIELDED);++i)
        status=lb_process_run(p,1,100);
    if(status!=expected) fprintf(stderr,"%s status=%d expected=%d\n",function,status,expected);
    CHECK(status==expected); return p;
}
static void payload(Eterm value)
{
    LbBitstringView view; CHECK(lb_bitstring_view(value,&view));
    CHECK(view.bit_size==56 && !memcmp(view.data,"payload",7));
}
int main(int argc,char **argv)
{
    FILE *file; long length; CHECK(argc==2 && (file=fopen(argv[1],"rb")) && !fseek(file,0,SEEK_END));
    length=ftell(file); CHECK(length>0 && !fseek(file,0,SEEK_SET));
    unsigned char *bytes=malloc((size_t)length); CHECK(bytes && fread(bytes,1,(size_t)length,file)==(size_t)length && !fclose(file));
    LbEngine *engine; LbCodeSpace *space; LbCodeModule *module; LbBeamError error;
    CHECK(lb_engine_create(NULL,&engine)==LB_ENGINE_OK && lb_code_space_create(engine,&space)==LB_CODE_OK);
    CHECK(lb_code_preload(space)==LB_CODE_OK);
    LbCodeStatus loaded=lb_code_load(space,bytes,(size_t)length,&module,&error);
    if(loaded!=LB_CODE_OK) fprintf(stderr,"load=%d stage=%s offset=%zu\n",loaded,error.stage,error.offset);
    CHECK(loaded==LB_CODE_OK); free(bytes);
    const char *names[]={"raised_error","raised_exit","raised_throw","fun_frame","plain_throw"};
    for(size_t i=0;i<5;++i) {
        LbProcess *p=execute(space,module,names[i],LB_PROCESS_EXCEPTION);
        CHECK(lb_exception_class(p->freason)==(i==1?am_exit:am_error));
        for(size_t r=0;r<MAX_REG;++r) p->reg[r]=NIL;
        CHECK(lb_process_collect(p,0)==LB_PROCESS_EXCEPTION);
        Eterm reason=p->exception_value;
        if(i==2 || i==4) { Eterm *tuple=tuple_val(reason); CHECK(arityval(tuple[0])==2 && tuple[1]==am_nocatch); reason=tuple[2]; }
        payload(reason);
        if(i<4) {
            LbRaisedTrace trace; CHECK(lb_raise_trace_unpack(p->ftrace,&trace));
            CHECK(is_list(trace.symbolic) && list_val(trace.symbolic)[1]==NIL);
            Eterm *frame=tuple_val(list_val(trace.symbolic)[0]);
            if(i==3) CHECK(arityval(frame[0])==3 && lb_fun(frame[1]) && frame[2]==NIL && frame[3]==NIL);
            else CHECK(arityval(frame[0])==4 && frame[1]==module->name && frame[2]==atom(space,"origin") && frame[3]==make_small(1) && frame[4]==NIL);
            if(i!=1) {
                Eterm down; p->heap_reserved=2048;
                CHECK(lb_error_down_reason(p,&down)==LB_PROCESS_READY);
                CHECK(lb_raise_trace_unpack(p->ftrace,&trace));
                int equal=0; CHECK(lb_term_equal(space->domain,tuple_val(down)[2],trace.symbolic,&equal)==LB_ALLOC_OK && equal);
                CHECK(tuple_val(down)[1]==p->exception_value);
            }
        }
        lb_process_destroy(p);
    }
    for(size_t i=0;i<2;++i) {
        LbProcess *p=execute(space,module,i?"invalid_trace":"invalid_class",LB_PROCESS_DONE);
        CHECK(lb_process_result(p)==am_badarg && p->ftrace==NIL); lb_process_destroy(p);
    }
    CHECK(lb_code_unload_all(space)==LB_CODE_OK && lb_code_space_destroy(space)==LB_CODE_OK);
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine);
    puts("CORE_RAISE_EXECUTION_OK supplied_trace=true classes=true nocatch=true badarg_return=true fun_frame=true gc=true monitor_reason=true catch_unwind=false");
    return 0;
}
