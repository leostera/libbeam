/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#include "beam_program_internal.h"
#include "utf8.h"
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
    if(lb_alloc_domain_allocate(p->domain,bytes,&memory)!=LB_ALLOC_OK) {
        lb_pfail(p,LB_BEAM_NO_MEMORY); return NULL;
    }
    block=memory; block->link.next=p->blocks; p->blocks=block; p->bytes+=bytes;
    memset(block+1,0,bytes-sizeof(*block)); return block+1;
}
int lb_pname(LbBeamProgram *p,LbBeamBytes name,Eterm *destination)
{
    LbNamePatch *patch;
    if(!lb_utf8_atom_validate(name.data,name.size)) return lb_pfail(p,LB_BEAM_BAD_FORMAT);
    patch=lb_palloc(p,1,sizeof(*patch)); if(!patch) return 0;
    patch->name=name; patch->destination=destination; *destination=THE_NON_VALUE;
    patch->next=p->names; p->names=patch; ++p->name_count; return 1;
}
void lb_beam_program_destroy(LbBeamProgram *p)
{
    LbAllocDomain *domain;
    if(!p) return;
    domain=p->domain;
    while(p->blocks) {
        LbPrepBlock *block=p->blocks; p->blocks=block->link.next;
        if(lb_alloc_domain_release(domain,block)!=LB_ALLOC_OK) abort();
    }
    lb_beam_image_destroy(p->image);
    if(p->retained && lb_atoms_release(p->atoms)!=LB_ATOM_OK) abort();
    if(lb_alloc_domain_release(domain,p)!=LB_ALLOC_OK) abort();
}
LbBeamStatus lb_beam_program_prepare(LbAllocDomain *domain,LbAtomTable *atoms,const void *bytes,size_t size,
                                     LbBeamProgram **out,LbBeamError *error)
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
    if(!lb_pliterals(p) || !lb_pmetadata(p) || !lb_pdecode(p)) goto fail;
    p->error.stage="atom admission";
    names=lb_palloc(p,p->name_count,sizeof(*names));
    terms=lb_palloc(p,p->name_count,sizeof(*terms));
    if(!names || !terms) goto fail;
    i=p->name_count;
    for(patch=p->names;patch;patch=patch->next) names[--i]=patch->name;
    ast=lb_atoms_retain(atoms);
    if(ast!=LB_ATOM_OK) { lb_pfail(p,LB_BEAM_LIMIT); goto fail; }
    p->retained=1;
    ast=lb_atoms_intern_names(atoms,names,p->name_count,terms);
    if(ast!=LB_ATOM_OK) {
        lb_pfail(p,ast==LB_ATOM_NO_MEMORY ? LB_BEAM_NO_MEMORY : ast==LB_ATOM_LIMIT ? LB_BEAM_LIMIT : LB_BEAM_BAD_FORMAT);
        goto fail;
    }
    /* No fallible work remains after namespace commit. Deferred writes target
     * stable private slots, never host/process roots or published instructions. */
    i=p->name_count;
    for(patch=p->names;patch;patch=patch->next) *patch->destination=terms[--i];
    for(op=p->ops;op;op=op->next) for(i=0;i<op->arity;++i)
        if(op->a[i].type==TAG_a) op->a[i].val=(Sint)p->file_atoms[op->a[i].val];
    for(i=0;i<p->lambda_count;++i) p->lambdas[i].function=p->file_atoms[p->lambdas[i].function];
    *out=p;
    if(error) *error=(LbBeamError){LB_BEAM_OK,0,"decoded; not executable"};
    return LB_BEAM_OK;
fail:
    if(p->error.status==LB_BEAM_OK) abort();
    status=p->error.status; if(error) *error=p->error;
    lb_beam_program_destroy(p); return status;
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
