/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1996-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * C-owned, serialized linking/publication. Transforms/emission retain the OTP
 * instruction ABI; this replaces the singleton loader/code-index architecture.
 */
#include "code_internal.h"
#include "md5.h"
static LbCodeStatus from_beam(LbBeamStatus s)
{
    switch(s) {
    case LB_BEAM_OK:return LB_CODE_OK;
    case LB_BEAM_NO_MEMORY:return LB_CODE_NO_MEMORY;
    case LB_BEAM_LIMIT:return LB_CODE_LIMIT;
    case LB_BEAM_UNSUPPORTED:return LB_CODE_UNSUPPORTED;
    default:return LB_CODE_FORMAT;
    }
}
Export *lb_export_find(LbCodeSpace *space,Eterm module,Eterm function,unsigned arity)
{
    Export *e;
    for(e=space->exports;e;e=e->next)
        if(e->info.mfa.module==module && e->info.mfa.function==function && e->info.mfa.arity==arity) return e;
    return NULL;
}
LbCodeStatus lb_code_space_create(LbEngine *engine,LbCodeSpace **out)
{
    LbCodeSpace *space;
    LbAllocDomain *domain;
    void *memory;
    size_t i;
    LbAtomStatus status;
    if(!out) return LB_CODE_INVALID;
    *out=NULL; if(!engine) return LB_CODE_INVALID;
    if(!lb_engine_is_open(engine)) return LB_CODE_CLOSED;
    if(engine->spaces==SIZE_MAX) return LB_CODE_LIMIT;
    domain=engine->domain;
    if(lb_alloc_domain_allocate(domain,sizeof(*space),&memory)!=LB_ALLOC_OK) return LB_CODE_NO_MEMORY;
    space=memory; memset(space,0,sizeof(*space)); space->engine=engine; space->domain=domain;
    status=lb_atoms_create(domain,1048576,&space->atoms);
    if(status!=LB_ATOM_OK) { lb_release(domain,space); return status==LB_ATOM_NO_MEMORY?LB_CODE_NO_MEMORY:LB_CODE_LIMIT; }
    for(i=0;i<LB_NATIVE_COUNT;++i) {
        unsigned id=engine->native_ids[i];
        const BifEntry *bif=&engine->bifs[id];
        Export *e=&space->natives[i];
        /* Mutable export/trampoline state is private; implementation metadata
         * and allocation infrastructure are retained from the actual Engine. */
        e->bif_number=(int)id; e->info.op=op_i_func_info_IaaI;
        e->info.mfa=(LbMFA){bif->module,bif->name,(Uint)bif->arity};
        e->trampoline.op=op_call_bif_W;
        _Static_assert(sizeof(LbBifFn)==sizeof(BeamInstr),"Native pointer word ABI");
        memcpy(&e->trampoline.address,&bif->f,sizeof(LbBifFn));
        e->dispatch.addresses[0]=&e->trampoline.op;
        e->next=space->exports; space->exports=e;
    }
    lb_engine_space_published(engine); *out=space; return LB_CODE_OK;
}
LbCodeStatus lb_code_space_destroy(LbCodeSpace *space)
{
    LbAllocDomain *domain;
    LbEngine *engine;
    if(!space) return LB_CODE_INVALID;
    if(space->modules || space->loading) return LB_CODE_BUSY;
    if(lb_atoms_destroy(space->atoms)!=LB_ATOM_OK) return LB_CODE_BUSY;
    domain=space->domain; engine=space->engine;
    lb_release(domain,space); lb_engine_space_released(engine); return LB_CODE_OK;
}
LbAtomTable *lb_code_space_atoms(LbCodeSpace *space) { return space?space->atoms:NULL; }
static Export *local_export(LbCodeModule *module,Eterm function,unsigned arity)
{
    size_t i;
    for(i=0;i<module->export_count;++i) {
        Export *e=&module->exports[i];
        if(e->info.mfa.function==function && e->info.mfa.arity==arity) return e;
    }
    return NULL;
}
static LbCodeStatus setup(LoaderState *st)
{
    LbBeamProgram *p=st->program;
    LbCodeModule *module=st->module_code;
    const LbBeamImageInfo *info=lb_beam_image_info(p->image);
    size_t i;
    LbBeamBytes chunk;
    void *memory;
    /* Records and executable debug instrumentation are not opaque executable
     * capabilities. Ordinary Dbgi/Line/Meta remain passive retained metadata. */
    if((lb_beam_image_chunk(p->image,LB_BEAM_ID('R','e','c','s'),&chunk)==LB_BEAM_OK && chunk.size) ||
       (lb_beam_image_chunk(p->image,LB_BEAM_ID('D','b','g','B'),&chunk)==LB_BEAM_OK && chunk.size)) return LB_CODE_UNSUPPORTED;
    st->module=module->name=p->file_atoms[1];
    if(module->name==am_erlang) return LB_CODE_EXISTS; /* protected native catalog */
    module->attributes=p->attributes; module->compile=p->compile;
    st->label_count=info->label_count;
    st->labels=lb_palloc(p,st->label_count,sizeof(*st->labels));
    if(!st->labels) return from_beam(p->error.status);
    module->export_count=info->export_count;
    if(info->export_count) {
        module->exports=lb_palloc(p,info->export_count,sizeof(*module->exports));
        if(!module->exports) return from_beam(p->error.status);
    }
    for(i=0;i<info->export_count;++i) {
        LbBeamExport e; Export *entry=&module->exports[i]; size_t j;
        if(lb_beam_image_export(p->image,(uint32_t)i,&e)!=LB_BEAM_OK) abort();
        entry->info.op=op_i_func_info_IaaI;
        entry->info.mfa=(LbMFA){module->name,p->file_atoms[e.atom],e.arity};
        entry->owner=module; entry->bif_number=-1;
        for(j=0;j<i;++j) if(module->exports[j].info.mfa.function==entry->info.mfa.function &&
                            module->exports[j].info.mfa.arity==e.arity) return LB_CODE_FORMAT;
    }
    st->beam.imports.count=module->import_count=info->import_count;
    if(info->import_count) {
        st->beam.imports.entries=lb_palloc(p,info->import_count,sizeof(*st->beam.imports.entries));
        st->bif_imports=lb_palloc(p,info->import_count,sizeof(*st->bif_imports));
        module->imports=lb_palloc(p,info->import_count,sizeof(*module->imports));
        if(!st->beam.imports.entries || !st->bif_imports || !module->imports) return from_beam(p->error.status);
        if(lb_alloc_domain_allocate(st->space->domain,info->import_count*sizeof(*module->dependencies),&memory)!=LB_ALLOC_OK) return LB_CODE_NO_MEMORY;
        module->dependencies=memory;
    }
    for(i=0;i<info->import_count;++i) {
        LbBeamImport imp; BeamFile_ImportEntry *entry=&st->beam.imports.entries[i]; Export *e;
        if(lb_beam_image_import(p->image,(uint32_t)i,&imp)!=LB_BEAM_OK) abort();
        *entry=(BeamFile_ImportEntry){p->file_atoms[imp.module],p->file_atoms[imp.function],imp.arity};
        e=entry->module==module->name?local_export(module,entry->function,(unsigned)entry->arity):
            lb_export_find(st->space,entry->module,entry->function,(unsigned)entry->arity);
        if(!e) return LB_CODE_UNRESOLVED;
        if(e->owner && e->owner!=module && e->owner->importers>SIZE_MAX-module->import_count) return LB_CODE_LIMIT;
        module->imports[i]=e; module->dependencies[i]=e->owner==module?NULL:e->owner;
        if(e->bif_number>=0) st->bif_imports[i]=&st->space->engine->bifs[e->bif_number];
    }
    st->ci=(size_t)info->function_count+1;
    return LB_CODE_OK;
}
static void checksum(LbCodeModule *module)
{
    /* beam_file.c checksum order, including the normalized legacy lambda uniq. */
    const uint32_t ids[]={LB_BEAM_ID('A','t','U','8'),LB_BEAM_ID('C','o','d','e'),LB_BEAM_ID('S','t','r','T'),
        LB_BEAM_ID('I','m','p','T'),LB_BEAM_ID('E','x','p','T'),LB_BEAM_ID('F','u','n','T'),
        LB_BEAM_ID('L','i','t','T'),LB_BEAM_ID('M','e','t','a'),LB_BEAM_ID('R','e','c','s'),LB_BEAM_ID('D','b','g','B')};
    erts_md5_state md5; size_t i;
    erts_md5_init(&md5);
    for(i=0;i<sizeof(ids)/sizeof(ids[0]);++i) {
        LbBeamBytes data;
        if(lb_beam_image_chunk(module->program->image,ids[i],&data)!=LB_BEAM_OK || !data.size) continue;
        if(i==5) {
            static const unsigned char zero[4]={0}; size_t pos;
            erts_md5_update(&md5,data.data,4);
            for(pos=4;pos<data.size;pos+=24) {
                erts_md5_update(&md5,data.data+pos,20); erts_md5_update(&md5,zero,4);
            }
        } else erts_md5_update(&md5,data.data,data.size);
    }
    erts_md5_finish(module->md5,&md5);
}
LbCodeStatus lb_compile_code(LbCodeModule *module)
{
    LoaderState st={0};
    LbCodeStatus status;
    size_t rewrites=0,i;
    st.program=module->program; st.module_code=module; st.space=module->space;
    st.program->error.stage="link/admission";
    status=setup(&st); if(status!=LB_CODE_OK) return status;
    status=lb_verify_program(&st); if(status!=LB_CODE_OK) return status;
    st.genop=st.program->ops;
    while(st.genop) {
        BeamOp *op=st.genop;
        LbBeamSelection selection;
        if(op->op>=NUM_GENERIC_OPS || op->arity<(unsigned)gen_opc[op->op].arity) return LB_CODE_FORMAT;
        st.program->error.offset=op->offset;
        if(gen_opc[op->op].transform) {
            int result;
            st.program->error.stage=gen_opc[op->op].name;
            if(++rewrites>1048576) return LB_CODE_LIMIT;
            result=lb_transform(&st);
            if(st.status!=LB_CODE_OK) return st.status;
            if(result==TE_OK) continue;
            if(result==TE_NO_MEMORY) return from_beam(st.program->error.status);
            if(result==TE_UNSUPPORTED) return LB_CODE_UNSUPPORTED;
            if(result!=TE_FAIL) return LB_CODE_FORMAT;
        }
        st.program->error.stage="specific selection";
        if(lb_beam_select_specific(op,&selection)!=LB_BEAM_OK) return LB_CODE_UNSUPPORTED;
        st.specific_op=(int)selection.opcode;
        st.program->error.stage=opc[selection.opcode].name;
        if(selection.opcode!=op_label_L && selection.opcode!=op_line_I &&
           selection.opcode!=op_i_func_info_IaaI && selection.opcode!=op_int_code_end &&
           !lb_instruction_supported(selection.opcode)) return LB_CODE_UNSUPPORTED;
        for(i=0;i<op->arity && i<sizeof(selection.r_mask)*CHAR_BIT;++i)
            if(selection.r_mask&(1u<<i)) op->a[i].type=TAG_r;
        status=lb_emit(&st,op); if(status!=LB_CODE_OK) return status;
        st.genop=op->next; lb_load_free_op(&st,op);
    }
    st.program->error.stage="relocation";
    status=lb_finish_emit(&st); if(status!=LB_CODE_OK) return status;
    for(i=0;i<module->export_count;++i) {
        LbBeamExport e;
        if(lb_beam_image_export(st.program->image,(uint32_t)i,&e)!=LB_BEAM_OK) abort();
        if(!st.labels[e.label].value || st.labels[e.label].value>=st.ci) return LB_CODE_FORMAT;
        module->exports[i].dispatch.addresses[0]=st.codev+st.labels[e.label].value;
    }
    checksum(module);
    return LB_CODE_OK;
}
LbCodeStatus lb_code_load(LbCodeSpace *space,const void *bytes,size_t size,LbCodeModule **out,LbBeamError *error)
{
    LbCodeModule *module,*peer;
    LbBeamProgram *p=NULL;
    LbBeamStatus status;
    LbCodeStatus result;
    void *memory;
    size_t i;
    if(error) *error=(LbBeamError){LB_BEAM_INVALID_ARGUMENT,0,"arguments"};
    if(!out) return LB_CODE_INVALID;
    *out=NULL;
    if(!space || !bytes || !size) return LB_CODE_INVALID;
    if(space->loading) {
        if(error) *error=(LbBeamError){LB_BEAM_UNSUPPORTED,0,"code admission busy"};
        return LB_CODE_BUSY;
    }
    space->loading=1;
    status=lb_program_begin(space->domain,space->atoms,bytes,size,&p,error);
    if(status!=LB_BEAM_OK) { space->loading=0; return from_beam(status); }
    for(peer=space->modules;peer;peer=peer->next) if(peer->name==p->file_atoms[1]) {
        if(error) *error=(LbBeamError){LB_BEAM_UNSUPPORTED,0,"module already published; hot reload excluded"};
        lb_beam_program_destroy(p); space->loading=0; return LB_CODE_EXISTS;
    }
    if(lb_alloc_domain_allocate(space->domain,sizeof(*module),&memory)!=LB_ALLOC_OK) {
        lb_beam_program_destroy(p); space->loading=0;
        if(error) *error=(LbBeamError){LB_BEAM_NO_MEMORY,0,"module control"}; return LB_CODE_NO_MEMORY;
    }
    module=memory; memset(module,0,sizeof(*module)); module->space=space; module->program=p;
    result=lb_compile_code(module);
    if(result!=LB_CODE_OK) {
        if(error) *error=(LbBeamError){result==LB_CODE_NO_MEMORY?LB_BEAM_NO_MEMORY:result==LB_CODE_LIMIT?LB_BEAM_LIMIT:
            result==LB_CODE_UNSUPPORTED?LB_BEAM_UNSUPPORTED:result==LB_CODE_UNRESOLVED?LB_BEAM_NOT_FOUND:LB_BEAM_BAD_FORMAT,
            p->error.offset,p->error.stage};
        lb_beam_program_destroy(p); lb_release(space->domain,module->dependencies); lb_release(space->domain,module); space->loading=0; return result;
    }
    /* Publication: all allocations, resolution, dispatch admission and fixups
     * succeeded. No fallible work or callback reentry after atom commit. */
    lb_program_commit(p);
    for(i=0;i<module->import_count;++i) if(module->dependencies[i]) ++module->dependencies[i]->importers;
    for(i=0;i<module->export_count;++i) { module->exports[i].next=space->exports; space->exports=&module->exports[i]; }
    module->next=space->modules; space->modules=module; module->published=1;
    space->loading=0; *out=module;
    if(error) *error=(LbBeamError){LB_BEAM_OK,0,"published native BEAM words"};
    return LB_CODE_OK;
}
LbCodeStatus lb_code_unload(LbCodeModule *module)
{
    LbCodeSpace *space; LbCodeModule **link; Export **entry; size_t i;
    if(!module || !module->published) return LB_CODE_INVALID;
    space=module->space;
    if(space->loading || module->users || module->importers) return LB_CODE_BUSY;
    for(entry=&space->exports;*entry;) {
        if((*entry)->owner==module) *entry=(*entry)->next; else entry=&(*entry)->next;
    }
    for(link=&space->modules;*link!=module;link=&(*link)->next) if(!*link) abort();
    *link=module->next; module->published=0;
    /* Physical code/literal/metadata retirement precedes dependency release. */
    lb_beam_program_destroy(module->program);
    for(i=0;i<module->import_count;++i) if(module->dependencies[i]) {
        if(!module->dependencies[i]->importers) abort(); --module->dependencies[i]->importers;
    }
    lb_release(space->domain,module->dependencies); lb_release(space->domain,module); return LB_CODE_OK;
}
LbCodeStatus lb_code_entry_acquire(LbCodeSpace *space,Eterm module,Eterm function,unsigned arity,LbCodeEntry **out)
{
    Export *e; LbCodeEntry *entry; void *memory;
    if(!out) return LB_CODE_INVALID; *out=NULL;
    if(!space || !is_atom(module) || !is_atom(function) || arity>255) return LB_CODE_INVALID;
    if(space->loading) return LB_CODE_BUSY;
    e=lb_export_find(space,module,function,arity);
    if(!e || !e->owner) return LB_CODE_NOT_FOUND;
    if(e->owner->users==SIZE_MAX) return LB_CODE_LIMIT;
    if(lb_alloc_domain_allocate(space->domain,sizeof(*entry),&memory)!=LB_ALLOC_OK) return LB_CODE_NO_MEMORY;
    entry=memory; entry->module=e->owner; entry->entry=e; ++e->owner->users; *out=entry; return LB_CODE_OK;
}
void lb_code_entry_release(LbCodeEntry *entry)
{
    LbAllocDomain *d; LbCodeModule *module;
    if(!entry) return;
    module=entry->module; d=module->space->domain;
    lb_release(d,entry);
    if(!module->users) abort(); --module->users;
}
const Uint *lb_code_entry_address(const LbCodeEntry *entry) { return entry?entry->entry->dispatch.addresses[0]:NULL; }
const Uint *lb_code_module_words(const LbCodeModule *module,size_t *count)
{
    if(count) *count=module?module->word_count:0;
    return module?module->words:NULL;
}
Eterm lb_code_module_name(const LbCodeModule *module) { return module?module->name:THE_NON_VALUE; }
size_t lb_code_module_function_count(const LbCodeModule *module)
{
    return module?lb_beam_image_info(module->program->image)->function_count:0;
}
