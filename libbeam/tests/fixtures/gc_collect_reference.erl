%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(gc_collect_reference).
-export([run/0]).
run() ->
    lists:foreach(fun(N) ->
        B = list_to_binary([(I*37) band 255 || I <- lists:seq(0,N-1)]),
        B = gc_collect_slice:copy(B)
    end,[0,4,64,65,8192,32768]),
    io:format("GC_COLLECT_REFERENCE_OK~n").
