% SPDX-License-Identifier: Apache-2.0
% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(bif_list_to_pid_1).
-export([entry/1,identity/1,invoke/3,check/3]).
entry(Input) ->
    {F,Mode,V} = erlang:binary_to_term(Input),
    case check(F,Mode,V) of true -> <<"ok">>; _ -> <<"contract mismatch">> end.
identity(Input) -> Input.
invoke(_F,direct,[A0]) -> erlang:'list_to_pid'(A0);
invoke(F,_DynamicMode,Args) -> erlang:apply(erlang,F,Args).
check(F,Mode,V) ->
    Pid = invoke(F,Mode,[pid_to_list(self())]),
    true = Pid =:= self(),
    true = badarg(fun() -> invoke(F,Mode,["not-a-pid"]) end),
    true.
badarg(Fun) ->
    try Fun() of _ -> false catch error:badarg -> true; _:_ -> false end.
