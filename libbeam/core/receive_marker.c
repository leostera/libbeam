/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 2017-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Native eight-slot marker reuse, reference binding and save boundaries.
 * Only the serialized inner queue exists: anchors borrow next-pointer slots,
 * repaired before any preceding message is unlinked. No distribution/prioq.
 */
#include "receive_marker.h"
#include "process_internal.h"
static int ordinary_ref(Eterm term)
{
    return is_boxed(term) && boxed_val(term)[0]==LB_REF_HEADER &&
        ((const LbOrdinaryRef *)boxed_val(term))->marker==UINT32_MAX;
}
static int equal_id(Eterm a,Eterm b)
{
    if(a==b) return 1;
    return ordinary_ref(a) && ordinary_ref(b) && !memcmp(boxed_val(a),boxed_val(b),LB_REF_WORDS*sizeof(Eterm));
}
static int find(LbRecvMarkerBlock *block,Eterm id)
{
    if(!block || id==am_undefined || id==NIL) return -1;
    for(int i=0;i<LB_RECV_MARKERS;++i) if(equal_id(block->ref[i],id)) return i;
    return -1;
}
LbProcessStatus lb_receive_marker_reserve(LbProcess *p,Eterm *out)
{
    *out=am_undefined; p->last_native_cost=1;
    if(!p->messages.mq_len) return LB_PROCESS_READY;
    LbRecvMarkerBlock *b=p->messages.markers;
    if(!b) {
        void *memory;
        if(lb_alloc_domain_allocate(p->entry_module->space->domain,sizeof(*b),&memory)!=LB_ALLOC_OK) return LB_PROCESS_NO_MEMORY;
        b=memory; memset(b,0,sizeof(*b)); b->used_ix=-1;
        b->pending_set_save_ix=b->set_save_ix=-1;
        for(int i=0;i<LB_RECV_MARKERS;++i) b->ref[i]=am_undefined;
        p->messages.markers=b;
    }
    if(b->sequence==(Uint)MAX_SMALL) return LB_PROCESS_LIMIT;
    int i=-1;
    for(int j=0;j<LB_RECV_MARKERS;++j) if(b->ref[j]==am_undefined) { i=j; break; }
    if(i<0) i=b->marker[b->used_ix].next_ix; /* oldest used slot */
    /* Move the reused slot to the newest end of the native circular index list. */
    LbRecvMarker *m=&b->marker[i];
    if(m->in_msgq) {
        b->marker[m->prev_ix].next_ix=m->next_ix;
        b->marker[m->next_ix].prev_ix=m->prev_ix;
        if(b->used_ix==i) b->used_ix=m->prev_ix==i?-1:m->prev_ix;
    }
    if(b->used_ix<0) m->prev_ix=m->next_ix=(signed char)i;
    else {
        int newest=b->used_ix,oldest=b->marker[newest].next_ix;
        m->prev_ix=(signed char)newest; m->next_ix=(signed char)oldest;
        b->marker[newest].next_ix=(signed char)i; b->marker[oldest].prev_ix=(signed char)i;
    }
    b->used_ix=(signed char)i; m->in_msgq=1; m->pass=0;
    m->prev_next=p->messages.last;
    b->ref[i]=make_small(++b->sequence); *out=b->ref[i]; return LB_PROCESS_READY;
}
void lb_receive_marker_bind(LbProcess *p,Eterm marker,Eterm reference)
{
    LbRecvMarkerBlock *b=p->messages.markers; int i=find(b,marker);
    if(i<0 || !is_small(b->ref[i])) return;
    if(!ordinary_ref(reference)) { b->ref[i]=am_undefined; return; }
    memcpy(b->reference_heap[i],boxed_val(reference),LB_REF_WORDS*sizeof(Eterm));
    b->ref[i]=make_boxed(b->reference_heap[i]);
}
void lb_receive_marker_clear(LbProcess *p,Eterm reference)
{
    if(!ordinary_ref(reference)) return;
    LbRecvMarkerBlock *b=p->messages.markers; int i=find(b,reference);
    if(i>=0) b->ref[i]=am_undefined;
}
void lb_receive_marker_use(LbProcess *p,Eterm reference)
{
    if(!ordinary_ref(reference)) return;
    LbRecvMarkerBlock *b=p->messages.markers; int i=find(b,reference);
    if(i<0) return;
    p->receive_message=NULL; p->receive_root=NIL;
    p->messages.save=b->marker[i].prev_next;
}
void lb_receive_marker_unlink(LbMessageQueue *q,LbMessage *message,LbMessage **replacement)
{
    if(!q->markers) return;
    for(int i=0;i<LB_RECV_MARKERS;++i)
        if(q->markers->marker[i].prev_next==&message->next)
            q->markers->marker[i].prev_next=replacement;
}
void lb_receive_marker_destroy(LbProcess *p)
{
    if(p->messages.markers) {
        if(lb_alloc_domain_release(p->entry_module->space->domain,p->messages.markers)!=LB_ALLOC_OK) abort();
        p->messages.markers=NULL;
    }
}
