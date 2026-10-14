%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(monitor_reference).
-export([run/0]).
run() ->
    %% The OTP boot callback may itself trap exits. Compare fresh process
    %% defaults with native host roots, not the special init process's flags.
    {Pid,Ref} = spawn_monitor(fun cases/0),
    receive {'DOWN',Ref,process,Pid,normal} -> ok;
            {'DOWN',Ref,process,Pid,Reason} -> error({reference_failed,Reason}) end.
cases() ->
    lists:foreach(fun(N) ->
        B = list_to_binary([(I*173+19) band 255 || I <- lists:seq(0,N-1)]),
        lists:foreach(fun(F) -> B = apply(monitor_slice,F,[B]) end,
                      [copy,named,local_name,missing,dead,cancel,self_monitor,mfa,mfa_peer,exit_payload,links,unlink_live,link_chain,link_outcomes,linked_arm,linked_drain])
    end, [0,65,8192]),
    lists:foreach(fun(F) ->
        try apply(monitor_slice,F,[<<7>>]) of _ -> error(expected_badarg)
        catch error:badarg -> ok end
    end, [bad_kind,bad_target,bad_name,bad_node,bad_reference,bad_module,bad_function,bad_args,bad_link,bad_unlink]),
    try monitor_slice:dead_link(<<>>) of _ -> error(expected_noproc)
    catch error:noproc -> ok end,
    Ref = monitor(process,self()),
    true = demonitor(Ref),
    true = demonitor(Ref),
    lists:foreach(fun({Type,Target}) ->
        try monitor(Type,Target) of _ -> error(expected_badarg)
        catch error:badarg -> ok end
    end, [{other,self()},{process,42},{process,{42,node()}},{process,{name,42}}]),
    try demonitor(42) of _ -> error(expected_badarg)
    catch error:badarg -> ok end,
    io:format("MONITOR_REFERENCE_OK~n").
