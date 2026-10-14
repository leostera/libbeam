%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(process_flag_reference).
-export([run/0]).
run() ->
    process_flag(trap_exit,false),
    lists:foreach(fun(N) ->
        B = list_to_binary([(I*37) band 255 || I <- lists:seq(0,N-1)]),
        B = process_flag_slice:copy(B),
        B = process_flag_slice:lifecycle(B),
        {trap_exit,false} = process_info(self(),trap_exit)
    end,[0,4,64,65,8192,32768]),
    lists:foreach(fun(F) ->
        try apply(process_flag_slice,F,[<<1>>]) of _ -> error(expected_badarg)
        catch error:badarg -> ok end
    end,[bad_value,bad_flag]),
    io:format("PROCESS_FLAG_REFERENCE_OK~n").
