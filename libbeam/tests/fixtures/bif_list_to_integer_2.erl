% SPDX-License-Identifier: Apache-2.0
% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(bif_list_to_integer_2).
-export([entry/1,identity/1,invoke/3,check/3]).
entry(Input) ->
    {F,Mode,V} = erlang:binary_to_term(Input),
    case check(F,Mode,V) of true -> <<"ok">>; _ -> <<"contract mismatch">> end.
identity(Input) -> Input.
invoke(_F,direct,[A0,A1]) -> erlang:'list_to_integer'(A0,A1);
invoke(F,_DynamicMode,Args) -> erlang:apply(erlang,F,Args).
check(F,Mode,V) ->
    255 = invoke(F,Mode,["ff",V+9]),
    true = badarg(fun() -> invoke(F,Mode,["fg",16]) end),
    true = badarg(fun() -> invoke(F,Mode,["10",1]) end),
    true.
badarg(Fun) ->
    try Fun() of _ -> false catch error:badarg -> true; _:_ -> false end.
