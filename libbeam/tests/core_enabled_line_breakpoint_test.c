// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@osteria.io>
#include "process_internal.h"
#include <stdio.h>
#define CHECK(condition) do { if(!(condition)) { fprintf(stderr,"CHECK %s:%d: %s\n",__FILE__,__LINE__,#condition); exit(1); } } while(0)

int main(int argc,char **argv)
{
    FILE *file; long length; unsigned char *beam;
    LbEngine *engine; LbCodeSpace *space; LbCodeModule *module; LbCodeEntry *entry;
    LbProcess *process; LbBeamError error; Eterm module_atom,function_atom;
    CHECK(argc==2 && (file=fopen(argv[1],"rb")) && !fseek(file,0,SEEK_END));
    length=ftell(file); CHECK(length>0 && !fseek(file,0,SEEK_SET));
    beam=malloc((size_t)length); CHECK(beam && fread(beam,1,(size_t)length,file)==(size_t)length && !fclose(file));
    CHECK(lb_engine_create(NULL,&engine)==LB_ENGINE_OK);
    CHECK(lb_code_space_create(engine,&space)==LB_CODE_OK);
    CHECK(lb_code_load(space,beam,(size_t)length,&module,&error)==LB_CODE_OK);
    CHECK(lb_atoms_find(space->atoms,"system_limit_body_opcode",24,&module_atom)==LB_ATOM_OK);
    CHECK(lb_atoms_find(space->atoms,"entry",5,&function_atom)==LB_ATOM_OK);
    CHECK(lb_code_entry_acquire(space,module_atom,function_atom,0,&entry)==LB_CODE_OK);
    CHECK(lb_process_create(entry,NULL,0,64,&process)==LB_PROCESS_READY);
    lb_code_entry_release(entry);

    CHECK(lb_instruction_supported(op_i_enabled_line_breakpoint_t));
    BeamInstr *code=(BeamInstr *)process->i;
    code[0]=op_i_enabled_line_breakpoint_t;
    code[1]=0; /* Live is ignored when this isolate has no debugger. */
    code[2]=op_normal_exit;
    CHECK(lb_process_run(process,10,10)==LB_PROCESS_DONE);

    lb_process_destroy(process);

    CHECK(lb_instruction_supported(op_i_debug_breakpoint));
    CHECK(lb_code_entry_acquire(space,module_atom,function_atom,0,&entry)==LB_CODE_OK);
    CHECK(lb_process_create(entry,NULL,0,64,&process)==LB_PROCESS_READY);
    lb_code_entry_release(entry);
    ((BeamInstr *)process->i)[0]=op_i_debug_breakpoint;
    CHECK(lb_process_run(process,10,10)==LB_PROCESS_EXCEPTION);
    CHECK(lb_process_exception(process)==am_undef);
    lb_process_destroy(process);

    CHECK(lb_instruction_supported(op_i_generic_breakpoint));
    CHECK(lb_code_entry_acquire(space,module_atom,function_atom,0,&entry)==LB_CODE_OK);
    CHECK(lb_process_create(entry,NULL,0,64,&process)==LB_PROCESS_READY);
    lb_code_entry_release(entry);
    LbCodeInfo *info=(LbCodeInfo *)(process->i-5);
    LbGenericBreakpoint breakpoint={op_normal_exit};
    CHECK(info->op==op_i_func_info_IaaI && !info->breakpoint);
    info->breakpoint=&breakpoint;
    ((BeamInstr *)process->i)[0]=op_i_generic_breakpoint;
    CHECK(lb_process_run(process,10,10)==LB_PROCESS_DONE);
    info->breakpoint=NULL;
    lb_process_destroy(process);

    CHECK(lb_code_entry_acquire(space,module_atom,function_atom,0,&entry)==LB_CODE_OK);
    CHECK(lb_process_create(entry,NULL,0,64,&process)==LB_PROCESS_READY);
    lb_code_entry_release(entry);
    ((BeamInstr *)process->i)[0]=op_i_generic_breakpoint;
    CHECK(lb_process_run(process,10,10)==LB_PROCESS_INVALID);
    lb_process_destroy(process);

    CHECK(lb_code_unload(module)==LB_CODE_OK);
    CHECK(lb_code_space_destroy(space)==LB_CODE_OK);
    CHECK(lb_engine_shutdown(engine)==LB_ENGINE_OK);
    lb_engine_release(engine);
    free(beam);
    puts("CORE_ENABLED_LINE_BREAKPOINT_OK no_debugger=true");
    return 0;
}
