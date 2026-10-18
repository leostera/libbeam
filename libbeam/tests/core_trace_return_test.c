// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@osteria.io>
#include "process_internal.h"
#include <stdio.h>
#define CHECK(condition) do { if(!(condition)) { fprintf(stderr,"CHECK %s:%d: %s\n",__FILE__,__LINE__,#condition); exit(1); } } while(0)

int main(int argc,char **argv)
{
    static const BeamInstr trace_returns[]={op_i_return_to_trace,op_return_trace,op_i_call_trace_return};
    static const size_t trace_frame_words[]={1,3,3};
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

    for(size_t i=0;i<sizeof(trace_returns)/sizeof(trace_returns[0]);++i) {
        LbCodeEntry *entry; LbProcess *process;
        const size_t extra=trace_frame_words[i],frame_words=extra+1;
        CHECK(lb_instruction_supported(trace_returns[i]));
        CHECK(lb_code_entry_acquire(space,module_atom,function_atom,0,&entry)==LB_CODE_OK);
        CHECK(lb_process_create(entry,NULL,0,64,&process)==LB_PROCESS_READY);
        lb_code_entry_release(entry);
        Eterm *frame=process->hend-(frame_words+1);
        for(size_t word=0;word<frame_words;++word) frame[word]=NIL;
        frame[frame_words]=(Eterm)lb_host_return;
        process->stop=frame;
        process->reg[0]=NIL;
        ((BeamInstr *)process->i)[0]=trace_returns[i];
        CHECK(lb_process_run(process,10,10)==LB_PROCESS_DONE);
        lb_process_destroy(process);
    }

    CHECK(lb_code_unload(module)==LB_CODE_OK);
    CHECK(lb_code_space_destroy(space)==LB_CODE_OK);
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK);
    lb_engine_release(engine);
    free(beam);
    puts("CORE_TRACE_RETURN_OK frames=3 tracer_disabled=true");
    return 0;
}
