%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
%% Stock-only oracle; never admitted into an owned code space.
-module(writable_init_reference).
-export([run/0]).
run() ->
    Values = [{zero,0},{one,1},{negative,-1},{capacity,1024},
              {atom,false},{nil,[]},{big,1 bsl 64},{binary,<<1,2>>}],
    lists:foreach(fun({Name,Value}) ->
        Result = writable_init_slice:init(Value),
        io:format("~s:~B~n", [Name,byte_size(Result)])
    end, Values),
    Saved = writable_init_slice:saved(binary:copy(<<107>>,65)),
    io:format("saved:~B:~B~n", [byte_size(Saved),binary:first(Saved)]).
