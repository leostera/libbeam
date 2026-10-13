%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(process_info_reference).
-export([run/0]).
run() ->
    {error_handler,error_handler} = process_info_slice:query(error_handler),
    [{error_handler,error_handler},{error_handler,error_handler}] = process_info_slice:query([error_handler,error_handler]),
    [] = process_info_slice:query([]),
    lists:foreach(fun(Selector) -> badarg(fun() -> process_info_slice:query(Selector) end) end,
        [1,[1],[error_handler|bad_tail]]),
    badarg(fun() -> process_info_slice:other({not_a_pid,error_handler}) end),
    Parent=self(), {Pid,Ref}=spawn_monitor(fun() ->
        process_flag(error_handler,custom_handler), Parent ! {ready,self()}, receive stop -> ok end
    end),
    receive {ready,Pid} -> ok end,
    {error_handler,custom_handler}=process_info_slice:other({Pid,error_handler}),
    Pid ! stop, receive {'DOWN',Ref,process,Pid,normal} -> ok end,
    undefined=process_info_slice:other({Pid,error_handler}),
    undefined=process_info_slice:other({Pid,[]}),
    badarg(fun() -> process_info_slice:other({Pid,[error_handler|bad_tail]}) end),
    <<"payload">> = process_info_slice:copy(<<"payload">>),
    io:format("OTP_PROCESS_INFO_REFERENCE_OK error_handler=true selector_lists=true dead_pid=true badarg=true~n").
badarg(F) -> try F() of _ -> error(expected_badarg) catch error:badarg -> ok end.
