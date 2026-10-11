/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#include "process_internal.h"
#include "executor.h"
#include "writable.h"
#include <pthread.h>
#include <stdio.h>
#include <time.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"CHECK %s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while(0)
typedef struct { size_t calls,fail,live,bytes; pthread_t host; _Atomic size_t worker_allocations; _Atomic int fail_worker; } Memory;
typedef union { max_align_t alignment; size_t size; } Header;
static void *allocate(void *context,size_t bytes)
{
    Memory *m=context;
    if(!pthread_equal(pthread_self(),m->host)) {
        atomic_fetch_add(&m->worker_allocations,1);
        if(atomic_exchange(&m->fail_worker,0)) return NULL;
    }
    if(++m->calls==m->fail) return NULL;
    Header *h=malloc(sizeof(*h)+bytes); if(!h) return NULL;
    h->size=bytes; ++m->live; m->bytes+=bytes; return h+1;
}
static void release(void *context,void *pointer)
{
    Memory *m=context; Header *h=(Header *)pointer-1;
    CHECK(m->live && m->bytes>=h->size); --m->live; m->bytes-=h->size; free(h);
}
static LbCodeEntry *entry(LbCodeSpace *space,LbCodeModule *module,const char *name)
{
    LbCodeEntry *result; Eterm atom;
    CHECK(lb_atoms_find(space->atoms,name,strlen(name),&atom)==LB_ATOM_OK);
    CHECK(lb_code_entry_acquire(space,lb_code_module_name(module),atom,1,&result)==LB_CODE_OK); return result;
}
static LbProcessStatus finish(LbProcess *p)
{
    for(size_t i=0;i<100;++i) {
        LbProcessStatus status=lb_process_run(p,1000,128);
        if(status!=LB_PROCESS_YIELDED) return status;
    }
    CHECK(0); return LB_PROCESS_INVALID;
}
static void check_empty(LbProcess *p,size_t capacity)
{
    Eterm value=lb_process_result(p); LbBitstringView view;
    CHECK(lb_bitstring_view(value,&view) && !view.bit_size && !view.bit_offset);
    CHECK(is_boxed(value) && *boxed_val(value)==LB_HEADER_SUB_BITS);
    LbSubBits *sub=(LbSubBits *)boxed_val(value); LbBinRef *ref=(LbBinRef *)boxed_val(sub->orig);
    CHECK(ref->thing_word==LB_HEADER_BIN_REF && ref->val->orig_size==(Sint)capacity);
    CHECK(sub->start==0 && sub->end==0 && (sub->base_flags&3)==LB_SUB_WRITABLE);
    CHECK(atomic_load(&ref->val->intern.refc)==1 && p->writable.overhead==capacity/sizeof(Eterm));
    for(size_t i=0;i<capacity;++i) CHECK(ref->val->orig_bytes[i]==0);
}
static void capacities(const LbCodeEntry *init)
{
    const Sint values[]={0,1,64,65,1024,65536,LB_MAX_BINARY_BYTES,-1,MIN_SMALL};
    for(size_t i=0;i<sizeof(values)/sizeof(*values);++i) {
        LbProcess *p; Eterm input=make_small(values[i]);
        size_t capacity=values[i]>=0?(size_t)values[i]:1024;
        CHECK(lb_process_create(init,&input,1,256,&p)==LB_PROCESS_READY);
        CHECK(finish(p)==LB_PROCESS_DONE); check_empty(p,capacity);
        CHECK(p->last_native_cost>=1+(capacity+1023)/1024);
        CHECK(lb_process_collect(p,0)==LB_PROCESS_DONE); check_empty(p,capacity); lb_process_destroy(p);
    }
    for(size_t i=0;i<4;++i) {
        LbProcess *p; Eterm input=i?am_false:NIL;
        CHECK(lb_process_create(init,&input,1,256,&p)==LB_PROCESS_READY);
        if(i>=2) { p->htop[0]=_make_header(2,i==2?POS_BIG_SUBTAG:NEG_BIG_SUBTAG); p->htop[1]=0; p->htop[2]=1; p->reg[0]=make_boxed(p->htop); p->htop+=3; }
        CHECK(finish(p)==LB_PROCESS_DONE); check_empty(p,1024); lb_process_destroy(p);
    }
    unsigned char bytes[65]; memset(bytes,0x47,sizeof(bytes)); LbProcess *p;
    CHECK(lb_process_create_binary(init,bytes,sizeof(bytes),256,&p)==LB_PROCESS_READY);
    CHECK(finish(p)==LB_PROCESS_DONE); check_empty(p,1024);
    CHECK(lb_process_collect(p,0)==LB_PROCESS_DONE && !p->off_heap.first); check_empty(p,1024); lb_process_destroy(p);
}
static void stop_at_init(LbProcess *p)
{
    for(size_t i=0;i<100 && *p->i!=op_bs_init_writable;++i) CHECK(lb_process_run(p,1,128)==LB_PROCESS_YIELDED);
    CHECK(*p->i==op_bs_init_writable);
}
static void failures(const LbCodeEntry *init,const LbCodeEntry *large,const LbCodeEntry *saved,Memory *m)
{
    size_t live=m->live,balance=m->bytes;
    for(size_t fail=1;fail<=2;++fail) {
        LbProcess *p; Eterm input=make_small(64); CHECK(lb_process_create(init,&input,1,256,&p)==LB_PROCESS_READY);
        stop_at_init(p); p->heap_reserved=(size_t)(p->stop-p->htop)-S_RESERVED;
        size_t calls=m->calls; Eterm *heap=p->heap; const BeamInstr *pc=p->i;
        m->fail=calls+fail;
        CHECK(lb_process_run(p,100,128)==LB_PROCESS_NO_MEMORY && p->heap==heap && p->i==pc && p->reg[0]==input);
        CHECK(!p->writable.first && lb_process_exception(p)==THE_NON_VALUE && lb_process_result(p)==THE_NON_VALUE);
        m->fail=0; lb_process_destroy(p); CHECK(m->live==live && m->bytes==balance);
        CHECK(lb_process_create(init,&input,1,256,&p)==LB_PROCESS_READY && finish(p)==LB_PROCESS_DONE);
        check_empty(p,64); lb_process_destroy(p); CHECK(m->live==live && m->bytes==balance);
    }
    /* Budget yield is after publication at the next actual PC, never a replay
     * of the allocating opcode. Resumption performs no second initialization. */
    LbProcess *p; Eterm input=NIL; CHECK(lb_process_create(large,&input,1,256,&p)==LB_PROCESS_READY);
    stop_at_init(p); const BeamInstr *pc=p->i;
    CHECK(lb_process_run(p,100,4)==LB_PROCESS_YIELDED && p->i==pc+1 && p->writable.first);
    size_t calls=m->calls; CHECK(finish(p)==LB_PROCESS_DONE && m->calls==calls); check_empty(p,65536); lb_process_destroy(p);
    unsigned char bytes[65]; memset(bytes,0x6b,sizeof(bytes));
    CHECK(lb_process_create_binary(saved,bytes,sizeof(bytes),256,&p)==LB_PROCESS_READY); stop_at_init(p);
    p->heap_reserved=(size_t)(p->stop-p->htop)-S_RESERVED; size_t collections=p->collections;
    CHECK(finish(p)==LB_PROCESS_DONE && p->collections>collections);
    LbBitstringView view; CHECK(lb_bitstring_view(lb_process_result(p),&view) && view.bit_size==sizeof(bytes)*8 && !view.bit_offset);
    CHECK(!memcmp(view.data,bytes,sizeof(bytes))); lb_process_destroy(p);
    CHECK(m->live==live && m->bytes==balance);
}
static void limit(const LbCodeEntry *init,Memory *m)
{
    LbProcess *p; Eterm input=make_small(LB_MAX_BINARY_BYTES+1u);
    CHECK(lb_process_create(init,&input,1,256,&p)==LB_PROCESS_READY); stop_at_init(p); size_t calls=m->calls;
    CHECK(lb_process_run(p,100,128)==LB_PROCESS_LIMIT && m->calls==calls && p->reg[0]==input);
    CHECK(p->freason==LB_EXC_NULL && lb_process_exception(p)==THE_NON_VALUE && lb_process_result(p)==THE_NON_VALUE);
    CHECK(lb_process_run(p,100,128)==LB_PROCESS_LIMIT); lb_process_destroy(p);
}
static LbTaskStatus wait_task(LbTask *task)
{
    struct timespec delay={0,1000000};
    for(size_t i=0;i<5000;++i) { LbTaskStatus s=lb_task_status(task); if(s!=LB_TASK_PENDING) return s; nanosleep(&delay,NULL); }
    CHECK(0); return LB_TASK_PENDING;
}
static void worker(LbEngine *engine,LbCodeSpace *space,LbCodeModule *module,Memory *m)
{
    const char *names[]={"loop","large","limit","saved"}; LbCodeEntry *entries[4]; LbTask *tasks[4];
    unsigned char bytes[65]; memset(bytes,0x59,sizeof(bytes));
    /* Acquire every raw handle before worker creation. */
    for(size_t i=0;i<4;++i) entries[i]=entry(space,module,names[i]);
    LbCodeEntry *init=entry(space,module,"init"); LbTask *attempt;
    atomic_store(&m->fail_worker,1);
    CHECK(lb_task_create_binary(engine,init,bytes,sizeof(bytes),&attempt)==LB_ENGINE_OK);
    CHECK(wait_task(attempt)==LB_TASK_NO_MEMORY && lb_task_exception(attempt)==THE_NON_VALUE); lb_task_release(attempt);
    CHECK(!atomic_load(&m->fail_worker));
    CHECK(lb_task_create_binary(engine,init,bytes,sizeof(bytes),&attempt)==LB_ENGINE_OK);
    CHECK(wait_task(attempt)==LB_TASK_DONE); lb_task_release(attempt);
    for(size_t i=0;i<4;++i) CHECK(lb_task_create_binary(engine,entries[i],bytes,sizeof(bytes),&tasks[i])==LB_ENGINE_OK);
    CHECK(wait_task(tasks[1])==LB_TASK_DONE && wait_task(tasks[2])==LB_TASK_LIMIT && wait_task(tasks[3])==LB_TASK_DONE);
    CHECK(lb_task_exception(tasks[2])==THE_NON_VALUE && lb_task_result(tasks[2])==THE_NON_VALUE);
    CHECK(atomic_load(&m->worker_allocations)>0 && lb_task_status(tasks[0])==LB_TASK_PENDING);
    lb_task_cancel(tasks[0]); CHECK(lb_task_status(tasks[0])==LB_TASK_CANCELLED);
    for(size_t i=0;i<4;++i) lb_task_release(tasks[i]);
    lb_engine_enter(engine); lb_code_entry_release(init); for(size_t i=0;i<4;++i) lb_code_entry_release(entries[i]); lb_engine_leave(engine);
    puts("CORE_WRITABLE_INIT_WORKER_OK off_host_allocation=true worker_oom_retry=true peer_progress=true explicit_limit=true cancellation=true");
}
static void reference_values(const LbCodeEntry *init,const LbCodeEntry *saved)
{
    const Eterm values[]={make_small(0),make_small(1),make_small(-1),make_small(1024),am_false,NIL};
    const char *names[]={"zero","one","negative","capacity","atom","nil","big","binary","saved"};
    for(size_t i=0;i<9;++i) {
        LbProcess *p; Eterm input=i<6?values[i]:NIL; unsigned char bytes[65]; memset(bytes,107,sizeof(bytes));
        if(i>=7) CHECK(lb_process_create_binary(i==8?saved:init,bytes,sizeof(bytes),256,&p)==LB_PROCESS_READY);
        else CHECK(lb_process_create(init,&input,1,256,&p)==LB_PROCESS_READY);
        if(i==6) { p->htop[0]=_make_header(2,POS_BIG_SUBTAG); p->htop[1]=0; p->htop[2]=1; p->reg[0]=make_boxed(p->htop); p->htop+=3; }
        CHECK(finish(p)==LB_PROCESS_DONE); LbBitstringView view;
        CHECK(lb_bitstring_view(lb_process_result(p),&view) && !view.bit_offset && !(view.bit_size%8));
        printf("%s:%zu",names[i],view.bit_size/8); if(i==8) printf(":%u",view.data[0]); putchar('\n');
        lb_process_destroy(p);
    }
}
int main(int argc,char **argv)
{
    CHECK(lb_instruction_supported(op_bs_init_writable) && lb_instruction_supported(op_i_bs_create_bin_jIWdW));
    if(argc==1) { puts("CORE_WRITABLE_INIT_GATE_OK whole_case=true bs_create_bin=true"); return 0; }
    int reference=argc==3 && !strcmp(argv[1],"--reference"); CHECK(argc==2 || reference);
    FILE *f=fopen(argv[argc-1],"rb"); CHECK(f && !fseek(f,0,SEEK_END)); long size=ftell(f); CHECK(size>0 && !fseek(f,0,SEEK_SET));
    unsigned char *data=malloc((size_t)size); CHECK(data && fread(data,1,(size_t)size,f)==(size_t)size && !fclose(f));
    Memory m={.host=pthread_self()}; LbSystemAllocator callbacks={&m,allocate,release}; LbEngine *engine; LbCodeSpace *space; LbCodeModule *module; LbBeamError error;
    CHECK(lb_engine_create(&callbacks,&engine)==LB_ENGINE_OK && lb_code_space_create(engine,&space)==LB_CODE_OK);
    CHECK(lb_code_load(space,data,(size_t)size,&module,&error)==LB_CODE_OK); free(data);
    LbCodeEntry *init=entry(space,module,"init"),*large=entry(space,module,"large"),*saved=entry(space,module,"saved");
    if(reference) reference_values(init,saved);
    else { capacities(init); failures(init,large,saved,&m); limit(init,&m); }
    lb_code_entry_release(init); lb_code_entry_release(large); lb_code_entry_release(saved);
    if(!reference) worker(engine,space,module,&m);
    lb_engine_enter(engine); CHECK(lb_code_unload(module)==LB_CODE_OK && lb_code_space_destroy(space)==LB_CODE_OK); lb_engine_leave(engine);
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine); CHECK(!m.live && !m.bytes);
    if(!reference) puts("CORE_WRITABLE_INIT_EXECUTION_OK standard_beam=true native_capacity_defaults=true zeroed_owned_backing=true y_gc=true failure_prefixes=2 fresh_process_retry=true post_publication_yield=true no_replay=true limit_not_exception=true physical_retirement=true bs_create_bin=true");
    return 0;
}
