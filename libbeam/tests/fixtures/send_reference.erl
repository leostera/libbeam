%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(send_reference).
-export([run/0]).
run() ->
    lists:foreach(fun(N) ->
        B = list_to_binary([(I*173+19) band 255 || I <- lists:seq(0,N-1)]),
        lists:foreach(fun(F) -> B = apply(send_slice,F,[B]) end, [copy,named,dead])
    end, [0,65,8192]),
    lists:foreach(fun(F) ->
        try apply(send_slice,F,[term_to_binary(7)]) of
            _ -> error(expected_badarg)
        catch error:badarg -> ok end
    end, [bad_destination,missing]),
    io:format("SEND_REFERENCE_OK~n").
