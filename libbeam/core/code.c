/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1996-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * C-owned, serialized linking/publication. Transforms/emission retain the OTP
 * instruction ABI; this replaces the singleton loader/code-index architecture.
 */
#include "code_internal.h"
#include "md5.h"
#include "pid_table.h"
#include "fun.h"
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
LbCodeStatus lb_code_space_create_in_domain(LbEngine *engine,LbAllocDomain *domain,LbCodeSpace **out)
{
    LbCodeSpace *space;
    void *memory;
    size_t i;
    LbAtomStatus status;
    if(!out) return LB_CODE_INVALID;
    *out=NULL; if(!engine || !domain) return LB_CODE_INVALID;
    if(!lb_engine_is_open(engine)) return LB_CODE_CLOSED;
    if(engine->spaces==SIZE_MAX) return LB_CODE_LIMIT;
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
LbCodeStatus lb_code_space_create(LbEngine *engine,LbCodeSpace **out)
{ return lb_code_space_create_in_domain(engine,engine?engine->domain:NULL,out); }
LbCodeStatus lb_code_space_destroy(LbCodeSpace *space)
{
    LbAllocDomain *domain;
    LbEngine *engine;
    if(!space) return LB_CODE_INVALID;
    if(space->modules || space->loading || space->messages || space->registry || lb_pid_count(space->pids)) return LB_CODE_BUSY;
    if(lb_atoms_destroy(space->atoms)!=LB_ATOM_OK) return LB_CODE_BUSY;
    if(space->pids && lb_pid_table_destroy(space->pids)!=LB_PID_OK) abort();
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
    if(module->name==am_erlang && !st->space->preloading) return LB_CODE_EXISTS; /* protected native catalog/wrappers */
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
        LbBeamImport imp;
        if(lb_beam_image_import(p->image,(uint32_t)i,&imp)!=LB_BEAM_OK) abort();
        st->beam.imports.entries[i]=(BeamFile_ImportEntry){p->file_atoms[imp.module],p->file_atoms[imp.function],imp.arity};
    }
    module->fun_count=p->lambda_count;
    if(p->lambda_count) {
        module->funs=lb_palloc(p,p->lambda_count,sizeof(*module->funs));
        st->lambda_literals=lb_palloc(p,p->lambda_count,sizeof(*st->lambda_literals));
        unsigned char *indices=lb_palloc(p,p->lambda_count,1);
        if(!module->funs || !st->lambda_literals || !indices) return from_beam(p->error.status);
        for(i=0;i<p->lambda_count;++i) {
            LbBeamLambda *lambda=&p->lambdas[i]; LbFunEntry *entry=&module->funs[i];
            if(indices[lambda->index]) return LB_CODE_FORMAT;
            indices[lambda->index]=1;
            entry->module=module->name; entry->arity=(int)(lambda->arity-lambda->num_free);
            entry->index=(int)lambda->index; entry->old_index=(int)i;
            _Static_assert(sizeof(entry->old_uniq)==sizeof(lambda->old_uniq),"native fun uniq width");
            memcpy(&entry->old_uniq,&lambda->old_uniq,sizeof(entry->old_uniq));
            entry->owner=module; st->lambda_literals[i]=INTPTR_MAX;
        }
    }
    st->ci=(size_t)info->function_count+1;
    return LB_CODE_OK;
}
static Export *batch_export_find(LbCodeModule *const *modules,size_t count,Eterm module,Eterm function,unsigned arity)
{
    size_t i;
    for(i=0;i<count;++i) if(modules[i]->name==module) {
        Export *entry=local_export(modules[i],function,arity);
        if(entry) return entry;
    }
    return NULL;
}
LbCodeStatus lb_code_resolve_imports(LoaderState *st,LbCodeModule *const *batch,size_t count)
{
    LbCodeModule *module=st->module_code;
    size_t i;
    for(i=0;i<module->import_count;++i) {
        BeamFile_ImportEntry *entry=&st->beam.imports.entries[i];
        Export *export=entry->module==module->name?local_export(module,entry->function,(unsigned)entry->arity):
            batch_export_find(batch,count,entry->module,entry->function,(unsigned)entry->arity);
        if(!export) export=lb_export_find(st->space,entry->module,entry->function,(unsigned)entry->arity);
        if(!export && st->space->preloading && entry->module==am_erlang)
            export=lb_export_find(st->space,entry->module,entry->function,(unsigned)entry->arity);
        if(!export) {
            st->program->error.stage="unresolved import"; st->program->error.offset=i;
            return LB_CODE_UNRESOLVED;
        }
        if(export->owner && export->owner!=module && export->owner->importers>SIZE_MAX-module->import_count)
            return LB_CODE_LIMIT;
        module->imports[i]=export;
        module->dependencies[i]=export->owner==module?NULL:export->owner;
        if(export->bif_number>=0) st->bif_imports[i]=&st->space->engine->bifs[export->bif_number];
    }
    return LB_CODE_OK;
}
static LbCodeStatus validate_batch_importers(LbCodeSpace *space,LbCodeModule *const *modules,size_t count)
{
    LbCodeModule *module;
    size_t i,j;
    for(i=0;i<count;++i) for(j=0;j<modules[i]->import_count;++j) {
        LbCodeModule *owner=modules[i]->dependencies[j];
        if(owner) {
            if(owner->pending_importers==SIZE_MAX) return LB_CODE_LIMIT;
            ++owner->pending_importers;
        }
    }
    for(module=space->modules;module;module=module->next)
        if(module->pending_importers>SIZE_MAX-module->importers) return LB_CODE_LIMIT;
    for(i=0;i<count;++i)
        if(modules[i]->pending_importers>SIZE_MAX-modules[i]->importers) return LB_CODE_LIMIT;
    return LB_CODE_OK;
}
static void clear_pending_importers(LbCodeModule *const *modules,size_t count)
{
    size_t i,j;
    for(i=0;i<count;++i) if(modules[i] && modules[i]->dependencies)
        for(j=0;j<modules[i]->import_count;++j)
            if(modules[i]->dependencies[j]) modules[i]->dependencies[j]->pending_importers=0;
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
LbCodeStatus lb_code_prepare_module(LbCodeModule *module,LoaderState *st)
{
    if(!module || !st || !module->program || !module->space) return LB_CODE_INVALID;
    memset(st,0,sizeof(*st));
    st->program=module->program; st->module_code=module; st->space=module->space;
    st->program->error.stage="link/admission";
    return setup(st);
}
LbCodeStatus lb_code_compile_prepared(LoaderState *prepared)
{
    LoaderState *st=prepared;
    LbCodeModule *module=st->module_code;
    LbCodeStatus status;
    size_t rewrites=0,i;
    status=lb_verify_program(st); if(status!=LB_CODE_OK) return status;
    st->genop=st->program->ops;
    while(st->genop) {
        BeamOp *op=st->genop;
        LbBeamSelection selection;
        if(op->op>=NUM_GENERIC_OPS || op->arity<(unsigned)gen_opc[op->op].arity) return LB_CODE_FORMAT;
        st->program->error.offset=op->offset;
        if(gen_opc[op->op].transform) {
            int result;
            st->program->error.stage=gen_opc[op->op].name;
            if(++rewrites>1048576) return LB_CODE_LIMIT;
            result=lb_transform(st);
            if(st->status!=LB_CODE_OK) return st->status;
            if(result==TE_OK) continue;
            if(result==TE_NO_MEMORY) return from_beam(st->program->error.status);
            if(result==TE_UNSUPPORTED) return LB_CODE_UNSUPPORTED;
            if(result!=TE_FAIL) return LB_CODE_FORMAT;
        }
        st->program->error.stage="specific selection";
        if(lb_beam_select_specific(op,&selection)!=LB_BEAM_OK) return LB_CODE_UNSUPPORTED;
        st->specific_op=(int)selection.opcode;
        st->program->error.stage=opc[selection.opcode].name;
        if(selection.opcode!=op_label_L && selection.opcode!=op_line_I &&
           selection.opcode!=op_i_func_info_IaaI && selection.opcode!=op_int_code_end &&
           !lb_instruction_supported(selection.opcode)) return LB_CODE_UNSUPPORTED;
        for(i=0;i<op->arity && i<sizeof(selection.r_mask)*CHAR_BIT;++i)
            if(selection.r_mask&(1u<<i)) op->a[i].type=TAG_r;
        status=lb_emit(st,op); if(status!=LB_CODE_OK) return status;
        st->genop=op->next; lb_load_free_op(st,op);
    }
    st->program->error.stage="relocation";
    status=lb_finish_emit(st); if(status!=LB_CODE_OK) return status;
    for(i=0;i<module->export_count;++i) {
        LbBeamExport e;
        if(lb_beam_image_export(st->program->image,(uint32_t)i,&e)!=LB_BEAM_OK) abort();
        if(!st->labels[e.label].value || st->labels[e.label].value>=st->ci) return LB_CODE_FORMAT;
        module->exports[i].dispatch.addresses[0]=st->codev+st->labels[e.label].value;
    }
    checksum(module);
    for(i=0;i<module->fun_count;++i) {
        LbBeamLambda *lambda=&st->program->lambdas[i]; LbFunEntry *entry=&module->funs[i];
        size_t address=st->labels[lambda->label].value;
        if(!address || address>=st->ci || (!lambda->num_free && st->lambda_literals[i]==INTPTR_MAX)) return LB_CODE_FORMAT;
        entry->dispatch.addresses[0]=st->codev+address;
        memcpy(entry->uniq,module->md5,sizeof(entry->uniq));
    }
    return LB_CODE_OK;
}
LbCodeStatus lb_compile_code(LbCodeModule *module)
{
    LoaderState state;
    LbCodeStatus status=lb_code_prepare_module(module,&state);
    if(status==LB_CODE_OK) status=lb_code_resolve_imports(&state,NULL,0);
    if(status==LB_CODE_OK) status=lb_code_compile_prepared(&state);
    return status;
}
static void report_import(LbBeamProgram *p,LbLoadReport *report)
{
    LbBeamImport import; LbBeamBytes module,function;
    if(lb_beam_image_import(p->image,(uint32_t)p->error.offset,&import)!=LB_BEAM_OK ||
       lb_beam_image_atom(p->image,import.module,&module)!=LB_BEAM_OK ||
       lb_beam_image_atom(p->image,import.function,&function)!=LB_BEAM_OK ||
       module.size>sizeof(report->module) || function.size>sizeof(report->function)) abort();
    report->has_import=1; report->arity=import.arity;
    report->module_size=module.size; report->function_size=function.size;
    memcpy(report->module,module.data,module.size); memcpy(report->function,function.data,function.size);
}
static LbCodeStatus load_code(LbCodeSpace *space,const void *bytes,size_t size,LbCodeModule **out,LbBeamError *error,LbLoadReport *report)
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
        if(result==LB_CODE_UNRESOLVED && report) report_import(p,report);
        lb_beam_program_destroy(p); lb_release(space->domain,module->dependencies); lb_release(space->domain,module); space->loading=0; return result;
    }
    /* Publication: all allocations, resolution, dispatch admission and fixups
     * succeeded. No fallible work or callback reentry after atom commit. */
    lb_program_commit(p);
    for(i=0;i<module->import_count;++i) if(module->dependencies[i]) ++module->dependencies[i]->importers;
    for(i=0;i<module->export_count;++i) { module->exports[i].next=space->exports; space->exports=&module->exports[i]; }
    module->next=space->modules; space->modules=module; module->published=1;
    if(module->literal_fun_uses) space->has_fun_literals=1;
    space->loading=0; *out=module;
    if(error) *error=(LbBeamError){LB_BEAM_OK,0,"published native BEAM words"};
    return LB_CODE_OK;
}
LbCodeStatus lb_code_load_report(LbCodeSpace *space,const void *bytes,size_t size,LbCodeModule **out,LbBeamError *error,LbLoadReport *report)
{
    LbBeamError local_error; LbCodeStatus status;
    if(!error) error=&local_error;
    if(report) memset(report,0,sizeof(*report));
    status=load_code(space,bytes,size,out,error,report);
    if(report) { report->stage=error->stage; report->offset=error->offset; }
    return status;
}
LbCodeStatus lb_code_load(LbCodeSpace *space,const void *bytes,size_t size,LbCodeModule **out,LbBeamError *error)
{
    return lb_code_load_report(space,bytes,size,out,error,NULL);
}
static LbBeamStatus beam_status_for_code(LbCodeStatus status)
{
    switch(status) {
    case LB_CODE_NO_MEMORY:return LB_BEAM_NO_MEMORY;
    case LB_CODE_LIMIT:return LB_BEAM_LIMIT;
    case LB_CODE_UNSUPPORTED:case LB_CODE_EXISTS:case LB_CODE_BUSY:return LB_BEAM_UNSUPPORTED;
    case LB_CODE_UNRESOLVED:return LB_BEAM_NOT_FOUND;
    default:return LB_BEAM_BAD_FORMAT;
    }
}
static void batch_error(LbBeamError *error,LbLoadReport *report,LbCodeStatus status,
                        LbBeamProgram *program,size_t module_index)
{
    if(error) *error=(LbBeamError){beam_status_for_code(status),program?program->error.offset:0,
        program&&program->error.stage?program->error.stage:"batch admission"};
    if(report) {
        report->stage=error?error->stage:(program&&program->error.stage?program->error.stage:"batch admission");
        report->offset=error?error->offset:(program?program->error.offset:0);
        if(module_index!=SIZE_MAX) {
            report->has_module_index=1; report->module_index=module_index;
        }
        if(status==LB_CODE_UNRESOLVED && program) report_import(program,report);
    }
}
static void batch_cleanup(LbCodeSpace *space,LbCodeModule **modules,LbBeamProgram **programs,
                          size_t count)
{
    size_t i;
    clear_pending_importers(modules,count);
    for(i=0;i<count;++i) if(modules[i] && modules[i]->dependencies)
        lb_release(space->domain,modules[i]->dependencies);
    for(i=0;i<count;++i) if(programs[i]) lb_beam_program_destroy(programs[i]);
    for(i=0;i<count;++i) if(modules[i]) lb_release(space->domain,modules[i]);
}
LbCodeStatus lb_code_load_batch(LbCodeSpace *space,const LbCodeInput *inputs,size_t count,
                                LbBeamError *error,LbLoadReport *report)
{
    LbCodeModule **modules=NULL,*peer;
    LbBeamProgram **programs=NULL;
    LoaderState *states=NULL;
    LbCodeStatus result=LB_CODE_OK;
    LbBeamError local_error={LB_BEAM_INVALID_ARGUMENT,0,"arguments"};
    LbBeamProgram *failed_program=NULL;
    size_t module_bytes,program_bytes,state_bytes,total_bytes=0,i,j;
    void *memory;
    if(error) *error=local_error;
    if(report) { memset(report,0,sizeof(*report)); report->stage="arguments"; }
    if(!space || !inputs || !count) return LB_CODE_INVALID;
    if(count>LB_CODE_BATCH_MODULE_LIMIT) {
        if(error) *error=(LbBeamError){LB_BEAM_LIMIT,0,"batch module limit"};
        if(report) report->stage="batch module limit";
        return LB_CODE_LIMIT;
    }
    if(space->loading) {
        if(error) *error=(LbBeamError){LB_BEAM_UNSUPPORTED,0,"code admission busy"};
        if(report) report->stage="code admission busy";
        return LB_CODE_BUSY;
    }
    for(i=0;i<count;++i) {
        if(!inputs[i].data || !inputs[i].size) return LB_CODE_INVALID;
        if(!lb_size_add(total_bytes,inputs[i].size,&total_bytes) || total_bytes>LB_CODE_BATCH_BYTES_LIMIT) {
            if(error) *error=(LbBeamError){LB_BEAM_LIMIT,0,"batch byte limit"};
            if(report) report->stage="batch byte limit";
            return LB_CODE_LIMIT;
        }
    }
    if(!lb_size_mul(count,sizeof(*modules),&module_bytes) ||
       !lb_size_mul(count,sizeof(*programs),&program_bytes) ||
       !lb_size_mul(count,sizeof(*states),&state_bytes)) {
        if(error) *error=(LbBeamError){LB_BEAM_LIMIT,0,"batch control size"};
        if(report) report->stage="batch control size";
        return LB_CODE_LIMIT;
    }
    space->loading=1;
    if(lb_alloc_domain_allocate(space->domain,module_bytes,&memory)!=LB_ALLOC_OK) {
        result=LB_CODE_NO_MEMORY;
        if(error) *error=(LbBeamError){LB_BEAM_NO_MEMORY,0,"batch module controls"};
        if(report) report->stage="batch module controls";
        goto finished;
    }
    modules=memory; memset(modules,0,module_bytes);
    if(lb_alloc_domain_allocate(space->domain,program_bytes,&memory)!=LB_ALLOC_OK) {
        result=LB_CODE_NO_MEMORY;
        if(error) *error=(LbBeamError){LB_BEAM_NO_MEMORY,0,"batch program controls"};
        if(report) report->stage="batch program controls";
        goto finished;
    }
    programs=memory; memset(programs,0,program_bytes);
    if(lb_alloc_domain_allocate(space->domain,state_bytes,&memory)!=LB_ALLOC_OK) {
        result=LB_CODE_NO_MEMORY;
        if(error) *error=(LbBeamError){LB_BEAM_NO_MEMORY,0,"batch loader state"};
        if(report) report->stage="batch loader state";
        goto finished;
    }
    states=memory; memset(states,0,state_bytes);
    for(i=0;i<count;++i) {
        LbBeamError item_error={LB_BEAM_OK,0,"decoded; atoms staged"};
        LbBeamStatus parse=lb_program_begin_staged(space->domain,space->atoms,inputs[i].data,
            inputs[i].size,&programs[i],&item_error);
        if(parse!=LB_BEAM_OK) {
            result=from_beam(parse); failed_program=programs[i];
            if(error) *error=item_error;
            if(report) { report->stage=item_error.stage; report->offset=item_error.offset;
                report->has_module_index=1; report->module_index=i; }
            goto rollback;
        }
        if(lb_alloc_domain_allocate(space->domain,sizeof(*modules[i]),&memory)!=LB_ALLOC_OK) {
            result=LB_CODE_NO_MEMORY; failed_program=programs[i];
            if(error) *error=(LbBeamError){LB_BEAM_NO_MEMORY,0,"module control"};
            if(report) { report->stage="module control"; report->has_module_index=1; report->module_index=i; }
            goto rollback;
        }
        modules[i]=memory; memset(modules[i],0,sizeof(*modules[i]));
        modules[i]->space=space; modules[i]->program=programs[i];
    }
    result=from_beam(lb_program_bind_batch(programs,count));
    if(result!=LB_CODE_OK) { failed_program=programs[0]; batch_error(error,report,result,failed_program,SIZE_MAX); goto rollback; }
    for(i=0;i<count;++i) {
        modules[i]->name=programs[i]->file_atoms[1];
        if(modules[i]->name==am_erlang && !space->preloading) {
            result=LB_CODE_EXISTS; failed_program=programs[i];
            if(error) *error=(LbBeamError){LB_BEAM_UNSUPPORTED,0,"protected native catalog/wrappers"};
            if(report) { report->stage="protected native catalog/wrappers"; report->has_module_index=1; report->module_index=i; }
            goto rollback;
        }
        for(peer=space->modules;peer;peer=peer->next) if(peer->name==modules[i]->name) {
            result=LB_CODE_EXISTS; failed_program=programs[i];
            if(error) *error=(LbBeamError){LB_BEAM_UNSUPPORTED,0,"module already published; hot reload excluded"};
            if(report) { report->stage="module already published; hot reload excluded"; report->has_module_index=1; report->module_index=i; }
            goto rollback;
        }
        for(j=0;j<i;++j) if(modules[j]->name==modules[i]->name) {
            result=LB_CODE_EXISTS; failed_program=programs[i];
            if(error) *error=(LbBeamError){LB_BEAM_UNSUPPORTED,0,"duplicate module name in batch"};
            if(report) { report->stage="duplicate module name in batch"; report->has_module_index=1; report->module_index=i; }
            goto rollback;
        }
    }
    for(i=0;i<count;++i) {
        result=lb_code_prepare_module(modules[i],&states[i]);
        if(result!=LB_CODE_OK) { failed_program=programs[i]; batch_error(error,report,result,failed_program,i); goto rollback; }
    }
    for(i=0;i<count;++i) {
        result=lb_code_resolve_imports(&states[i],modules,count);
        if(result!=LB_CODE_OK) { failed_program=programs[i]; batch_error(error,report,result,failed_program,i); goto rollback; }
    }
    result=validate_batch_importers(space,modules,count);
    if(result!=LB_CODE_OK) {
        failed_program=programs[0];
        if(error) *error=(LbBeamError){LB_BEAM_LIMIT,0,"dependency lease count"};
        if(report) report->stage="dependency lease count";
        goto rollback;
    }
    for(i=0;i<count;++i) {
        result=lb_code_compile_prepared(&states[i]);
        if(result!=LB_CODE_OK) { failed_program=programs[i]; batch_error(error,report,result,failed_program,i); goto rollback; }
    }
    /* Atom identities and all code become visible together. From this point
     * through publication, every operation is allocation-free and cannot fail. */
    lb_program_commit(programs[0]);
    for(i=0;i<count;++i) for(j=0;j<modules[i]->import_count;++j) {
        LbCodeModule *owner=modules[i]->dependencies[j];
        if(owner && owner->pending_importers) {
            owner->importers+=owner->pending_importers;
            owner->pending_importers=0;
        }
    }
    for(i=0;i<count;++i) {
        for(j=0;j<modules[i]->export_count;++j) {
            modules[i]->exports[j].next=space->exports; space->exports=&modules[i]->exports[j];
        }
        modules[i]->next=space->modules; space->modules=modules[i]; modules[i]->published=1;
        if(modules[i]->literal_fun_uses) space->has_fun_literals=1;
    }
    result=LB_CODE_OK;
    if(error) *error=(LbBeamError){LB_BEAM_OK,0,"published eager BEAM module batch"};
    if(report) report->stage="published eager BEAM module batch";
    goto finished;
rollback:
    batch_cleanup(space,modules,programs,count);
finished:
    if(states) lb_release(space->domain,states);
    if(programs) lb_release(space->domain,programs);
    if(modules) lb_release(space->domain,modules);
    space->loading=0;
    return result;
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
LbCodeStatus lb_code_unload_all(LbCodeSpace *space)
{
    LbCodeModule *module,*next;
    Export **link;
    if(!space) return LB_CODE_INVALID;
    if(space->loading || space->messages) return LB_CODE_BUSY;
    for(module=space->modules;module;module=module->next)
        if(module->users || module->pending_importers) return LB_CODE_BUSY;
    for(link=&space->exports;*link;) {
        if((*link)->owner) *link=(*link)->next; else link=&(*link)->next;
    }
    module=space->modules; space->modules=NULL; space->has_fun_literals=0;
    while(module) {
        next=module->next; module->published=0;
        lb_beam_program_destroy(module->program);
        lb_release(space->domain,module->dependencies);
        lb_release(space->domain,module); module=next;
    }
    return LB_CODE_OK;
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
