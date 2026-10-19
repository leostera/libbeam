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
    CHECK(lb_atoms_find(space->atoms,"native_record_check_opcode",26,&module_atom)==LB_ATOM_OK);
    CHECK(lb_atoms_find(space->atoms,"entry",5,&function_atom)==LB_ATOM_OK);

    for(size_t i=0;i<sizeof(checks)/sizeof(checks[0]);++i) {
        LbCodeEntry *entry; LbProcess *process;
        CHECK(lb_instruction_supported(checks[i]));
        CHECK(lb_code_entry_acquire(space,module_atom,function_atom,0,&entry)==LB_CODE_OK);
        CHECK(lb_process_create(entry,NULL,0,64,&process)==LB_PROCESS_READY);
        lb_code_entry_release(entry);
        BeamInstr *code=(BeamInstr *)process->i;
        code[0]=checks[i];
        code[1]=2; /* Packed predicates use a zero-offset source and failure edge. */
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

    for(size_t i=0;i<2;++i) {
        LbCodeEntry *entry; LbProcess *process;
        Eterm low=module_atom<am_true?module_atom:am_true;
        Eterm high=module_atom<am_true?am_true:module_atom;
        LbRecordField fields[]={
            {low,{TAG_a,(Sint)am_false},0},
            {high,{TAG_a,(Sint)am_true},1}
        };
        LbRecordDefinition definition={.name=module_atom,.exported=1,.field_count=2,.fields=fields};
        CHECK(lb_code_entry_acquire(space,module_atom,function_atom,0,&entry)==LB_CODE_OK);
        CHECK(lb_process_create(entry,NULL,0,64,&process)==LB_PROCESS_READY);
        lb_code_entry_release(entry);
        Eterm record=lb_record_create(process,&definition,module_atom,1,0,NULL);
        CHECK(is_record(record)); process->reg[0]=record;
        if(i) { CHECK(process->stop>process->htop+S_RESERVED); *--process->stop=record; }
        BeamInstr *code=(BeamInstr *)process->i;
        code[0]=i?op_i_get_record_elements_jyI:op_i_get_record_elements_jxI;
        code[1]=7; code[2]=((BeamInstr)4<<BEAM_WIDE_SHIFT);
        code[3]=low; code[4]=make_loader_x_reg(1);
        code[5]=high; code[6]=make_loader_x_reg(2);
        code[7]=op_normal_exit;
        CHECK(lb_process_run(process,10,10)==LB_PROCESS_DONE);
        CHECK(process->reg[1]==am_false && process->reg[2]==am_true);
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
        LbRecordField field={module_atom,{TAG_a,(Sint)am_true},0};
        LbRecordDefinition definition={.name=module_atom,.exported=1,.field_count=1,.fields=&field};
        BeamInstr updates[]={module_atom,make_loader_x_reg(0)};
        process->reg[0]=make_small(42);
        Eterm created=lb_record_create(process,&definition,module_atom,1,2,updates);
        CHECK(is_record(created));
        process->reg[0]=created;
        Eterm *record=record_val(created);
        CHECK(lb_flat_size(space->domain,process->reg[0],&words) && words==12);
        CHECK(lb_process_collect_live(process,0,1));
        record=record_val(process->reg[0]);
        CHECK(is_record(process->reg[0]) && is_tuple(record[1]));
        CHECK(lb_record_term_field_count(process->reg[0])==1 &&
              lb_record_term_definition(process->reg[0])->keys[0]==module_atom &&
              is_small(lb_record_term_definition(process->reg[0])->hash) &&
              tuple_val(lb_record_term_definition(process->reg[0])->field_order)[1]==make_small(0) &&
              record[2]==make_small(42));
        CHECK(lb_heap_reserve(process,words,1));
        Eterm *top=process->htop;
        Eterm copied=lb_copy_flat(process->reg[0],&top,&process->off_heap);
        CHECK(!is_non_value(copied) && is_record(copied)); process->htop=top;
        CHECK(lb_term_equal(space->domain,process->reg[0],copied,&equal)==LB_ALLOC_OK && equal);
        Eterm lo=module_atom<am_true?module_atom:am_true;
        Eterm hi=module_atom<am_true?am_true:module_atom;
        LbRecordField reversed[]={
            {lo,{TAG_a,(Sint)am_false},1},
            {hi,{TAG_a,(Sint)am_true},0},
        };
        LbRecordDefinition two={.name=module_atom,.exported=1,.field_count=2,.fields=reversed};
        Eterm another=lb_record_create(process,&two,module_atom,1,0,NULL);
        CHECK(is_record(another) && lb_record_term_field_count(another)==2);
        const LbRecordTermDefinition *two_def=lb_record_term_definition(another);
        Eterm *field_order=tuple_val(two_def->field_order);
        CHECK(field_order[1]==make_small(1) && field_order[2]==make_small(0));
        CHECK(two_def->keys[0]==lo && two_def->keys[1]==hi);
        Eterm hash_tuple[]={make_arityval(5),module_atom,module_atom,am_true,hi,lo};
        Eterm hash_args[]={make_tuple(hash_tuple),make_small(UINT64_C(1)<<32)};
        CHECK(two_def->hash==lb_bif_phash2_2(process,hash_args,NULL));
        BeamInstr update[]={module_atom,make_small(99)};
        Eterm updated=lb_record_update(process,process->reg[0],1,2,update);
        CHECK(is_record(updated) && lb_record_instance(updated)->values[0]==make_small(99));
        CHECK(lb_record_instance(process->reg[0])->values[0]==make_small(42));
        BeamInstr wrong[]={am_false,make_small(7)};
        CHECK(is_non_value(lb_record_update(process,process->reg[0],1,2,wrong)) &&
              process->freason==LB_EXC_BADFIELD && tuple_val(process->fvalue)[2]==am_false);
        CHECK(is_non_value(lb_record_create(process,&definition,module_atom,1,2,wrong)) &&
              process->freason==LB_EXC_BADFIELD && is_tuple(process->fvalue));
        CHECK(tuple_val(process->fvalue)[2]==am_false &&
              tuple_val(tuple_val(process->fvalue)[1])[1]==module_atom);
        field.default_value=(LbBeamArg){TAG_u,0};
        CHECK(is_non_value(lb_record_create(process,&definition,module_atom,1,0,NULL)) &&
              process->freason==LB_EXC_NOVALUE && is_tuple(process->fvalue));
        CHECK(tuple_val(process->fvalue)[2]==module_atom);
        Eterm *literal=malloc(3*sizeof(Eterm)); CHECK(literal);
        literal[0]=make_arityval(2); literal[1]=am_true; literal[2]=make_small(42);
        field.default_value=(LbBeamArg){TAG_q,(Sint)(make_tuple(literal)|TAG_LITERAL_PTR)};
        Eterm owned=lb_record_create(process,&definition,module_atom,1,0,NULL);
        CHECK(is_record(owned)); process->reg[0]=owned;
        free(literal);
        CHECK(is_tuple(lb_record_instance(owned)->values[0]) &&
              !is_literal_ptr(lb_record_instance(owned)->values[0]));
        CHECK(lb_process_collect_live(process,0,1));
        CHECK(tuple_val(lb_record_instance(process->reg[0])->values[0])[2]==make_small(42));
        lb_process_destroy(process);
    }
    for(size_t i=0;i<2;++i) {
        LbCodeEntry *entry; LbProcess *process;
        LbRecordField field={module_atom,{TAG_a,(Sint)am_true},0};
        LbRecordDefinition definition={.name=module_atom,.exported=0,.field_count=1,.fields=&field};
        CHECK(lb_code_entry_acquire(space,module_atom,function_atom,0,&entry)==LB_CODE_OK);
        CHECK(lb_process_create(entry,NULL,0,64,&process)==LB_PROCESS_READY);
        lb_code_entry_release(entry);
        Eterm record=lb_record_create(process,&definition,module_atom,0,0,NULL);
        CHECK(is_record(record)); process->reg[0]=record;
        if(i&1) { CHECK(process->stop>process->htop+S_RESERVED); *--process->stop=record; }
        BeamInstr *code=(BeamInstr *)process->i;
        code[0]=checks[i]; code[1]=2; code[2]=module_atom;
        code[3]=module_atom; code[4]=op_normal_exit;
        CHECK(lb_process_run(process,10,10)==LB_PROCESS_DONE);
        lb_process_destroy(process);
    }

    for(size_t i=2;i<6;++i) {
        LbCodeEntry *entry; LbProcess *process;
        LbRecordField field={module_atom,{TAG_a,(Sint)am_true},0};
        LbRecordDefinition definition={.name=module_atom,.exported=i<4,.field_count=1,.fields=&field};
        CHECK(lb_code_entry_acquire(space,module_atom,function_atom,0,&entry)==LB_CODE_OK);
        CHECK(lb_process_create(entry,NULL,0,64,&process)==LB_PROCESS_READY);
        lb_code_entry_release(entry);
        Eterm record=lb_record_create(process,&definition,module_atom,0,0,NULL);
        CHECK(is_record(record)); process->reg[0]=record;
        if(i&1) { CHECK(process->stop>process->htop+S_RESERVED); *--process->stop=record; }
        BeamInstr *code=(BeamInstr *)process->i;
        code[0]=checks[i]; code[1]=2; code[2]=module_atom;
        if(i>=4) code[3]=op_normal_exit;
        else code[2]=op_normal_exit;
        LbProcessStatus status=lb_process_run(process,10,10);
        CHECK(status==LB_PROCESS_DONE);
        lb_process_destroy(process);
    }

    for(size_t i=6;i<8;++i) for(int found=0;found<2;++found) {
        LbCodeEntry *entry; LbProcess *process;
        LbRecordField field={module_atom,{TAG_a,(Sint)am_true},0};
        LbRecordDefinition definition={.name=module_atom,.exported=0,.field_count=1,.fields=&field};
        CHECK(lb_code_entry_acquire(space,module_atom,function_atom,0,&entry)==LB_CODE_OK);
        CHECK(lb_process_create(entry,NULL,0,64,&process)==LB_PROCESS_READY);
        lb_code_entry_release(entry);
        Eterm record=lb_record_create(process,&definition,module_atom,0,0,NULL);
        CHECK(is_record(record)); process->reg[0]=record;
        if(i&1) { CHECK(process->stop>process->htop+S_RESERVED); *--process->stop=record; }
        BeamInstr *code=(BeamInstr *)process->i;
        code[0]=checks[i]; code[1]=5;
        code[2]=(2ULL<<BEAM_WIDE_SHIFT); /* one key/destination pair, src offset 0 */
        code[3]=found?module_atom:am_false;
        code[4]=make_loader_x_reg(1); code[5]=op_normal_exit;
        CHECK(lb_process_run(process,10,10)==LB_PROCESS_DONE);
        CHECK(process->reg[1]==(found?am_true:NIL));
        lb_process_destroy(process);
    }

    for(size_t i=0;i<4;++i) {
        LbCodeEntry *entry; LbProcess *process;
        LbRecordField field={module_atom,{TAG_a,(Sint)am_true},0};
        LbRecordDefinition definition={.name=module_atom,.exported=1,.field_count=1,.fields=&field};
        CHECK(lb_code_entry_acquire(space,module_atom,function_atom,0,&entry)==LB_CODE_OK);
        CHECK(lb_process_create(entry,NULL,0,64,&process)==LB_PROCESS_READY);
        lb_code_entry_release(entry);
        Eterm record=lb_record_create(process,&definition,module_atom,0,0,NULL);
        CHECK(is_record(record)); process->reg[0]=record;
        if(i&1) { CHECK(process->stop>process->htop+S_RESERVED); *--process->stop=record; }
        BeamInstr *code=(BeamInstr *)process->i;
        code[0]=field_getters[i]; code[1]=0; code[2]=0;
        code[3]=i<2?module_atom:am_Underscore;
        code[4]=module_atom; code[5]=op_normal_exit;
        CHECK(lb_process_run(process,10,10)==LB_PROCESS_DONE);
        CHECK(process->reg[0]==am_true);
        lb_process_destroy(process);
    }

    {
        LbCodeEntry *entry; LbProcess *process;
        LbRecordField field={module_atom,{TAG_a,(Sint)am_true},0};
        LbRecordDefinition definition={.name=module_atom,.exported=0,.field_count=1,.fields=&field};
        CHECK(lb_code_entry_acquire(space,module_atom,function_atom,0,&entry)==LB_CODE_OK);
        CHECK(lb_process_create(entry,NULL,0,64,&process)==LB_PROCESS_READY);
        lb_code_entry_release(entry);
        Eterm record=lb_record_create(process,&definition,module_atom,0,0,NULL);
        CHECK(is_record(record)); process->reg[0]=record;
        BeamInstr *code=(BeamInstr *)process->i;
        code[0]=op_i_get_local_record_field_jxaad; code[1]=0; code[2]=0;
        code[3]=module_atom; code[4]=am_false; code[5]=op_normal_exit;
        CHECK(lb_process_run(process,10,10)==LB_PROCESS_EXCEPTION);
        CHECK(lb_process_exception(process)==am_badfield);
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
