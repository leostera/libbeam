/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * C-owned world/admission/completion state over the real loader and worker.
 */
#include "world_internal.h"
#include "binary.h"

static LbWorldStatus code_status(LbCodeStatus status)
{
    switch(status) {
    case LB_CODE_OK: return LB_WORLD_OK;
    case LB_CODE_NO_MEMORY: return LB_WORLD_NO_MEMORY;
    case LB_CODE_FORMAT: return LB_WORLD_BAD_BEAM;
    case LB_CODE_LIMIT: return LB_WORLD_LIMIT;
    case LB_CODE_UNSUPPORTED: case LB_CODE_UNRESOLVED: return LB_WORLD_UNSUPPORTED;
    case LB_CODE_NOT_FOUND: return LB_WORLD_NOT_FOUND;
    case LB_CODE_CLOSED: return LB_WORLD_CLOSED;
    case LB_CODE_EXISTS: case LB_CODE_BUSY: return LB_WORLD_INVALID_STATE;
    default: return LB_WORLD_INVALID;
    }
}
static int closed(const LbWorld *world) { return world->phase>=LB_WORLD_STOPPING; }
static void world_unref(LbWorld *world)
{
    LbEngine *engine=world->engine;
    LbWorld **link;
    if(!world->references) abort();
    if(--world->references) return;
    if(!engine->control_borrow || world->owner_live || world->phase!=LB_WORLD_RECLAIMED || world->calls ||
       world->outstanding || world->queued_bytes) abort();
    for(link=&engine->worlds;*link && *link!=world;link=&(*link)->next) {}
    if(!*link) abort();
    *link=world->next; lb_release(engine->domain,world);
    /* Host callers still hold the Engine control borrow. Worker completion
     * cannot remove the final world reference: its host owner remains live. */
}
LbWorldStatus lb_world_create(LbEngine *engine,LbWorld **out)
{
    LbWorld *world; void *memory; LbCodeStatus status;
    if(!out) return LB_WORLD_INVALID;
    *out=NULL; if(!engine) return LB_WORLD_INVALID;
    if(!lb_engine_is_open(engine)) return LB_WORLD_CLOSED;
    lb_engine_enter(engine);
    if(lb_alloc_domain_allocate(engine->domain,sizeof(*world),&memory)!=LB_ALLOC_OK) {
        lb_engine_leave(engine); return LB_WORLD_NO_MEMORY;
    }
    world=memory; memset(world,0,sizeof(*world)); world->engine=engine;
    status=lb_code_space_create(engine,&world->space);
    if(status!=LB_CODE_OK) { lb_release(engine->domain,world); lb_engine_leave(engine); return code_status(status); }
    world->references=1; world->owner_live=1; world->phase=LB_WORLD_LOADING;
    world->next=engine->worlds; engine->worlds=world; *out=world;
    lb_engine_leave(engine); return LB_WORLD_OK;
}
LbWorldStatus lb_world_load(LbWorld *world,LbBytesView bytes)
{
    LbWorldStatus status; LbCodeModule *module; LbBeamError error;
    if(!world) return LB_WORLD_INVALID;
    lb_engine_enter(world->engine);
    if(closed(world)) status=LB_WORLD_CLOSED;
    else if(bytes.size && !bytes.data) status=LB_WORLD_INVALID;
    else if(world->phase!=LB_WORLD_LOADING) status=LB_WORLD_INVALID_STATE;
    else status=code_status(lb_code_load(world->space,bytes.data,bytes.size,&module,&error));
    lb_engine_leave(world->engine); return status;
}
static LbWorldStatus resolve(LbWorld *world,LbBytesView module,LbBytesView function,LbCodeEntry **entry)
{
    Eterm m,f; LbAtomStatus status;
    status=lb_atoms_find(world->space->atoms,module.data,module.size,&m);
    if(status==LB_ATOM_OK) status=lb_atoms_find(world->space->atoms,function.data,function.size,&f);
    if(status!=LB_ATOM_OK) return status==LB_ATOM_NOT_FOUND?LB_WORLD_NOT_FOUND:LB_WORLD_INVALID;
    return code_status(lb_code_entry_acquire(world->space,m,f,1,entry));
}
static LbWorldStatus admit(LbWorld *world,LbBytesView module,LbBytesView function,
                           LbBytesView input,int starting,LbCall **out)
{
    LbCall *call; LbCodeEntry *entry=NULL; void *memory;
    LbWorldStatus status; LbEngineStatus task_status; size_t charge;
    if(!out) return LB_WORLD_INVALID;
    *out=NULL;
    if(!world) return LB_WORLD_INVALID;
    lb_engine_enter(world->engine);
    if(closed(world)) { status=LB_WORLD_CLOSED; goto unlock; }
    if((module.size && !module.data) || (function.size && !function.data) ||
       (input.size && !input.data) || module.size>1020 || function.size>1020) {
        status=LB_WORLD_INVALID; goto unlock;
    }
    if(input.size>LB_CALL_BYTES_LIMIT) { status=LB_WORLD_LIMIT; goto unlock; }
    charge=input.size+LB_CALL_BYTES_LIMIT; /* reserve worst-case output before accepting */
    if(world->phase!=(starting?LB_WORLD_LOADING:LB_WORLD_RUNNING)) { status=LB_WORLD_INVALID_STATE; goto unlock; }
    if(world->references==SIZE_MAX) { status=LB_WORLD_LIMIT; goto unlock; }
    if(world->outstanding==LB_CALL_COUNT_LIMIT || charge>LB_CALL_QUEUE_LIMIT-world->queued_bytes) {
        status=LB_WORLD_FULL; goto unlock;
    }
    status=resolve(world,module,function,&entry); if(status!=LB_WORLD_OK) goto unlock;
    if(lb_alloc_domain_allocate(world->engine->domain,sizeof(*call),&memory)!=LB_ALLOC_OK) {
        status=LB_WORLD_NO_MEMORY; goto release_entry;
    }
    call=memory; memset(call,0,sizeof(*call)); call->world=world;
    call->status=LB_WORLD_PENDING; call->owner_live=1; call->slot_held=1; call->charge=charge;
    task_status=lb_task_create_call_locked(world->engine,entry,input,call,&call->task);
    if(task_status!=LB_ENGINE_OK) {
        lb_release(world->engine->domain,call);
        status=task_status==LB_ENGINE_NO_MEMORY?LB_WORLD_NO_MEMORY:LB_WORLD_INVALID; goto release_entry;
    }
    /* Worker publication is protected by the same mutex. No fallible work
     * follows; failed start never leaves a half-started world or lost status. */
    call->next=world->calls; world->calls=call; ++world->references;
    ++world->outstanding; world->queued_bytes+=charge;
    if(starting) world->phase=LB_WORLD_RUNNING;
    *out=call; status=LB_WORLD_OK;
release_entry:
    lb_code_entry_release(entry);
unlock:
    lb_engine_leave(world->engine); return status;
}
LbWorldStatus lb_world_start(LbWorld *w,LbBytesView m,LbBytesView f,LbBytesView b,LbCall **out)
{ return admit(w,m,f,b,1,out); }
LbWorldStatus lb_world_call(LbWorld *w,LbBytesView m,LbBytesView f,LbBytesView b,LbCall **out)
{ return admit(w,m,f,b,0,out); }

static LbWorldStatus copy_result(LbCall *call,LbProcess *process)
{
    LbBitstringView view; size_t size,i,offset; unsigned shift; void *memory;
    if(!lb_bitstring_view(lb_process_result(process),&view) || view.bit_size%8) return LB_WORLD_BAD_RESULT;
    size=view.bit_size/8; if(size>LB_CALL_BYTES_LIMIT) return LB_WORLD_LIMIT;
    if(!size) return LB_WORLD_OK;
    if(lb_alloc_domain_allocate(call->world->engine->domain,size,&memory)!=LB_ALLOC_OK) return LB_WORLD_NO_MEMORY;
    call->bytes=memory; call->size=size;
    offset=view.bit_offset/8; shift=(unsigned)(view.bit_offset%8);
    if(!shift) memcpy(call->bytes,view.data+offset,size);
    else for(i=0;i<size;++i)
        call->bytes[i]=(unsigned char)((view.data[offset+i]<<shift)|(view.data[offset+i+1]>>(8-shift)));
    return LB_WORLD_OK;
}
void lb_call_complete(LbCall *call,LbProcess *process,LbTaskStatus status)
{
    LbWorld *world=call->world;
    if(call->status!=LB_WORLD_PENDING || world->queued_bytes<call->charge) abort();
    world->queued_bytes-=call->charge; call->charge=0;
    switch(status) {
    case LB_TASK_DONE: call->status=call->owner_live?copy_result(call,process):LB_WORLD_OK; break;
    case LB_TASK_EXCEPTION: call->status=LB_WORLD_EXCEPTION; break;
    case LB_TASK_NO_MEMORY: call->status=LB_WORLD_NO_MEMORY; break;
    case LB_TASK_CANCELLED: call->status=LB_WORLD_CANCELLED; break;
    default: abort();
    }
    call->charge=call->size;
    if(call->charge>LB_CALL_QUEUE_LIMIT-world->queued_bytes) abort();
    world->queued_bytes+=call->charge;
}
static void release_payload(LbCall *call)
{
    LbWorld *world=call->world;
    lb_release(world->engine->domain,call->bytes); call->bytes=NULL; call->size=0;
    if(call->charge>world->queued_bytes) abort();
    world->queued_bytes-=call->charge; call->charge=0;
    if(call->slot_held) {
        if(!world->outstanding) abort(); --world->outstanding; call->slot_held=0;
    }
}
void lb_call_discard_completed(LbCall *call)
{
    LbWorld *world=call->world; LbCall **link;
    if(call->owner_live || call->status==LB_WORLD_PENDING || call->task) abort();
    for(link=&world->calls;*link && *link!=call;link=&(*link)->next) {}
    if(!*link) abort(); *link=call->next;
    release_payload(call); lb_release(world->engine->domain,call); world_unref(world);
}
LbWorldStatus lb_call_poll(LbCall *call,LbBytesView *out)
{
    LbWorldStatus status;
    if(!out) return LB_WORLD_INVALID;
    *out=(LbBytesView){NULL,0}; if(!call) return LB_WORLD_INVALID;
    lb_engine_enter(call->world->engine);
    status=call->consumed?LB_WORLD_CLOSED:call->status;
    if(status==LB_WORLD_OK) *out=(LbBytesView){call->bytes,call->size};
    lb_engine_leave(call->world->engine); return status;
}
LbWorldStatus lb_call_consume(LbCall *call)
{
    LbWorldStatus status; LbEngine *engine;
    if(!call) return LB_WORLD_INVALID;
    engine=call->world->engine; lb_engine_enter(engine);
    if(call->consumed) status=LB_WORLD_CLOSED;
    else if(call->status==LB_WORLD_PENDING) status=LB_WORLD_PENDING;
    else {
        release_payload(call); call->consumed=1;
        lb_task_release_locked(call->task); call->task=NULL; status=LB_WORLD_OK;
    }
    lb_engine_leave(engine); return status;
}
void lb_call_release(LbCall *call)
{
    LbEngine *engine;
    if(!call) return;
    engine=call->world->engine; lb_engine_enter(engine);
    if(!call->owner_live) abort(); call->owner_live=0;
    if(call->status!=LB_WORLD_PENDING) {
        lb_task_release_locked(call->task); call->task=NULL; lb_call_discard_completed(call);
    } /* A dropped pending handle does not cancel accepted guest work. */
    lb_engine_leave(engine);
}

static void retire_one(LbWorld *world)
{
    if(world->space->modules) {
        /* Eager publication only imports older modules (or self). Retiring
         * newest first is a reverse dependency order; no hot-load cycles exist. */
        if(lb_code_unload(world->space->modules)!=LB_CODE_OK) abort();
        return;
    }
    if(lb_code_space_destroy(world->space)!=LB_CODE_OK) abort();
    world->space=NULL; world->phase=LB_WORLD_RECLAIMED;
}
int lb_world_retire_step(LbEngine *engine)
{
    LbWorld *world; int progress=0;
    for(world=engine->worlds;world;world=world->next)
        if(world->phase==LB_WORLD_STOPPING) { retire_one(world); progress=1; }
    return progress;
}
static void close_admission(LbWorld *world)
{
    LbCall *call,*next;
    world->phase=LB_WORLD_STOPPING;
    for(call=world->calls;call;call=next) {
        next=call->next;
        if(call->status==LB_WORLD_PENDING) lb_task_cancel_locked(call->task);
    }
    if(!world->engine->worker_started)
        while(world->phase!=LB_WORLD_RECLAIMED) retire_one(world);
    else if(pthread_cond_signal(&world->engine->wake)) abort();
}
LbWorldStatus lb_world_stop(LbWorld *world,LbReclamation **out)
{
    LbReclamation *reclamation; void *memory; LbWorldStatus status;
    if(!out) return LB_WORLD_INVALID;
    *out=NULL; if(!world) return LB_WORLD_INVALID;
    lb_engine_enter(world->engine);
    if(closed(world)) status=LB_WORLD_CLOSED;
    else if(world->references==SIZE_MAX) status=LB_WORLD_LIMIT;
    else if(lb_alloc_domain_allocate(world->engine->domain,sizeof(*reclamation),&memory)!=LB_ALLOC_OK)
        status=LB_WORLD_NO_MEMORY;
    else {
        reclamation=memory; reclamation->world=world; reclamation->consumed=0; ++world->references;
        close_admission(world); *out=reclamation; status=LB_WORLD_OK;
    }
    lb_engine_leave(world->engine); return status;
}
void lb_world_release(LbWorld *world)
{
    LbEngine *engine;
    if(!world) return;
    engine=world->engine; lb_engine_enter(engine);
    if(!world->owner_live) abort();
    if(!closed(world)) close_admission(world);
    /* Destruction cannot strand an orphan world waiting for another host wait.
     * All guest processes are at a safepoint; this is cleanup, not host execution. */
    while(world->phase!=LB_WORLD_RECLAIMED) retire_one(world);
    world->owner_live=0; world_unref(world); lb_engine_leave(engine);
}
LbWorldStatus lb_reclamation_poll(LbReclamation *reclamation)
{
    LbWorldStatus status;
    if(!reclamation) return LB_WORLD_INVALID;
    lb_engine_enter(reclamation->world->engine);
    status=reclamation->consumed?LB_WORLD_CLOSED:
        (reclamation->world->phase==LB_WORLD_RECLAIMED?LB_WORLD_OK:LB_WORLD_PENDING);
    lb_engine_leave(reclamation->world->engine); return status;
}
LbWorldStatus lb_reclamation_consume(LbReclamation *reclamation)
{
    LbWorldStatus status;
    if(!reclamation) return LB_WORLD_INVALID;
    lb_engine_enter(reclamation->world->engine);
    status=reclamation->consumed?LB_WORLD_CLOSED:
        (reclamation->world->phase==LB_WORLD_RECLAIMED?LB_WORLD_OK:LB_WORLD_PENDING);
    if(status==LB_WORLD_OK) reclamation->consumed=1;
    lb_engine_leave(reclamation->world->engine); return status;
}
void lb_reclamation_release(LbReclamation *reclamation)
{
    LbWorld *world; LbEngine *engine;
    if(!reclamation) return;
    world=reclamation->world; engine=world->engine; lb_engine_enter(engine);
    lb_release(engine->domain,reclamation); world_unref(world); lb_engine_leave(engine);
}
