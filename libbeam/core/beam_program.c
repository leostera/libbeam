/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#include "beam_program_internal.h"
#include "alloc_internal.h"
#include "utf8.h"
#include "work_budget.h"
#include <stdlib.h>
int lb_pfail(LbBeamProgram *p,LbBeamStatus status)
{
    if(p->error.status==LB_BEAM_OK) p->error.status=status;
    return 0;
}
void *lb_palloc(LbBeamProgram *p,size_t count,size_t size)
{
    size_t bytes; void *memory;
    LbPrepBlock *block;
    if(!count || !size || !lb_size_mul(count,size,&bytes) ||
       !lb_size_add(bytes,sizeof(*block),&bytes) || bytes>LB_BEAM_MAX_BYTES-p->bytes) {
        lb_pfail(p,LB_BEAM_LIMIT); return NULL;
    }
    /* Reserve the eventual single teardown walk incrementally: each new block
     * and optional name contributes constant cleanup work. Recharging the whole
     * existing prefix on every allocation made preparation quadratic. */
    if(!lb_work_domain_charge(p->domain,28)) {
        lb_pfail(p,LB_BEAM_LIMIT); return NULL;
    }
    if(lb_alloc_domain_allocate(p->domain,bytes,&memory)!=LB_ALLOC_OK) {
        lb_pfail(p,LB_BEAM_NO_MEMORY); return NULL;
    }
    block=memory; block->link.previous=NULL; block->link.next=p->blocks; block->link.bytes=bytes;
    if(p->blocks) p->blocks->link.previous=block;
    p->blocks=block; p->bytes+=bytes; ++p->block_count;
    memset(block+1,0,bytes-sizeof(*block)); return block+1;
}
void lb_pfree(LbBeamProgram *p,void *pointer)
{
    LbPrepBlock *block;
    if(!p || !pointer) return;
    block=(LbPrepBlock *)pointer-1;
    if(block->link.previous) block->link.previous->link.next=block->link.next;
    else { if(p->blocks!=block) abort(); p->blocks=block->link.next; }
    if(block->link.next) block->link.next->link.previous=block->link.previous;
    if(p->bytes<block->link.bytes || !p->block_count) abort();
    p->bytes-=block->link.bytes; --p->block_count;
    if(lb_alloc_domain_release_owned(p->domain,block)!=LB_ALLOC_OK) abort();
}
int lb_program_allocate_op_block(LbBeamProgram *p,LbBeamOp **free_ops)
{
    size_t capacity=p->op_block_capacity? p->op_block_capacity*2 : 32,bytes,ops_bytes;
    LbOpBlock *block;
    if(!p || !free_ops) return 0;
    if(capacity>4096) capacity=4096;
    if(capacity<p->op_block_capacity || !lb_size_mul(capacity,sizeof(*block->ops),&ops_bytes) ||
       !lb_size_add(offsetof(LbOpBlock,ops),ops_bytes,&bytes)) return lb_pfail(p,LB_BEAM_LIMIT);
    block=lb_palloc(p,1,bytes); if(!block) return 0;
    block->next=p->op_blocks; block->capacity=capacity; p->op_blocks=block; p->op_block_capacity=capacity;
    for(size_t i=0;i<capacity;++i) block->ops[i].next=i+1<capacity?&block->ops[i+1]:NULL;
    *free_ops=block->ops;
    return 1;
}
void lb_program_discard_ops(LbBeamProgram *p,int discard_chain)
{
    if(!p) return;
    if(discard_chain) for(LbBeamOp *op=p->ops;op;op=op->next)
        if(op->a && op->a!=op->def_args) { lb_pfree(p,op->a); op->a=op->def_args; }
    while(p->op_blocks) {
        LbOpBlock *block=p->op_blocks; p->op_blocks=block->next; lb_pfree(p,block);
    }
    p->ops=p->free_ops=NULL;
}
int lb_pname(LbBeamProgram *p,LbBeamBytes name,Eterm *destination)
{
    LbNamePatch *patch;
    if(!lb_utf8_atom_validate(name.data,name.size)) return lb_pfail(p,LB_BEAM_BAD_FORMAT);
    if(p->atom_lookup) {
        if(lb_atoms_transaction_find(p->atom_lookup,name.data,name.size,destination)!=LB_ATOM_OK)
            return lb_pfail(p,LB_BEAM_BAD_FORMAT);
        return 1;
    }
    if(!p->name_blocks || p->name_blocks->used==p->name_blocks->capacity) {
        size_t capacity=p->name_block_capacity? p->name_block_capacity*2 : 32,patch_bytes,bytes;
        if(capacity>1024) capacity=1024;
        if(capacity<p->name_block_capacity ||
           !lb_size_mul(capacity,sizeof(LbNamePatch),&patch_bytes) ||
           !lb_size_add(offsetof(LbNameBlock,patches),patch_bytes,&bytes)) return lb_pfail(p,LB_BEAM_LIMIT);
        LbNameBlock *block=lb_palloc(p,1,bytes); if(!block) return 0;
        block->next=p->name_blocks; block->capacity=capacity;
        p->name_blocks=block; p->name_block_capacity=capacity;
    }
    patch=&p->name_blocks->patches[p->name_blocks->used++];
    patch->name=name; patch->destination=destination; *destination=THE_NON_VALUE;
    patch->next=p->names; p->names=patch; ++p->name_count; return 1;
}
void lb_program_discard_names(LbBeamProgram *p)
{
    if(!p) return;
    while(p->name_blocks) {
        LbNameBlock *block=p->name_blocks; p->name_blocks=block->next; lb_pfree(p,block);
    }
    p->names=NULL; p->name_count=0;
}
void lb_beam_program_destroy(LbBeamProgram *p)
{
    LbAllocDomain *domain;
    if(!p) return;
    domain=p->domain;
    lb_offheap_clear(&p->off_heap);
    while(p->blocks) {
        LbPrepBlock *block=p->blocks; p->blocks=block->link.next;
        if(lb_alloc_domain_release_owned(domain,block)!=LB_ALLOC_OK) abort();
        assert(p->block_count); --p->block_count;
    }
    lb_beam_image_destroy(p->image);
    lb_atoms_abort(p->transaction);
    if(p->retained && lb_atoms_release(p->atoms)!=LB_ATOM_OK) abort();
    if(lb_alloc_domain_release(domain,p)!=LB_ALLOC_OK) abort();
}
static LbBeamStatus begin(LbAllocDomain *domain,LbAtomTable *atoms,const void *bytes,size_t size,
                          LbBeamProgram **out,LbBeamError *error,int executable,int bind_atoms)
{
    LbBeamProgram *p;
    const LbBeamImageInfo *info;
    LbBeamStatus status;
    LbAtomStatus ast;
    LbBeamBytes *names;
    Eterm *terms;
    LbNamePatch *patch;
    LbBeamOp *op;
    size_t i;
    void *memory;
    if(error) *error=(LbBeamError){LB_BEAM_INVALID_ARGUMENT,0,"arguments"};
    if(!out) return LB_BEAM_INVALID_ARGUMENT;
    *out=NULL;
    if(!domain || !atoms || !bytes || !size) return LB_BEAM_INVALID_ARGUMENT;
    if(lb_alloc_domain_allocate(domain,sizeof(*p),&memory)!=LB_ALLOC_OK) {
        if(error) *error=(LbBeamError){LB_BEAM_NO_MEMORY,0,"control"};
        return LB_BEAM_NO_MEMORY;
    }
    p=memory; memset(p,0,sizeof(*p)); p->domain=domain; p->atoms=atoms;
    p->error.stage="image";
    status=lb_beam_image_create(domain,bytes,size,&p->image);
    if(status!=LB_BEAM_OK) { lb_pfail(p,status); goto fail; }
    info=lb_beam_image_info(p->image);
    p->error.stage="atom plan";
    p->file_atoms=lb_palloc(p,(size_t)info->atom_count+1,sizeof(Eterm));
    if(!p->file_atoms) goto fail;
    p->file_atoms[0]=NIL;
    for(i=1;i<=info->atom_count;++i) {
        LbBeamBytes name;
        if(lb_beam_image_atom(p->image,(uint32_t)i,&name)!=LB_BEAM_OK) abort();
        if(!lb_pname(p,name,&p->file_atoms[i])) goto fail;
    }
    if(bind_atoms) {
        if(!lb_pliterals(p) || !lb_pmetadata(p) || !lb_pdecode(p)) goto fail;
        if(executable && !lb_pmodule_info(p)) goto fail;
    } else if(!lb_pliterals(p) || !lb_pmodule_info(p)) goto fail;
    p->error.stage="atom admission";
    ast=lb_atoms_retain(atoms);
    if(ast!=LB_ATOM_OK) { lb_pfail(p,LB_BEAM_LIMIT); goto fail; }
    p->retained=1;
    if(bind_atoms) {
        names=lb_palloc(p,p->name_count,sizeof(*names));
        terms=lb_palloc(p,p->name_count,sizeof(*terms));
        if(!names || !terms) goto fail;
        i=0;
        for(patch=p->names;patch;patch=patch->next) names[i++]=patch->name;
        ast=lb_atoms_prepare_names(atoms,names,p->name_count,terms,&p->transaction);
        if(ast!=LB_ATOM_OK) {
            lb_pfail(p,ast==LB_ATOM_NO_MEMORY ? LB_BEAM_NO_MEMORY : ast==LB_ATOM_LIMIT ? LB_BEAM_LIMIT : LB_BEAM_BAD_FORMAT);
            goto fail;
        }
        /* Provisional atom identities are usable only inside this unpublished
         * transaction. Code transformation/linking may still fail and abort it. */
        i=0;
        for(patch=p->names;patch;patch=patch->next) *patch->destination=terms[i++];
        lb_program_discard_names(p);
        for(op=p->ops;op;op=op->next) for(i=0;i<op->arity;++i)
            if(op->a[i].type==TAG_a) op->a[i].val=(Sint)p->file_atoms[op->a[i].val];
        for(i=0;i<p->lambda_count;++i) p->lambdas[i].function=p->file_atoms[p->lambdas[i].function];
        if(!lb_pvalidate_maps(p,NULL)) goto fail;
    }
    *out=p;
    if(error) *error=(LbBeamError){LB_BEAM_OK,0,bind_atoms?"decoded; not executable":"decoded; atoms staged"};
    return LB_BEAM_OK;
fail:
    if(p->error.status==LB_BEAM_OK) abort();
    status=p->error.status; if(error) *error=p->error;
    lb_beam_program_destroy(p); return status;
}
LbBeamStatus lb_program_begin(LbAllocDomain *domain,LbAtomTable *atoms,const void *bytes,size_t size,
                              LbBeamProgram **out,LbBeamError *error)
{
    return begin(domain,atoms,bytes,size,out,error,1,1);
}
LbBeamStatus lb_program_begin_staged(LbAllocDomain *domain,LbAtomTable *atoms,const void *bytes,size_t size,
                                     LbBeamProgram **out,LbBeamError *error)
{
    return begin(domain,atoms,bytes,size,out,error,1,0);
}
LbBeamStatus lb_program_prepare_staged(LbBeamProgram *p)
{
    if(!p || !p->atom_lookup) return LB_BEAM_INVALID_ARGUMENT;
    p->error.stage="metadata";
    if(!lb_pmetadata(p)) return p->error.status;
    return LB_BEAM_OK;
}
LbBeamStatus lb_program_decode_staged(LbBeamProgram *p)
{
    LbBeamOp *op; size_t i;
    if(!p || !p->atom_lookup) return LB_BEAM_INVALID_ARGUMENT;
    p->error.stage="code decode";
    if(!lb_pdecode(p)) return p->error.status;
    for(op=p->ops;op;op=op->next) for(i=0;i<op->arity;++i)
        if(op->a[i].type==TAG_a) op->a[i].val=(Sint)p->file_atoms[op->a[i].val];
    for(i=0;i<p->lambda_count;++i) p->lambdas[i].function=p->file_atoms[p->lambdas[i].function];
    if(!lb_pvalidate_maps(p,NULL)) return p->error.status;
    p->atom_lookup=NULL;
    return LB_BEAM_OK;
}
typedef struct {
    LbBeamProgram *const *programs;
    size_t count,module;
    LbNamePatch *patch;
} AtomPatchStream;
static int atom_patch_next(void *context,size_t index,LbBeamBytes *name,Eterm **destination)
{
    AtomPatchStream *stream=context;
    (void)index;
    while(stream->module<stream->count && !stream->patch) {
        ++stream->module;
        if(stream->module<stream->count) stream->patch=stream->programs[stream->module]->names;
    }
    if(stream->module>=stream->count) return 0;
    *name=stream->patch->name; *destination=stream->patch->destination;
    stream->patch=stream->patch->next;
    return 1;
}
LbBeamStatus lb_program_bind_batch(LbBeamProgram *const *programs,size_t count)
{
    LbBeamProgram *first;
    LbAtomTransaction *transaction=NULL; LbAtomStatus status;
    AtomPatchStream stream; size_t total=0,i;
    if(!programs || !count || !programs[0]) return LB_BEAM_INVALID_ARGUMENT;
    first=programs[0];
    for(i=0;i<count;++i) {
        LbBeamProgram *p=programs[i];
        if(!p || p->atoms!=first->atoms || p->transaction || !p->retained) return LB_BEAM_INVALID_ARGUMENT;
        if(!lb_size_add(total,p->name_count,&total)) {
            lb_pfail(first,LB_BEAM_LIMIT);
            return LB_BEAM_LIMIT;
        }
    }
    stream=(AtomPatchStream){programs,count,0,programs[0]->names};
    status=lb_atoms_prepare_names_stream(first->atoms,total,atom_patch_next,&stream,&transaction);
    if(status!=LB_ATOM_OK) {
        first->error.stage="atom batch admission";
        lb_pfail(first,status==LB_ATOM_NO_MEMORY?LB_BEAM_NO_MEMORY:
            status==LB_ATOM_LIMIT?LB_BEAM_LIMIT:LB_BEAM_BAD_FORMAT);
        return first->error.status;
    }
    first->transaction=transaction;
    for(i=0;i<count;++i) {
        programs[i]->atom_lookup=transaction;
        lb_program_discard_names(programs[i]);
    }
    return LB_BEAM_OK;
}
void lb_program_commit(LbBeamProgram *p)
{
    if(p->transaction) { lb_atoms_commit(p->transaction); p->transaction=NULL; }
}
LbBeamStatus lb_beam_program_prepare(LbAllocDomain *domain,LbAtomTable *atoms,const void *bytes,size_t size,
                                     LbBeamProgram **out,LbBeamError *error)
{
    LbBeamStatus status=begin(domain,atoms,bytes,size,out,error,0,1);
    if(status==LB_BEAM_OK) lb_program_commit(*out);
    return status;
}
const LbBeamOp *lb_beam_program_ops(const LbBeamProgram *p) { return p ? p->ops : NULL; }
const LbBeamImage *lb_beam_program_image(const LbBeamProgram *p) { return p ? p->image : NULL; }
size_t lb_beam_program_literal_count(const LbBeamProgram *p) { return p ? p->literal_count : 0; }
size_t lb_beam_program_dynamic_literal_count(const LbBeamProgram *p) { return p ? p->dynamic_count : 0; }
size_t lb_beam_program_lambda_count(const LbBeamProgram *p) { return p ? p->lambda_count : 0; }
int lb_beam_program_type_fallback(const LbBeamProgram *p) { return p ? p->type_fallback : 0; }
LbBeamStatus lb_beam_program_literal(const LbBeamProgram *p,const LbAtomTable *owner,Sint index,Eterm *out)
{
    LbDynamicLiteral *literal;
    if(!out) return LB_BEAM_INVALID_ARGUMENT;
    *out=THE_NON_VALUE;
    if(!p || !owner || p->atoms!=owner) return LB_BEAM_INVALID_ARGUMENT;
    if(index>=0) {
        if((Uint)index>=p->literal_count) return LB_BEAM_NOT_FOUND;
        *out=p->literals[index]; return LB_BEAM_OK;
    }
    for(literal=p->dynamic;literal;literal=literal->next) if(literal->index==index) {
        *out=literal->value; return LB_BEAM_OK;
    }
    return LB_BEAM_NOT_FOUND;
}
LbBeamStatus lb_beam_program_lambda(const LbBeamProgram *p,const LbAtomTable *owner,size_t i,LbBeamLambda *out)
{
    if(!out) return LB_BEAM_INVALID_ARGUMENT;
    *out=(LbBeamLambda){0};
    if(!p || !owner || p->atoms!=owner) return LB_BEAM_INVALID_ARGUMENT;
    if(i>=p->lambda_count) return LB_BEAM_NOT_FOUND;
    *out=p->lambdas[i]; return LB_BEAM_OK;
}
LbBeamStatus lb_beam_program_type(const LbBeamProgram *p,size_t i,LbBeamType *out)
{
    if(!out) return LB_BEAM_INVALID_ARGUMENT;
    *out=(LbBeamType){0};
    if(!p) return LB_BEAM_INVALID_ARGUMENT;
    if(i>=p->type_count) return LB_BEAM_NOT_FOUND;
    *out=p->types[i]; return LB_BEAM_OK;
}
