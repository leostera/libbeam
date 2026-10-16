% SPDX-License-Identifier: Apache-2.0
% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(bif_is_record_3).
-export([entry/1,identity/1,invoke/3,check/3]).
entry(Input) ->
    {F,Mode,V} = erlang:binary_to_term(Input),
    case check(F,Mode,V) of true -> <<"ok">>; _ -> <<"contract mismatch">> end.
identity(Input) -> Input.
invoke(_F,direct,[Term,Tag,Size]) -> erlang:is_record(Term,Tag,Size);
invoke(F,_DynamicMode,Args) -> erlang:apply(erlang,F,Args).
check(F,Mode,Seed) ->
    true = Seed =:= 7,
    true = invoke(F,Mode,[{foo,1},foo,2]),
    true = invoke(F,Mode,[{foo},foo,1]),
    false = invoke(F,Mode,[{foo,1},foo,3]),
    false = invoke(F,Mode,[{bar,1},foo,2]),
    false = invoke(F,Mode,[not_a_tuple,foo,2]),
    false = invoke(F,Mode,[{},foo,0]),
    false = invoke(F,Mode,[{foo,1},some_module,foo]),
    false = invoke(F,Mode,[{foo,1},1,2]),
    true = badarg(fun() -> invoke(F,Mode,[{foo,1},foo,1.5]) end),
    true = badarg(fun() -> invoke(F,Mode,[{foo,1},foo,18446744073709551616]) end),
    true.
badarg(Fun) ->
    try Fun() of _ -> false catch error:badarg -> true; _:_ -> false end.
