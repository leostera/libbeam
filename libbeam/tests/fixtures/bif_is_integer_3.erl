% SPDX-License-Identifier: Apache-2.0
% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(bif_is_integer_3).
-export([entry/1,identity/1,invoke/3,check/3]).
entry(Input) ->
    {F,Mode,V} = erlang:binary_to_term(Input),
    case check(F,Mode,V) of true -> <<"ok">>; _ -> <<"contract mismatch">> end.
identity(Input) -> Input.
invoke(_F,direct,[Value,Lower,Upper]) -> erlang:is_integer(Value,Lower,Upper);
invoke(F,_DynamicMode,Args) -> erlang:apply(erlang,F,Args).
check(F,Mode,Seed) ->
    Huge = 18446744073709551616,
    true = invoke(F,Mode,[Seed,Seed,Seed]),
    true = invoke(F,Mode,[0,0,255]),
    true = invoke(F,Mode,[255,0,255]),
    false = invoke(F,Mode,[-1,0,255]),
    false = invoke(F,Mode,[256,0,255]),
    false = invoke(F,Mode,[not_an_integer,0,255]),
    false = invoke(F,Mode,[1,2,1]),
    true = invoke(F,Mode,[Huge,Huge,Huge]),
    true = invoke(F,Mode,[-Huge,-Huge,Huge]),
    false = invoke(F,Mode,[Huge,Huge+1,Huge+2]),
    true = badarg(fun() -> invoke(F,Mode,[not_an_integer,not_an_integer,2]) end),
    true = badarg(fun() -> invoke(F,Mode,[not_an_integer,0,not_an_integer]) end),
    true.
badarg(Fun) ->
    try Fun() of _ -> false catch error:badarg -> true; _:_ -> false end.
