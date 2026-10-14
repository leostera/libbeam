%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(group_leader_reference).
-export([run/0]).
run() ->
    Original = group_leader(),
    lists:foreach(fun(N) ->
        B = list_to_binary([(I*37) band 255 || I <- lists:seq(0,N-1)]),
        B = group_leader_slice:copy(B),
        B = group_leader_slice:lifecycle(B),
        Original = group_leader()
    end,[0,4,64,65,8192,32768]),
    lists:foreach(fun(F) ->
        try apply(group_leader_slice,F,[<<1>>]) of _ -> error(expected_badarg)
        catch error:badarg -> ok end
    end,[invalid,invalid_target,dead_target]),
    io:format("GROUP_LEADER_REFERENCE_OK~n").
