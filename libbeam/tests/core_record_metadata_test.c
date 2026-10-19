// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#include "code_internal.h"
#include <stdio.h>
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"CHECK %s:%d: %s\n",__FILE__,__LINE__,#c); exit(1); } } while(0)
int main(int argc,char **argv)
{
    FILE *file; long length; unsigned char *beam; size_t pos=12,recs=0;
    LbEngine *engine; LbCodeSpace *space; LbBeamProgram *program; LbBeamError error;
    CHECK(argc==2 && (file=fopen(argv[1],"rb")) && !fseek(file,0,SEEK_END));
    length=ftell(file); CHECK(length>0 && !fseek(file,0,SEEK_SET));
    beam=malloc((size_t)length); CHECK(beam && fread(beam,1,(size_t)length,file)==(size_t)length && !fclose(file));
    for(;pos+8<=(size_t)length;) {
        size_t size=((size_t)beam[pos+4]<<24)|((size_t)beam[pos+5]<<16)|((size_t)beam[pos+6]<<8)|beam[pos+7];
        CHECK(size<=(size_t)length-pos-8);
        if(!memcmp(beam+pos,"Recs",4)) { recs=pos+8; break; }
        pos+=8+((size+3)&~(size_t)3);
    }
    CHECK(recs && recs+12<=(size_t)length);
    CHECK(lb_engine_create(NULL,&engine)==LB_ENGINE_OK && lb_code_space_create(engine,&space)==LB_CODE_OK);
    CHECK(lb_beam_program_prepare(engine->domain,space->atoms,beam,(size_t)length,&program,&error)==LB_BEAM_OK);
    CHECK(program->record_count==1 && program->record_field_count==2);
    CHECK(program->records && !program->records[0].exported && program->records[0].field_count==2);
    CHECK(is_atom(program->records[0].name) && is_atom(program->records[0].fields[0].key));
    CHECK(is_atom(program->records[0].fields[1].key));
    CHECK(is_atom((Eterm)program->records[0].fields[0].default_value.val));
    CHECK(is_atom((Eterm)program->records[0].fields[1].default_value.val));
    LbBeamBytes name;
    Eterm first=0,second=0,one=0,two=0,thing=0;
    for(size_t i=1;i<=lb_beam_image_info(program->image)->atom_count;++i) {
        CHECK(lb_beam_image_atom(program->image,(uint32_t)i,&name)==LB_BEAM_OK);
        if(name.size==5 && !memcmp(name.data,"thing",5)) thing=program->file_atoms[i];
        if(name.size==5 && !memcmp(name.data,"first",5)) first=program->file_atoms[i];
        if(name.size==6 && !memcmp(name.data,"second",6)) second=program->file_atoms[i];
        if(name.size==3 && !memcmp(name.data,"one",3)) one=program->file_atoms[i];
        if(name.size==3 && !memcmp(name.data,"two",3)) two=program->file_atoms[i];
    }
    CHECK(first && second && one && two && thing && program->records[0].name==thing);
    CHECK(program->records[0].fields[0].key<program->records[0].fields[1].key);
    for(size_t i=0;i<2;++i) {
        const LbRecordField *field=&program->records[0].fields[i];
        CHECK((field->key==first && (Eterm)field->default_value.val==one) ||
              (field->key==second && (Eterm)field->default_value.val==two));
    }
    lb_beam_program_destroy(program);
    LbCodeModule *module;
    CHECK(lb_code_load(space,beam,(size_t)length,&module,&error)==LB_CODE_OK);
    size_t local_creates=0,external_creates=0,updates=0;
    for(size_t i=0;i<module->word_count;++i) {
        local_creates+=module->words[i]==op_i_create_local_native_record_qdtI;
        external_creates+=module->words[i]==op_i_create_native_record_jctI;
        updates+=module->words[i]==op_i_update_native_record_jsdtI;
    }
    CHECK(local_creates && !external_creates && updates);
    CHECK(lb_code_unload(module)==LB_CODE_OK);
    unsigned char version=beam[recs+3]; beam[recs+3]=1;
    CHECK(lb_beam_program_prepare(engine->domain,space->atoms,beam,(size_t)length,&program,&error)==LB_BEAM_BAD_FORMAT);
    CHECK(!program); beam[recs+3]=version;
    /* A forged count must be rejected before allocating any definitions. */
    unsigned char saved_count[4]; memcpy(saved_count,beam+recs+4,4);
    memset(beam+recs+4,0xff,4);
    CHECK(lb_beam_program_prepare(engine->domain,space->atoms,beam,(size_t)length,&program,&error)==LB_BEAM_BAD_FORMAT);
    CHECK(!program); memcpy(beam+recs+4,saved_count,4);
    unsigned char saved_op=beam[recs+12]; beam[recs+12]=0;
    CHECK(lb_beam_program_prepare(engine->domain,space->atoms,beam,(size_t)length,&program,&error)==LB_BEAM_BAD_FORMAT);
    CHECK(!program); beam[recs+12]=saved_op;
    CHECK(lb_code_space_destroy(space)==LB_CODE_OK);
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine); free(beam);
    puts("CORE_RECORD_METADATA_OK version=0 records=1 fields=2");
    return 0;
}
