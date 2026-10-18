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
    lb_beam_program_destroy(program);
    unsigned char version=beam[recs+3]; beam[recs+3]=1;
    CHECK(lb_beam_program_prepare(engine->domain,space->atoms,beam,(size_t)length,&program,&error)==LB_BEAM_BAD_FORMAT);
    CHECK(!program); beam[recs+3]=version;
    CHECK(lb_code_space_destroy(space)==LB_CODE_OK);
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK); lb_engine_release(engine); free(beam);
    puts("CORE_RECORD_METADATA_OK version=0 records=1 fields=2");
    return 0;
}
