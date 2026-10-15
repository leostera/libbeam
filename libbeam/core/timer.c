/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Engine-serialized delayed messages; timer records never borrow guest heaps.
 */
#define _POSIX_C_SOURCE 200809L
#include "timer.h"
#include "world_internal.h"
#include "message.h"
#include "alias.h"
#include "reference.h"
#include "big.h"
#include "work_budget_internal.h"
#include "time_unit.h"
#include <time.h>

struct LbTimer {
    struct LbTimer *next;
    uint64_t deadline_ns;
    uint32_t reference[3];
    Eterm destination; /* Immediate PID/name; boxed alias terms are never borrowed. */
    LbAlias *destination_alias;
    LbMessage *message;
};

static int now_ns(uint64_t *result)
{
    struct timespec now;
    if(clock_gettime(CLOCK_MONOTONIC,&now) || now.tv_sec<0 || now.tv_nsec<0 ||
       (uint64_t)now.tv_nsec>=LB_NATIVE_TIME_UNITS_PER_SECOND ||
       (uint64_t)now.tv_sec>(UINT64_MAX-(uint64_t)now.tv_nsec)/LB_NATIVE_TIME_UNITS_PER_SECOND) return 0;
    *result=(uint64_t)now.tv_sec*LB_NATIVE_TIME_UNITS_PER_SECOND+(uint64_t)now.tv_nsec;
    return 1;
}
static int timeout_milliseconds(Eterm value,uint64_t *milliseconds)
{
    if(is_small(value)) {
        Sint signed_value=signed_val(value);
        if(signed_value<0) return 0;
        *milliseconds=(uint64_t)signed_value; return 1;
    }
    if(!lb_is_big(value) || (*boxed_val(value)&_TAG_HEADER_MASK)==NEG_BIG_SUBTAG) return 0;
    size_t digits=lb_big_size(value);
    if(!digits || digits>1) return -1;
    *milliseconds=boxed_val(value)[1]; return 1;
}
static void fail_status(LbProcess *p,LbProcessStatus status)
{
    switch(status) {
    case LB_PROCESS_LIMIT: p->status=LB_PROCESS_LIMIT; break;
    case LB_PROCESS_WORK_LIMIT: p->status=LB_PROCESS_WORK_LIMIT; break;
    case LB_PROCESS_NO_MEMORY: p->status=LB_PROCESS_NO_MEMORY; break;
    default: p->status=status; break;
    }
}
static Eterm schedule(LbProcess *p,Eterm *args,int start_timer,int absolute)
{
    LbCodeSpace *space=p->entry_module->space; LbWorld *world=p->world;
    LbEngine *engine=space->engine; LbAllocDomain *domain=space->domain;
    uint64_t milliseconds,now;
    p->last_native_cost=1;
    if(!lb_work_domain_charge(domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    int timeout=timeout_milliseconds(args[0],&milliseconds);
    if(!timeout) goto badarg;
    if(timeout<0) goto limit;
    if(!is_internal_pid(args[1]) && !is_atom(args[1]) && !lb_is_reference(args[1])) {
        if(is_pid(args[1])) goto refused;
        goto badarg;
    }
    if(!world || !p->running || world->phase!=LB_WORLD_RUNNING) goto refused;
    if(engine->reference_counter==UINT64_MAX) goto limit;
    if(!now_ns(&now)) { p->status=LB_PROCESS_ENVIRONMENT; return THE_NON_VALUE; }
    if(milliseconds>UINT64_MAX/UINT64_C(1000000)) goto limit;
    uint64_t requested_ns=milliseconds*UINT64_C(1000000);
    if(!absolute && requested_ns>UINT64_MAX-now) goto limit;
    uint64_t deadline_ns=absolute?requested_ns:now+requested_ns;
    if(!lb_work_domain_charge(domain,LB_REF_WORDS+2)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    p->last_native_cost+=LB_REF_WORDS+2;
    size_t reserve_words=LB_REF_WORDS+(start_timer?4:0),need;
    if(!lb_size_add(reserve_words,p->heap_reserved,&need)) goto limit;
    size_t collections=p->collections;
    if(!lb_heap_reserve(p,need,MAX_REG)) {
        LbAllocStatus cause=lb_alloc_domain_failure(domain);
        fail_status(p,cause==LB_ALLOC_WORK_LIMIT?LB_PROCESS_WORK_LIMIT:
            cause==LB_ALLOC_NO_MEMORY?LB_PROCESS_NO_MEMORY:LB_PROCESS_LIMIT);
        return THE_NON_VALUE;
    }
    if(p->collections!=collections) p->last_native_cost+=p->last_gc_cost;
    /* The X-register pointer is stable across GC; read the moved destination
     * and message only after reserving the timer's reference/result footprint. */
    Eterm destination=args[1],payload=args[2],sender;
    LbAlias *destination_alias=NULL;
    if(lb_is_reference(destination)) {
        destination_alias=lb_alias_lookup(space,destination,&p->last_native_cost);
        if(lb_alloc_domain_failure(domain)==LB_ALLOC_WORK_LIMIT) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
        if(!destination_alias) goto badarg;
    }
    LbProcessStatus status=lb_process_identity(p,&sender);
    if(status!=LB_PROCESS_READY) { p->status=status; return THE_NON_VALUE; }
    Eterm *top=p->htop;
    Eterm reference=lb_reference_write(p->htop,engine->reference_counter); p->htop+=LB_REF_WORDS;
    Eterm message_term=payload;
    if(start_timer) { message_term=TUPLE3(p->htop,am_timeout,reference,payload); p->htop+=4; }
    void *memory;
    if(!lb_work_domain_charge(domain,1)) { p->htop=top; p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    status=lb_alloc_domain_allocate(domain,sizeof(LbTimer),&memory)==LB_ALLOC_OK?LB_PROCESS_READY:
        lb_alloc_domain_failure(domain)==LB_ALLOC_LIMIT?LB_PROCESS_LIMIT:
        lb_alloc_domain_failure(domain)==LB_ALLOC_WORK_LIMIT?LB_PROCESS_WORK_LIMIT:LB_PROCESS_NO_MEMORY;
    if(status!=LB_PROCESS_READY) { p->htop=top; p->status=status; return THE_NON_VALUE; }
    LbTimer *timer=memory; memset(timer,0,sizeof(*timer));
    timer->destination=destination_alias?NIL:destination; timer->destination_alias=destination_alias;
    Uint message_cost=0;
    status=lb_message_create(space,message_term,&timer->message,&message_cost);
    p->last_native_cost+=message_cost;
    if(status!=LB_PROCESS_READY) {
        lb_release(domain,timer); p->htop=top; p->status=status; return THE_NON_VALUE;
    }
    timer->message->m[2]=sender;
    const LbOrdinaryRef *ref=lb_reference(reference);
    memcpy(timer->reference,ref->num,sizeof(timer->reference));
    timer->deadline_ns=deadline_ns;
    timer->next=world->timers; world->timers=timer;
    ++engine->reference_counter;
    if(start_timer) p->htop=top+LB_REF_WORDS;
    if(pthread_cond_signal(&engine->wake)) abort();
    return reference;
badarg: p->freason=LB_EXC_BADARG; return THE_NON_VALUE;
refused: p->status=LB_PROCESS_REFUSED; return THE_NON_VALUE;
limit: p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE;
}
static int parse_absolute_option(LbProcess *p,Eterm options,int *absolute)
{
    size_t count=0; *absolute=0;
    while(is_list(options)) {
        if(!lb_work_domain_charge(p->entry_module->space->domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return 0; }
        if(++count>64) { p->status=LB_PROCESS_LIMIT; return 0; }
        Eterm option=CAR(list_val(options)); options=CDR(list_val(options));
        if(!is_tuple(option)) { p->freason=LB_EXC_BADARG; return 0; }
        Eterm *tuple=tuple_val(option);
        if(arityval(tuple[0])!=2 || tuple[1]!=am_abs ||
           (tuple[2]!=am_true && tuple[2]!=am_false)) { p->freason=LB_EXC_BADARG; return 0; }
        *absolute=tuple[2]==am_true;
    }
    if(!is_nil(options)) { p->freason=LB_EXC_BADARG; return 0; }
    return 1;
}
Eterm lb_bif_send_after_3(LbProcess *p,Eterm *args,const BeamInstr *pc)
{ (void)pc; return schedule(p,args,0,0); }
Eterm lb_bif_start_timer_3(LbProcess *p,Eterm *args,const BeamInstr *pc)
{ (void)pc; return schedule(p,args,1,0); }
Eterm lb_bif_send_after_4(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)pc; int absolute;
    if(!parse_absolute_option(p,args[3],&absolute)) return THE_NON_VALUE;
    return schedule(p,args,0,absolute);
}
Eterm lb_bif_start_timer_4(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)pc; int absolute;
    if(!parse_absolute_option(p,args[3],&absolute)) return THE_NON_VALUE;
    return schedule(p,args,1,absolute);
}

static void destroy_timer(LbWorld *world,LbTimer *timer);
static int timer_matches(const LbTimer *timer,Eterm reference)
{
    const LbOrdinaryRef *ref=lb_reference(reference);
    return ref && !memcmp(timer->reference,ref->num,sizeof(timer->reference));
}
static Sint remaining_milliseconds(const LbTimer *timer,uint64_t now)
{
    uint64_t remaining=timer->deadline_ns>now?timer->deadline_ns-now:0;
    return (Sint)(remaining/UINT64_C(1000000));
}
static LbTimer *find_timer(LbProcess *p,Eterm reference,LbTimer ***owner)
{
    LbTimer **link=&p->world->timers;
    while(*link) {
        if(!lb_work_domain_charge(p->entry_module->space->domain,1)) return NULL;
        ++p->last_native_cost;
        if(timer_matches(*link,reference)) { if(owner) *owner=link; return *link; }
        link=&(*link)->next;
    }
    return NULL;
}
Eterm lb_bif_read_timer_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)pc; p->last_native_cost=1;
    if(!lb_work_domain_charge(p->entry_module->space->domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    if(!lb_reference(args[0])) { p->freason=LB_EXC_BADARG; return THE_NON_VALUE; }
    if(!p->world) { p->status=LB_PROCESS_REFUSED; return THE_NON_VALUE; }
    LbTimer *timer=find_timer(p,args[0],NULL);
    if(lb_alloc_domain_failure(p->entry_module->space->domain)==LB_ALLOC_WORK_LIMIT) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    if(!timer) return am_false;
    uint64_t now; if(!now_ns(&now)) { p->status=LB_PROCESS_ENVIRONMENT; return THE_NON_VALUE; }
    return make_small(remaining_milliseconds(timer,now));
}
static Eterm cancel_timer_sync(LbProcess *p,Eterm reference,int info)
{
    LbAllocDomain *domain=p->entry_module->space->domain;
    if(!lb_work_domain_charge(domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    ++p->last_native_cost;
    if(!lb_reference(reference)) { p->freason=LB_EXC_BADARG; return THE_NON_VALUE; }
    if(!p->world) { p->status=LB_PROCESS_REFUSED; return THE_NON_VALUE; }
    LbTimer **owner=NULL; LbTimer *timer=find_timer(p,reference,&owner);
    if(lb_alloc_domain_failure(domain)==LB_ALLOC_WORK_LIMIT) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    if(!timer) return info?am_false:am_ok;
    uint64_t now; if(!now_ns(&now)) { p->status=LB_PROCESS_ENVIRONMENT; return THE_NON_VALUE; }
    Sint remaining=remaining_milliseconds(timer,now); *owner=timer->next; destroy_timer(p->world,timer);
    return info?make_small(remaining):am_ok;
}
Eterm lb_bif_cancel_timer_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)pc; p->last_native_cost=0;
    return cancel_timer_sync(p,args[0],1);
}
static int parse_cancel_options(LbProcess *p,Eterm options,int *async,int *info)
{
    *async=0; *info=1; size_t count=0;
    while(is_list(options)) {
        if(!lb_work_domain_charge(p->entry_module->space->domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return 0; }
        if(++count>64) { p->status=LB_PROCESS_LIMIT; return 0; }
        ++p->last_native_cost;
        Eterm option=CAR(list_val(options)); options=CDR(list_val(options));
        if(!is_tuple(option)) { p->freason=LB_EXC_BADARG; return 0; }
        Eterm *tuple=tuple_val(option);
        if(arityval(tuple[0])!=2 || (tuple[2]!=am_true && tuple[2]!=am_false)) {
            p->freason=LB_EXC_BADARG; return 0;
        }
        if(tuple[1]==am_async) *async=tuple[2]==am_true;
        else if(tuple[1]==am_info) *info=tuple[2]==am_true;
        else { p->freason=LB_EXC_BADARG; return 0; }
    }
    if(!is_nil(options)) { p->freason=LB_EXC_BADARG; return 0; }
    return 1;
}
Eterm lb_bif_cancel_timer_2(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)pc; p->last_native_cost=0; int async,info;
    if(!parse_cancel_options(p,args[1],&async,&info)) return THE_NON_VALUE;
    if(async) { p->status=LB_PROCESS_REFUSED; return THE_NON_VALUE; }
    return cancel_timer_sync(p,args[0],info);
}

static void unlink_timer(LbWorld *world,LbTimer *timer)
{
    LbTimer **link=&world->timers;
    while(*link && *link!=timer) link=&(*link)->next;
    if(!*link) abort(); *link=timer->next;
}
static void destroy_timer(LbWorld *world,LbTimer *timer)
{
    if(timer->message) lb_message_destroy(timer->message);
    lb_release(&world->domain,timer);
}
void lb_timer_cancel_world(LbWorld *world)
{
    if(!world) return;
    LbTimer *timer=world->timers; world->timers=NULL;
    while(timer) { LbTimer *next=timer->next; destroy_timer(world,timer); timer=next; }
}
int lb_timer_engine_pending(const LbEngine *engine)
{
    for(const LbWorld *world=engine->worlds;world;world=world->next)
        if(world->timers) return 1;
    return 0;
}
static LbProcess *destination_process(LbWorld *world,LbTimer *timer,LbAlias **out_alias)
{
    *out_alias=NULL;
    if(timer->destination_alias) {
        LbAlias *alias=timer->destination_alias;
        if(!alias->active) return NULL;
        *out_alias=alias; return alias->owner;
    }
    if(is_internal_pid(timer->destination)) return lb_pid_lookup(world->space->pids,timer->destination);
    Uint cost=0; return lb_registry_lookup(world->space,timer->destination,&cost);
}
static void expire_timer(LbWorld *world,LbTimer *timer)
{
    LbMessage *message=timer->message; LbWorkBudget *budget=message->budget;
    LbAllocDomain *domain=&world->domain; LbProcess *receiver=NULL;
    lb_alloc_domain_begin_operation(domain); lb_work_domain_borrow(domain,budget);
    LbAllocStatus cause=LB_ALLOC_OK; LbProcessStatus delivered=LB_PROCESS_READY;
    if(!lb_work_domain_charge(domain,1)) cause=LB_ALLOC_WORK_LIMIT;
    else {
        LbAlias *alias=NULL;
        receiver=destination_process(world,timer,&alias);
        cause=lb_alloc_domain_failure(domain);
        if(cause==LB_ALLOC_OK && receiver) {
            if(alias && alias->reply) message->reply_alias=alias;
            delivered=lb_message_deliver(receiver,message);
            if(delivered==LB_PROCESS_READY) timer->message=NULL;
        }
        if(cause==LB_ALLOC_OK && delivered!=LB_PROCESS_READY) {
            cause=delivered==LB_PROCESS_WORK_LIMIT?LB_ALLOC_WORK_LIMIT:
                delivered==LB_PROCESS_NO_MEMORY?LB_ALLOC_NO_MEMORY:
                delivered==LB_PROCESS_LIMIT?LB_ALLOC_LIMIT:LB_ALLOC_INVALID;
        }
    }
    lb_work_domain_borrow(domain,NULL); lb_work_domain_clear_failure(domain);
    if(cause!=LB_ALLOC_OK) {
        if(lb_work_budget_retain(budget)!=LB_ALLOC_OK) abort();
        destroy_timer(world,timer);
        if(cause==LB_ALLOC_WORK_LIMIT || cause==LB_ALLOC_LIMIT || cause==LB_ALLOC_NO_MEMORY) {
            LbTaskStatus status=cause==LB_ALLOC_WORK_LIMIT?LB_TASK_WORK_LIMIT:
                cause==LB_ALLOC_LIMIT?LB_TASK_MEMORY_LIMIT:LB_TASK_NO_MEMORY;
            lb_task_cancel_family_locked(budget,status);
        } else lb_world_close_for_limit_locked(world);
        lb_work_budget_release(budget);
        return;
    }
    destroy_timer(world,timer);
}
void lb_timer_poll_engine(LbEngine *engine)
{
    uint64_t now;
    if(!lb_timer_engine_pending(engine)) return;
    if(!now_ns(&now)) {
        for(LbWorld *world=engine->worlds;world;world=world->next) lb_timer_cancel_world(world);
        return;
    }
    for(LbWorld *world=engine->worlds;world;world=world->next) {
        for(;;) {
            LbTimer *due=NULL;
            for(LbTimer *timer=world->timers;timer;timer=timer->next)
                if(timer->deadline_ns<=now) { due=timer; break; }
            if(!due) break;
            unlink_timer(world,due);
            expire_timer(world,due);
            if(world->phase!=LB_WORLD_RUNNING) break;
            if(!now_ns(&now)) { lb_timer_cancel_world(world); break; }
        }
    }
}
