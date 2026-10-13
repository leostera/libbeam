/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#define _POSIX_C_SOURCE 200809L
#include "process_internal.h"
#include "big.h"
#include "world_internal.h"
#include <stdio.h>
#include <time.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"CHECK %s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while(0)
typedef struct {
    size_t calls,fail,live,bytes;
    pthread_t host; uintptr_t worker_large_base; int worker_large_released;
} Memory;
typedef union { max_align_t alignment; size_t size; } Header;
static void *allocate(void *context,size_t size)
{
    Memory *m=context; Header *h;
    if(++m->calls==m->fail) return NULL;
    h=malloc(sizeof(*h)+size); CHECK(h); h->size=size; ++m->live; m->bytes+=size;
    if(!pthread_equal(m->host,pthread_self()) && size>1024*1024 && !m->worker_large_base) m->worker_large_base=(uintptr_t)(h+1);
    return h+1;
}
static void release(void *context,void *pointer)
{
    Memory *m=context; Header *h=(Header *)pointer-1;
    CHECK(m->live && m->bytes>=h->size); --m->live; m->bytes-=h->size;
    if((uintptr_t)pointer==m->worker_large_base) m->worker_large_released=1;
    memset(pointer,0xa5,h->size); free(h);
}
static void text(LbDecimalView view,const char *expected)
{ CHECK(view.data && view.size==strlen(expected) && !memcmp(view.data,expected,view.size)); }
static char *power_of_two(unsigned bits)
{
    /* Independent oracle: decimal digit doubling, not native limb division. */
    size_t capacity=bits+2,n=1,j; unsigned i; unsigned char *digits=calloc(capacity,1);
    char *result=malloc(capacity); CHECK(digits && result); digits[0]=1;
    for(i=0;i<bits;++i) {
        unsigned carry=0;
        for(j=0;j<n;++j) { unsigned value=digits[j]*2+carry; digits[j]=(unsigned char)(value%10); carry=value/10; }
        if(carry) digits[n++]=(unsigned char)carry;
    }
    for(j=0;j<n;++j) result[j]=(char)('0'+digits[n-1-j]); result[n]=0; free(digits); return result;
}
static LbDecimalView finish(LbDecimal *d,size_t capacity,Memory *m)
{
    size_t work,steps=0,calls=m->calls;
    while(!lb_decimal_step(d,1,&work)) {
        CHECK(work && work<=capacity && !lb_decimal_view(d).data && ++steps<100000);
    }
    CHECK(work<=capacity && calls==m->calls); return lb_decimal_view(d);
}
static void components(LbAllocDomain *domain,Memory *m)
{
    const Sint smalls[]={0,1,-1,MIN_SMALL,MAX_SMALL,INT64_MIN,INT64_MAX};
    unsigned char small[LB_DECIMAL_SMALL_BYTES]; Eterm words[3]; LbDecimal *d;
    size_t i,n,live=m->live,bytes=m->bytes; char expected[128]; uint64_t state=17;
    for(i=0;i<sizeof(smalls)/sizeof(smalls[0]);++i) {
        snprintf(expected,sizeof(expected),"%lld",(long long)smalls[i]); text(lb_decimal_small(smalls[i],small),expected);
    }
    for(i=0;i<10000;++i) {
        __uint128_t value,copy; char *end=expected+sizeof(expected)-1,*p=end; *end=0;
        state=state*UINT64_C(6364136223846793005)+1; words[1]=state;
        state=state*UINT64_C(6364136223846793005)+1; words[2]=state|1;
        value=((__uint128_t)words[2]<<64)|words[1]; copy=value;
        do { *--p=(char)('0'+copy%10); copy/=10; } while(copy);
        if(i&1) *--p='-'; words[0]=_make_header(2,(i&1)?NEG_BIG_SUBTAG:POS_BIG_SUBTAG);
        CHECK(lb_decimal_create(domain,make_boxed(words),&d)==LB_ALLOC_OK);
        text(finish(d,2,m),p); CHECK(words[1]==(Uint)value && words[2]==(Uint)(value>>64)); lb_decimal_destroy(d);
    }
    for(n=1;n<=128;n*=2) {
        Eterm *input=calloc(n+1,sizeof(Eterm)); char *oracle=power_of_two((unsigned)(n*64-1));
        CHECK(input); input[0]=_make_header(n,POS_BIG_SUBTAG); input[n]=(Uint)1<<63;
        CHECK(lb_decimal_create(domain,make_boxed(input),&d)==LB_ALLOC_OK);
        memset(input,0xa5,(n+1)*sizeof(Eterm)); free(input); /* continuation owns its copy */
        text(finish(d,n,m),oracle); lb_decimal_destroy(d); free(oracle);
    }
    words[0]=_make_header(1,POS_BIG_SUBTAG); words[1]=UINTPTR_MAX;
    m->fail=m->calls+1; d=(LbDecimal *)1;
    CHECK(lb_decimal_create(domain,make_boxed(words),&d)==LB_ALLOC_NO_MEMORY && !d && m->live==live && m->bytes==bytes);
    m->fail=0; CHECK(lb_decimal_create(domain,make_boxed(words),&d)==LB_ALLOC_OK);
    text(finish(d,1,m),"18446744073709551615"); lb_decimal_destroy(d);
    {
        Eterm *input=calloc(LB_BIG_ARITY_MAX+1,sizeof(Eterm)); size_t work;
        CHECK(input); input[0]=_make_header(LB_BIG_ARITY_MAX,POS_BIG_SUBTAG); input[LB_BIG_ARITY_MAX]=1;
        CHECK(lb_decimal_create(domain,make_boxed(input),&d)==LB_ALLOC_OK); free(input);
        CHECK(!lb_decimal_step(d,1,&work) && work==LB_BIG_ARITY_MAX);
        CHECK(lb_alloc_domain_destroy(domain)==LB_ALLOC_BUSY); lb_decimal_destroy(d);
    }
    CHECK(m->live==live && m->bytes==bytes);
    puts("CORE_DECIMAL_COMPONENT_OK oracle_128bit=10000 independent_decimal_doubling=true copied_input=true bounded_passes=true maximum_partial_retirement=true scratch_oom_retry=true execution=false");
}
static Eterm atom(LbCodeSpace *space,const char *name)
{ Eterm value; CHECK(lb_atoms_find(space->atoms,name,strlen(name),&value)==LB_ATOM_OK); return value; }
static LbProcess *start(LbCodeSpace *space,LbCodeModule *module,const char *function,size_t arity,size_t words)
{
    LbCodeEntry *entry; LbProcess *p; Eterm args[2]={NIL,NIL};
    CHECK(lb_code_entry_acquire(space,module->name,atom(space,function),(unsigned)arity,&entry)==LB_CODE_OK);
    CHECK(lb_process_create(entry,args,arity,words,&p)==LB_PROCESS_READY); lb_code_entry_release(entry); return p;
}
static size_t run(LbProcess *p,int collect)
{
    size_t i,continuations=0;
    for(i=0;i<100000 && (p->status==LB_PROCESS_READY || p->status==LB_PROCESS_YIELDED);++i) {
        lb_process_run(p,1,4);
        if(p->decimal) { ++continuations; CHECK(p->status==LB_PROCESS_YIELDED && *p->i==op_call_bif_W); }
        if(collect && p->status==LB_PROCESS_YIELDED) CHECK(lb_process_collect(p,0)==LB_PROCESS_YIELDED);
    }
    CHECK(i<100000); return continuations;
}
static LbDecimalView binary(LbProcess *p)
{
    LbBitstringView view; CHECK(p->status==LB_PROCESS_DONE && !p->decimal);
    CHECK(lb_bitstring_view(lb_process_result(p),&view) && !view.bit_offset && !(view.bit_size%8));
    return (LbDecimalView){view.data,view.bit_size/8};
}
static void semantics(LbCodeSpace *space,LbCodeModule *module)
{
    LbProcess *p; size_t i; char expected[32];
    const Sint values[]={0,1,-1,MIN_SMALL,MAX_SMALL};
    for(i=0;i<sizeof(values)/sizeof(values[0]);++i) {
        p=start(space,module,"number",1,5); p->reg[0]=make_small(values[i]); run(p,1);
        snprintf(expected,sizeof(expected),"%lld",(long long)values[i]); text(binary(p),expected); lb_process_destroy(p);
    }
    p=start(space,module,"sum",2,5); p->reg[0]=make_small(MAX_SMALL); p->reg[1]=make_small(1); run(p,1);
    text(binary(p),"576460752303423488"); lb_process_destroy(p);
    p=start(space,module,"negative",1,5); run(p,1); text(binary(p),"-18446744073709551617"); lb_process_destroy(p);
    for(i=0;i<2;++i) {
        char *expected=power_of_two(212+(unsigned)i);
        p=start(space,module,i?"boundary65":"boundary64",1,5); run(p,1);
        text(binary(p),expected); CHECK(binary(p).size==64+i && !!p->off_heap.first==(int)i);
        free(expected); lb_process_destroy(p);
    }
    p=start(space,module,"large",1,5); CHECK(run(p,1)>1);
    { char *expected=power_of_two(8192); text(binary(p),expected); free(expected); }
    CHECK(p->off_heap.first); lb_process_destroy(p);
    p=start(space,module,"number",1,5); run(p,0);
    CHECK(p->status==LB_PROCESS_EXCEPTION && lb_process_exception(p)==am_badarg); lb_process_destroy(p);
    p=start(space,module,"number",1,7); p->reg[0]=make_boxed(p->htop);
    *p->htop++=_make_header(1,FLOAT_SUBTAG); *p->htop++=0; run(p,1);
    CHECK(p->status==LB_PROCESS_EXCEPTION && lb_process_exception(p)==am_badarg && !p->decimal); lb_process_destroy(p);
}
static void pending_retirement(LbCodeSpace *space,LbCodeModule *module,Memory *m)
{
    size_t live=m->live,bytes=m->bytes,steps; LbProcess *p=start(space,module,"slow",1,5);
    for(steps=0;steps<100 && !p->decimal;++steps) CHECK(lb_process_run(p,1,4)==LB_PROCESS_YIELDED);
    CHECK(p->decimal && *p->i==op_call_bif_W && p->current->function==am_integer_to_binary);
    {
        Eterm *heap=p->heap,root=p->reg[0]; LbDecimal *continuation=p->decimal;
        size_t current_live=m->live,current_bytes=m->bytes; m->fail=m->calls+1;
        CHECK(lb_process_collect(p,0)==LB_PROCESS_NO_MEMORY && p->status==LB_PROCESS_YIELDED);
        CHECK(p->heap==heap && p->reg[0]==root && p->decimal==continuation && m->live==current_live && m->bytes==current_bytes);
        m->fail=0;
    }
    CHECK(lb_process_collect(p,0)==LB_PROCESS_YIELDED && lb_code_unload(module)==LB_CODE_BUSY);
    CHECK(lb_process_run(p,1,4)==LB_PROCESS_YIELDED && p->decimal);
    lb_process_destroy(p); CHECK(m->live==live && m->bytes==bytes);
}
static void failures(LbCodeSpace *space,LbCodeModule *module,Memory *m)
{
    size_t prefix,steps,live=m->live,bytes=m->bytes;
    {
        LbProcess *p=start(space,module,"small",1,5);
        for(steps=0;steps<100 && *p->i!=op_call_bif_W;++steps) CHECK(lb_process_run(p,1,4)==LB_PROCESS_YIELDED);
        CHECK(steps<100); while(p->stop-p->htop>S_RESERVED) *p->htop++=NIL;
        m->fail=m->calls+1; run(p,0); m->fail=0;
        CHECK(p->status==LB_PROCESS_NO_MEMORY && !p->decimal); lb_process_destroy(p);
        CHECK(m->live==live && m->bytes==bytes);
    }
    /* Stop at the real BIF, after any generated allocate-zero prologue. The
     * three fallible steps are scratch, copying space, offheap output payload. */
    for(prefix=1;prefix<=3;++prefix) {
        LbProcess *p=start(space,module,"large",1,5);
        for(steps=0;steps<100 && *p->i!=op_call_bif_W;++steps) CHECK(lb_process_run(p,1,4)==LB_PROCESS_YIELDED);
        CHECK(steps<100);
        while(p->stop-p->htop>S_RESERVED) *p->htop++=NIL;
        m->fail=m->calls+prefix; run(p,0); m->fail=0;
        CHECK(p->status==LB_PROCESS_NO_MEMORY && !p->decimal && lb_process_result(p)==THE_NON_VALUE);
        lb_process_destroy(p); CHECK(m->live==live && m->bytes==bytes);
        p=start(space,module,"large",1,5); run(p,0); CHECK(binary(p).size>64); lb_process_destroy(p);
    }
}
static void load_failures(LbEngine *engine,Memory *m,LbBytesView image)
{
    LbCodeSpace *space; LbCodeModule *module; LbBeamError error; size_t calls,i,live,bytes;
    CHECK(lb_code_space_create(engine,&space)==LB_CODE_OK); calls=m->calls;
    CHECK(lb_code_load(space,image.data,image.size,&module,&error)==LB_CODE_OK); calls=m->calls-calls;
    CHECK(lb_code_unload(module)==LB_CODE_OK && lb_code_space_destroy(space)==LB_CODE_OK);
    for(i=1;i<=calls;++i) {
        CHECK(lb_code_space_create(engine,&space)==LB_CODE_OK); live=m->live; bytes=m->bytes; m->fail=m->calls+i;
        CHECK(lb_code_load(space,image.data,image.size,&module,&error)==LB_CODE_NO_MEMORY && !module);
        CHECK(m->live==live && m->bytes==bytes); m->fail=0;
        CHECK(lb_code_load(space,image.data,image.size,&module,&error)==LB_CODE_OK);
        CHECK(lb_code_unload(module)==LB_CODE_OK && lb_code_space_destroy(space)==LB_CODE_OK);
    }
    printf("CORE_DECIMAL_LOAD_OK failure_prefixes=%zu retry_each=true\n",calls);
}
static LbBytesView name(const char *s) { return (LbBytesView){(const unsigned char *)s,strlen(s)}; }
static void wait_call(LbCall *call,LbWorldStatus expected)
{
    size_t i; LbBytesView view; LbWorldStatus status=LB_WORLD_PENDING; const struct timespec pause={0,1000000};
    for(i=0;i<5000 && status==LB_WORLD_PENDING;++i) { status=lb_call_poll(call,&view); if(status==LB_WORLD_PENDING) nanosleep(&pause,NULL); }
    if(status!=expected) fprintf(stderr,"decimal worker status=%d expected=%d polls=%zu\n",status,expected,i);
    CHECK(status==expected && lb_call_consume(call)==LB_WORLD_OK); lb_call_release(call);
}
static void worker_retirement(LbEngine *engine,Memory *m,LbBytesView image)
{
    LbWorld *a,*b; LbCall *slow,*peer; LbReclamation *reclamation; LbBytesView empty={NULL,0};
    size_t i; int pending=0; const struct timespec pause={0,1000000};
    CHECK(lb_world_create(engine,&a)==LB_WORLD_OK && lb_world_create(engine,&b)==LB_WORLD_OK);
    CHECK(lb_world_load(a,image)==LB_WORLD_OK && lb_world_load(b,image)==LB_WORLD_OK);
    CHECK(lb_world_start(b,name("decimal_slice"),name("small"),empty,&peer)==LB_WORLD_OK); wait_call(peer,LB_WORLD_OK);
    CHECK(lb_world_start(a,name("decimal_slice"),name("slow"),empty,&slow)==LB_WORLD_OK);
    for(i=0;i<5000 && !pending;++i) {
        lb_engine_enter(engine);
        pending=m->worker_large_base && !m->worker_large_released && slow->status==LB_WORLD_PENDING;
        lb_engine_leave(engine); if(!pending) nanosleep(&pause,NULL);
    }
    CHECK(pending);
    CHECK(lb_world_call(b,name("decimal_slice"),name("large"),empty,&peer)==LB_WORLD_OK); wait_call(peer,LB_WORLD_OK);
    CHECK(lb_world_stop(a,&reclamation)==LB_WORLD_OK); wait_call(slow,LB_WORLD_CANCELLED);
    for(i=0;i<5000 && lb_reclamation_poll(reclamation)==LB_WORLD_PENDING;++i) nanosleep(&pause,NULL);
    CHECK(lb_reclamation_poll(reclamation)==LB_WORLD_OK);
    lb_engine_enter(engine); CHECK(m->worker_large_released && !a->space && b->space); lb_engine_leave(engine);
    lb_reclamation_release(reclamation); lb_world_release(a);
    CHECK(lb_world_call(b,name("decimal_slice"),name("small"),empty,&peer)==LB_WORLD_OK); wait_call(peer,LB_WORLD_OK);
    lb_world_release(b);
    /* This witness isolates the 64 KiB transport boundary. Export-directory
     * work can legitimately exhaust the default invocation budget before a
     * 78,914-byte decimal result is ready; do not conflate those two limits. */
    LbWorld *transport; LbResourceOverrides output_only={.has_work=1,.invocation_work=UINT64_MAX};
    CHECK(lb_world_create_with_overrides(engine,&output_only,&transport)==LB_WORLD_OK);
    CHECK(lb_world_load(transport,image)==LB_WORLD_OK);
    CHECK(lb_world_start(transport,name("decimal_slice"),name("too_large"),empty,&peer)==LB_WORLD_OK); wait_call(peer,LB_WORLD_LIMIT);
    lb_world_release(transport);
    puts("CORE_DECIMAL_WORKER_OK real_continuation_allocation=true peer_progress=true cancelled_scratch_released=true physical_world_retirement=true oversized_result_refused=true");
}
static void execution(const char *path,Memory *m,LbSystemAllocator *allocator)
{
    FILE *file=fopen(path,"rb"); long length; unsigned char *data;
    LbEngine *engine; LbCodeSpace *space; LbCodeModule *module; LbBeamError error; LbCodeStatus status;
    CHECK(file && !fseek(file,0,SEEK_END)); length=ftell(file); CHECK(length>0 && length<8*1024*1024 && !fseek(file,0,SEEK_SET));
    data=malloc((size_t)length); CHECK(data && fread(data,1,(size_t)length,file)==(size_t)length && !fclose(file));
    CHECK(lb_engine_create(allocator,&engine)==LB_ENGINE_OK && lb_code_space_create(engine,&space)==LB_CODE_OK);
    status=lb_code_load(space,data,(size_t)length,&module,&error);
    if(status!=LB_CODE_OK) fprintf(stderr,"status=%d stage=%s offset=%zu\n",status,error.stage,error.offset);
    CHECK(status==LB_CODE_OK); semantics(space,module); pending_retirement(space,module,m); failures(space,module,m);
    CHECK(lb_code_unload(module)==LB_CODE_OK && lb_code_space_destroy(space)==LB_CODE_OK);
    load_failures(engine,m,(LbBytesView){data,(size_t)length});
    worker_retirement(engine,m,(LbBytesView){data,(size_t)length});
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine); free(data);
    puts("CORE_DECIMAL_EXECUTION_OK real_bif=true continuation_gc=true offheap_result=true partial_destroy=true three_oom_prefixes=true retry=true");
}
int main(int argc,char **argv)
{
    Memory memory={0}; LbSystemAllocator allocator={&memory,allocate,release}; LbAllocDomain *domain;
    memory.host=pthread_self();
    CHECK(lb_alloc_domain_create(&allocator,&domain)==LB_ALLOC_OK); components(domain,&memory);
    CHECK(lb_alloc_domain_destroy(domain)==LB_ALLOC_OK && !memory.live && !memory.bytes);
    CHECK(argc==1 || argc==2); if(argc==2) execution(argv[1],&memory,&allocator);
    CHECK(!memory.live && !memory.bytes); return 0;
}
