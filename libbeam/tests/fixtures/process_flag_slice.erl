%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(process_flag_slice).
-export([identity/1,copy/1,lifecycle/1,bad_value/1,bad_flag/1,unsupported/1]).
identity(B) -> B.
copy(B) ->
    false = process_flag(trap_exit,true),
    [{trap_exit,true},{trap_exit,true},{error_handler,error_handler}] =
        process_info(self(),[trap_exit,trap_exit,error_handler]),
    true = erlang:garbage_collect(),
    {trap_exit,true} = process_info(self(),trap_exit),
    true = process_flag(trap_exit,false),
    {trap_exit,false} = process_info(self(),trap_exit),
    B.
lifecycle(B) ->
    false = process_flag(trap_exit,true),
    Parent = self(),
    {Child,Ref} = spawn_monitor(fun() ->
        false = process_flag(trap_exit,true),
        true = process_flag(trap_exit,false),
        Parent ! {self(),fresh},
        receive stop -> ok end
    end),
    receive {Child,fresh} -> ok end,
    {trap_exit,false} = process_info(Child,trap_exit),
    {trap_exit,true} = process_info(self(),trap_exit),
    Child ! stop,
    receive {'DOWN',Ref,process,Child,normal} -> ok end,
    {Victim,Killed} = spawn_monitor(fun() ->
        false = process_flag(trap_exit,true),
        Parent ! {self(),armed},
        receive stop -> ok end
    end),
    receive {Victim,armed} -> ok end,
    {trap_exit,true} = process_info(Victim,trap_exit),
    true = exit(Victim,kill),
    receive {'DOWN',Killed,process,Victim,killed} -> ok end,
    {Exiting,Exited} = spawn_monitor(fun() ->
        false = process_flag(trap_exit,true),
        exit(flag_exit)
    end),
    receive {'DOWN',Exited,process,Exiting,flag_exit} -> ok end,
    true = process_flag(trap_exit,false),
    B.
bad_value(B) -> process_flag(trap_exit,B).
bad_flag(B) -> process_flag(B,true).
unsupported(_) -> process_flag(priority,high).
