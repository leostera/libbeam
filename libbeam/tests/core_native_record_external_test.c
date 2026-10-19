// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#include "process_internal.h"
#include "binary_match.h"
#include <stdio.h>
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"CHECK %s:%d: %s\n",__FILE__,__LINE__,#c); exit(1); } } while(0)
typedef struct { Eterm expected; const unsigned char *bytes; size_t size; unsigned calls; } RecordResolver;
static LbCodeInput resolve_record(void *context,Eterm module)
{
    RecordResolver *resolver=context;
    ++resolver->calls;
    return module==resolver->expected ? (LbCodeInput){resolver->bytes,resolver->size} : (LbCodeInput){NULL,0};
}
static unsigned char *file_bytes(const char *path,size_t *size)
{
    FILE *file=fopen(path,"rb"); long length;
    CHECK(file && !fseek(file,0,SEEK_END)); length=ftell(file);
    CHECK(length>0 && !fseek(file,0,SEEK_SET));
    unsigned char *bytes=malloc((size_t)length);
    CHECK(bytes && fread(bytes,1,(size_t)length,file)==(size_t)length && !fclose(file));
    *size=(size_t)length; return bytes;
}
int main(int argc,char **argv)
{
    LbEngine *engine; LbCodeSpace *space; LbCodeModule *public,*external;
    LbBeamError error; size_t public_size,external_size;
    CHECK(argc==3);
    CHECK(lb_instruction_supported(op_i_create_native_record_jctI) &&
          lb_instruction_supported(op_finish_create_record));
    unsigned char *pub=file_bytes(argv[1],&public_size),*ext=file_bytes(argv[2],&external_size);
    CHECK(lb_engine_create(NULL,&engine)==LB_ENGINE_OK && lb_code_space_create(engine,&space)==LB_CODE_OK);
    CHECK(lb_code_load(space,pub,public_size,&public,&error)==LB_CODE_OK);
    LbCodeStatus loaded=lb_code_load(space,ext,external_size,&external,&error);
    if(loaded!=LB_CODE_OK) fprintf(stderr,"external load status=%d stage=%s offset=%zu\n",loaded,error.stage,error.offset);
    CHECK(loaded==LB_CODE_OK);
    size_t external_creates=0,external_reads=0;
    for(size_t i=0;i<external->word_count;++i) {
        external_creates+=external->words[i]==op_i_create_native_record_jctI;
        external_reads+=external->words[i]==op_i_get_record_field_jxcad;
    }
    CHECK(external_creates && external_reads);
    Eterm module,function; LbCodeEntry *entry; LbProcess *process;
    Eterm public_name,create_name,one;
    CHECK(lb_atoms_find(space->atoms,"native_record_public",20,&public_name)==LB_ATOM_OK);
    CHECK(lb_atoms_find(space->atoms,"create",6,&create_name)==LB_ATOM_OK);
    CHECK(lb_atoms_find(space->atoms,"one",3,&one)==LB_ATOM_OK);
    CHECK(lb_code_entry_acquire(space,public_name,create_name,1,&entry)==LB_CODE_OK);
    CHECK(lb_process_create(entry,&one,1,256,&process)==LB_PROCESS_READY);
    lb_code_entry_release(entry);
    LbProcessStatus create_status;
    do { create_status=lb_process_run(process,1000,1000); } while(create_status==LB_PROCESS_YIELDED);
    CHECK(create_status==LB_PROCESS_DONE && is_record(lb_process_result(process)));
    Eterm hash_arg[]={lb_process_result(process)};
    Eterm hash=lb_bif_phash2_1(process,hash_arg,NULL);
    CHECK(is_small(hash) && signed_val(hash)==125246533); /* OTP 29 oracle */
    lb_process_destroy(process);
    CHECK(lb_atoms_find(space->atoms,"native_record_external",22,&module)==LB_ATOM_OK);
    CHECK(lb_atoms_find(space->atoms,"entry",5,&function)==LB_ATOM_OK);
    CHECK(lb_code_entry_acquire(space,module,function,1,&entry)==LB_CODE_OK);
    Eterm args[]={am_true};
    CHECK(lb_process_create(entry,args,1,256,&process)==LB_PROCESS_READY);
    lb_code_entry_release(entry);
    LbProcessStatus status;
    do { status=lb_process_run(process,1000,1000); } while(status==LB_PROCESS_YIELDED);
    CHECK(status==LB_PROCESS_DONE);
    LbBitstringView result;
    CHECK(lb_bitstring_view(lb_process_result(process),&result) && result.bit_size==16 &&
          !memcmp(result.data,"ok",2));
    lb_process_destroy(process);
    CHECK(lb_atoms_find(space->atoms,"record",6,&function)==LB_ATOM_OK);
    Eterm record_args[]={one};
    CHECK(lb_code_entry_acquire(space,module,function,1,&entry)==LB_CODE_OK);
    CHECK(lb_process_create(entry,record_args,1,256,&process)==LB_PROCESS_READY);
    lb_code_entry_release(entry);
    do { status=lb_process_run(process,1000,1000); } while(status==LB_PROCESS_YIELDED);
    CHECK(status==LB_PROCESS_DONE && is_record(lb_process_result(process)));
    CHECK(lb_code_unload(public)==LB_CODE_OK); /* definition and defaults are owned */
    CHECK(lb_process_collect(process,0)==LB_PROCESS_DONE);
    Eterm surviving[]={lb_process_result(process)};
    hash=lb_bif_phash2_1(process,surviving,NULL);
    CHECK(is_small(hash) && signed_val(hash)==125246533);
    lb_process_destroy(process);
    CHECK(lb_code_entry_acquire(space,module,function,1,&entry)==LB_CODE_OK);
    CHECK(lb_process_create(entry,record_args,1,256,&process)==LB_PROCESS_READY);
    lb_code_entry_release(entry);
    do { status=lb_process_run(process,1000,1000); } while(status==LB_PROCESS_YIELDED);
    CHECK(status==LB_PROCESS_EXCEPTION && lb_process_exception(process)==am_badrecord);
    Eterm reason=lb_process_exception_value(process);
    CHECK(is_tuple(reason) && arityval(*tuple_val(reason))==2 &&
          tuple_val(reason)[1]==am_badrecord && is_tuple(tuple_val(reason)[2]));
    lb_process_destroy(process);
    RecordResolver invalid={public_name,pub,3,0};
    CHECK(lb_code_space_set_record_resolver(space,resolve_record,&invalid)==LB_CODE_OK);
    CHECK(lb_code_entry_acquire(space,module,function,1,&entry)==LB_CODE_OK);
    CHECK(lb_process_create(entry,record_args,1,256,&process)==LB_PROCESS_READY);
    lb_code_entry_release(entry);
    do { status=lb_process_run(process,1000,1000); } while(status==LB_PROCESS_YIELDED);
    CHECK(status==LB_PROCESS_EXCEPTION && lb_process_exception(process)==am_badrecord);
    CHECK(invalid.calls==1); /* A failed ensure-loaded attempt is never retried forever. */
    lb_process_destroy(process);
    RecordResolver resolver={public_name,pub,public_size,0};
    CHECK(lb_code_space_set_record_resolver(space,resolve_record,&resolver)==LB_CODE_OK);
    CHECK(lb_code_entry_acquire(space,module,function,1,&entry)==LB_CODE_OK);
    CHECK(lb_process_create(entry,record_args,1,256,&process)==LB_PROCESS_READY);
    lb_code_entry_release(entry);
    do { status=lb_process_run(process,1000,1000); } while(status==LB_PROCESS_YIELDED);
    CHECK(status==LB_PROCESS_DONE && is_record(lb_process_result(process)));
    CHECK(resolver.calls==1); /* missing module -> resolver -> finish_create_record -> retry */
    lb_process_destroy(process);
    CHECK(lb_code_space_set_record_resolver(space,NULL,NULL)==LB_CODE_OK);
    public=NULL;
    for(LbCodeModule *candidate=space->modules;candidate;candidate=candidate->next)
        if(candidate->name==public_name) { public=candidate; break; }
    CHECK(public && lb_code_unload(public)==LB_CODE_OK);

    CHECK(lb_code_entry_acquire(space,module,function,1,&entry)==LB_CODE_OK);
    CHECK(lb_process_create(entry,record_args,1,256,&process)==LB_PROCESS_READY);
    lb_code_entry_release(entry);
    ((BeamInstr *)process->i)[0]=op_finish_create_record;
    CHECK(lb_process_run(process,1000,1000)==LB_PROCESS_INVALID);
    lb_process_destroy(process);
    CHECK(lb_code_unload(external)==LB_CODE_OK);
    CHECK(lb_code_space_destroy(space)==LB_CODE_OK);
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine);
    free(pub); free(ext);
    puts("CORE_NATIVE_RECORD_EXTERNAL_OK"); return 0;
}
