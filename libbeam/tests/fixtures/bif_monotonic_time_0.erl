% SPDX-License-Identifier: Apache-2.0
% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(bif_monotonic_time_0).
-export([entry/1,identity/1,invoke/3,check/3]).
entry(Input) ->
    {F,Mode,V} = erlang:binary_to_term(Input),
    case check(F,Mode,V) of true -> <<"ok">>; _ -> <<"contract mismatch">> end.
identity(Input) -> Input.
invoke(_F,direct,[]) -> erlang:'monotonic_time'();
invoke(F,_DynamicMode,Args) -> erlang:apply(erlang,F,Args).
check(F,Mode,_V) ->
    Time = invoke(F,Mode,[]),
    case Time of N when is_integer(N) -> true; _ -> false end.
