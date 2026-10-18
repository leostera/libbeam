// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@osteria.io>
#include "process_internal.h"
#include "term_compare.h"
#include "native_record.h"
#include <stdio.h>
#define CHECK(condition) do { if(!(condition)) { fprintf(stderr,"CHECK %s:%d: %s\n",__FILE__,__LINE__,#condition); exit(1); } } while(0)

int main(int argc,char **argv)
{
    static const BeamInstr field_getters[]={
        op_i_get_local_record_field_jxaad,op_i_get_local_record_field_jyaad,
        op_i_get_record_field_jxcad,op_i_get_record_field_jycad
    };
    static const BeamInstr checks[]={
        op_i_is_native_record_fxaa,op_i_is_native_record_fyaa,
        op_i_is_record_accessible_fx,op_i_is_record_accessible_fy,
        op_i_is_wildcard_record_accessible_fxa,op_i_is_wildcard_record_accessible_fya,
        op_i_get_record_elements_jxI,op_i_get_record_elements_jyI
    };
    FILE *file; long length; unsigned char *beam;
    LbEngine *engine; LbCodeSpace *space; LbCodeModule *module; LbBeamError error;
    Eterm module_atom,function_atom;
    CHECK(argc==2 && (file=fopen(argv[1],"rb")) && !fseek(file,0,SEEK_END));
    length=ftell(file); CHECK(length>0 && !fseek(file,0,SEEK_SET));
    beam=malloc((size_t)length); CHECK(beam && fread(beam,1,(size_t)length,file)==(size_t)length && !fclose(file));
    CHECK(lb_engine_create(NULL,&engine)==LB_ENGINE_OK);
    CHECK(lb_code_space_create(engine,&space)==LB_CODE_OK);
    CHECK(lb_code_load(space,beam,(size_t)length,&module,&error)==LB_CODE_OK);
    CHECK(lb_atoms_find(space->atoms,"system_limit_body_opcode",24,&module_atom)==LB_ATOM_OK);
    CHECK(lb_atoms_find(space->atoms,"entry",5,&function_atom)==LB_ATOM_OK);

    for(size_t i=0;i<sizeof(checks)/sizeof(checks[0]);++i) {
        LbCodeEntry *entry; LbProcess *process;
        CHECK(lb_instruction_supported(checks[i]));
        CHECK(lb_code_entry_acquire(space,module_atom,function_atom,0,&entry)==LB_CODE_OK);
        CHECK(lb_process_create(entry,NULL,0,64,&process)==LB_PROCESS_READY);
        lb_code_entry_release(entry);
        BeamInstr *code=(BeamInstr *)process->i;
        code[0]=checks[i];
        code[1]=2; /* Relative failure edge inside the fixture's code range. */
        code[2]=op_normal_exit;
        CHECK(lb_process_run(process,10,10)==LB_PROCESS_DONE);
        lb_process_destroy(process);
        if(checks[i]!=op_i_get_record_elements_jxI && checks[i]!=op_i_get_record_elements_jyI) continue;
        CHECK(lb_code_entry_acquire(space,module_atom,function_atom,0,&entry)==LB_CODE_OK);
        CHECK(lb_process_create(entry,NULL,0,64,&process)==LB_PROCESS_READY);
        lb_code_entry_release(entry);
        code=(BeamInstr *)process->i;
        code[0]=checks[i]; code[1]=0;
        CHECK(lb_process_run(process,10,10)==LB_PROCESS_INVALID);
        lb_process_destroy(process);
    }

    for(size_t i=0;i<sizeof(field_getters)/sizeof(field_getters[0]);++i) {
        LbCodeEntry *entry; LbProcess *process;
        CHECK(lb_instruction_supported(field_getters[i]));
        CHECK(lb_code_entry_acquire(space,module_atom,function_atom,0,&entry)==LB_CODE_OK);
        CHECK(lb_process_create(entry,NULL,0,64,&process)==LB_PROCESS_READY);
        lb_code_entry_release(entry);
        BeamInstr *code=(BeamInstr *)process->i;
        code[0]=field_getters[i]; code[1]=2; code[2]=op_normal_exit;
        CHECK(lb_process_run(process,10,10)==LB_PROCESS_DONE);
        lb_process_destroy(process);
        if(i&1) continue; /* A y-source is a stack slot, not the initial CP. */
        CHECK(lb_code_entry_acquire(space,module_atom,function_atom,0,&entry)==LB_CODE_OK);
        CHECK(lb_process_create(entry,NULL,0,64,&process)==LB_PROCESS_READY);
        lb_code_entry_release(entry);
        code=(BeamInstr *)process->i;
        code[0]=field_getters[i]; code[1]=0; code[2]=0;
        process->reg[0]=make_small(7);
        CHECK(lb_process_run(process,10,10)==LB_PROCESS_EXCEPTION);
        CHECK(lb_process_exception(process)==am_badrecord);
        lb_process_destroy(process);
    }

    /* A native record's definition and values are GC children, not raw
     * payload. Prove both collector forwarding and message-style flat copy. */
    {
        LbCodeEntry *entry; LbProcess *process;
        size_t words; int equal;
        CHECK(lb_code_entry_acquire(space,module_atom,function_atom,0,&entry)==LB_CODE_OK);
        CHECK(lb_process_create(entry,NULL,0,64,&process)==LB_PROCESS_READY);
        lb_code_entry_release(entry);
        CHECK(lb_heap_reserve(process,10,0));
        Eterm *def=process->htop; process->htop+=7;
        def[0]=make_arityval(6); def[1]=NIL; def[2]=make_small(0);
        def[3]=module_atom; def[4]=module_atom;
        def[5]=am_true; def[6]=module_atom;
        Eterm *record=process->htop; process->htop+=3;
        record[0]=make_record_header(1); record[1]=make_tuple(def); record[2]=make_small(42);
        process->reg[0]=make_boxed(record);
        CHECK(is_record(process->reg[0]));
        CHECK(lb_flat_size(space->domain,process->reg[0],&words) && words==10);
        CHECK(lb_process_collect_live(process,0,1));
        record=record_val(process->reg[0]);
        CHECK(is_record(process->reg[0]) && is_tuple(record[1]));
        CHECK(lb_record_term_field_count(process->reg[0])==1 &&
              lb_record_term_definition(process->reg[0])->keys[0]==module_atom &&
              record[2]==make_small(42));
        CHECK(lb_heap_reserve(process,words,1));
        Eterm *top=process->htop;
        Eterm copied=lb_copy_flat(process->reg[0],&top,&process->off_heap);
        CHECK(!is_non_value(copied) && is_record(copied)); process->htop=top;
        CHECK(lb_term_equal(space->domain,process->reg[0],copied,&equal)==LB_ALLOC_OK && equal);
        lb_process_destroy(process);
    }
    CHECK(lb_code_unload(module)==LB_CODE_OK);
    CHECK(lb_code_space_destroy(space)==LB_CODE_OK);
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK);
    lb_engine_release(engine);
    free(beam);
    puts("CORE_NATIVE_RECORD_CHECK_OK no_native_record_representation=true accessibility=false elements=false");
    return 0;
}
