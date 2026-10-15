/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@osterra.io>
 */
#include "process_internal.h"
#include "world_internal.h"
#include "big.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"gc_bif:%d: %s\n",__LINE__,#x); abort(); } } while(0)
static Eterm atom(LbWorld *w,const char *name)
{ Eterm value; CHECK(lb_atoms_find(w->space->atoms,name,strlen(name),&value)==LB_ATOM_OK); return value; }
static LbProcess *create(LbWorld *w,LbCodeModule *m,const char *name,unsigned arity,Eterm *args,size_t heap)
{
    LbCodeEntry *entry; LbProcess *p;
    CHECK(lb_code_entry_acquire(w->space,m->name,atom(w,name),arity,&entry)==LB_CODE_OK);
    CHECK(lb_process_create(entry,args,arity,heap,&p)==LB_PROCESS_READY);
    lb_code_entry_release(entry); return p;
}
static LbProcessStatus run(LbProcess *p)
{
    LbProcessStatus status=LB_PROCESS_READY;
    for(unsigned i=0;i<1000 && (status==LB_PROCESS_READY || status==LB_PROCESS_YIELDED);++i)
        status=lb_process_run(p,1,100);
    return status;
}
static void fill_heap(LbProcess *p)
{
    p->heap_reserved=11;
    while(p->htop<p->stop-S_RESERVED-p->heap_reserved) *p->htop++=NIL;
}
static void check_integer(Eterm actual,Eterm expected)
{
    if(is_small(expected)) CHECK(actual==expected);
    else {
        CHECK(lb_is_big(actual) && lb_big_size(actual)==lb_big_size(expected));
        CHECK(!memcmp(boxed_val(actual),boxed_val(expected),(lb_big_size(expected)+1)*sizeof(Eterm)));
    }
}
static void bnot_cases(LbWorld *w,LbCodeModule *m)
{
    Eterm args[]={make_small(5)}; LbProcess *p=create(w,m,"bnot_input",1,args,1024);
    CHECK(run(p)==LB_PROCESS_DONE && lb_process_result(p)==make_small(-6)); lb_process_destroy(p);

    Eterm source_words[2]={_make_header(1,POS_BIG_SUBTAG),(Uint)MAX_SMALL+1};
    Eterm expected_words[6]={0}; Eterm expected=lb_big_bnot(make_boxed(source_words),expected_words,6);
    CHECK(!is_small(expected));
    p=create(w,m,"bnot_input",1,args,1024);
    p->htop[0]=source_words[0]; p->htop[1]=source_words[1];
    p->construction_root=p->reg[0]=make_boxed(p->htop); p->htop+=2; fill_heap(p);
    size_t collections=p->collections;
    CHECK(run(p)==LB_PROCESS_DONE && p->collections>collections);
    check_integer(p->construction_root,make_boxed(source_words));
    check_integer(lb_process_result(p),expected); lb_process_destroy(p);

    args[0]=am_true; p=create(w,m,"bnot_input",1,args,1024);
    CHECK(run(p)==LB_PROCESS_EXCEPTION && p->exception_value==am_badarith); lb_process_destroy(p);
}
static void list_to_atom_cases(LbWorld *w,LbCodeModule *m)
{
    Eterm args[]={NIL}; LbProcess *p=create(w,m,"list_atom",1,args,1024);
    Eterm *cell=p->htop; cell[0]=make_small('h'); cell[1]=make_list(cell+2);
    cell[2]=make_small('i'); cell[3]=NIL; p->htop+=4;
    p->construction_root=p->reg[0]=make_list(cell);
    CHECK(run(p)==LB_PROCESS_DONE && lb_process_result(p)==atom(w,"hi")); lb_process_destroy(p);

    p=create(w,m,"list_atom",1,args,1024); cell=p->htop;
    cell[0]=make_small(-1); cell[1]=NIL; p->htop+=2; p->construction_root=p->reg[0]=make_list(cell);
    CHECK(run(p)==LB_PROCESS_EXCEPTION && p->exception_value==am_badarg); lb_process_destroy(p);
}
static void divide_cases(LbWorld *w,LbCodeModule *m)
{
    Eterm args[]={make_small(5),make_small(2)}; LbProcess *p=create(w,m,"divide",2,args,1024); fill_heap(p);
    size_t collections=p->collections; CHECK(run(p)==LB_PROCESS_DONE && p->collections>collections);
    Eterm result=lb_process_result(p); CHECK(is_boxed(result) && *boxed_val(result)==_make_header(1,FLOAT_SUBTAG));
    double value; memcpy(&value,boxed_val(result)+1,sizeof(value)); if(value!=2.5) fprintf(stderr,"div=%g\n",value); CHECK(value==2.5); lb_process_destroy(p);

    args[1]=make_small(0); p=create(w,m,"divide",2,args,1024);
    CHECK(run(p)==LB_PROCESS_EXCEPTION && p->exception_value==am_badarith); lb_process_destroy(p);
}
int main(int argc,char **argv)
{
    CHECK(argc==2); FILE *file=fopen(argv[1],"rb"); CHECK(file && !fseek(file,0,SEEK_END));
    long length=ftell(file); CHECK(length>0 && !fseek(file,0,SEEK_SET));
    unsigned char *bytes=malloc((size_t)length); CHECK(bytes && fread(bytes,1,(size_t)length,file)==(size_t)length && !fclose(file));
    LbEngine *engine; LbWorld *world; LbCodeModule *module; LbBeamError error;
    CHECK(lb_engine_create(NULL,&engine)==LB_ENGINE_OK && lb_world_create(engine,&world)==LB_WORLD_OK);
    lb_engine_enter(engine); LbCodeStatus loaded=lb_code_load(world->space,bytes,(size_t)length,&module,&error);
    if(loaded!=LB_CODE_OK) fprintf(stderr,"load=%d stage=%s offset=%zu\n",loaded,error.stage,error.offset);
    CHECK(loaded==LB_CODE_OK); free(bytes);
    bnot_cases(world,module); list_to_atom_cases(world,module); divide_cases(world,module);
    lb_engine_leave(engine); LbReclamation *reclamation;
    CHECK(lb_world_stop(world,&reclamation)==LB_WORLD_OK && lb_reclamation_poll(reclamation)==LB_WORLD_OK);
    CHECK(lb_reclamation_consume(reclamation)==LB_WORLD_OK); lb_reclamation_release(reclamation); lb_world_release(world);
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine);
    puts("CORE_GC_BIF_FORMS_OK bnot=true list_to_atom=true slash=true failure=true gc_roots=true");
    return 0;
}
