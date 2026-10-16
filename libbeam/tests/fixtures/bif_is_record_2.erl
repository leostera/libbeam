% SPDX-License-Identifier: Apache-2.0
% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(bif_is_record_2).
-export([entry/1,identity/1,invoke/3,check/3]).
entry(Input) ->
    {F,Mode,V} = erlang:binary_to_term(Input),
    case check(F,Mode,V) of true -> <<"ok">>; _ -> <<"contract mismatch">> end.
identity(Input) -> Input.
invoke(_F,direct,[Term,Tag,_]) -> erlang:is_record(Term,Tag);
invoke(F,_DynamicMode,[Term,Tag,_]) -> erlang:apply(erlang,F,[Term,Tag]).
check(F,Mode,Seed) ->
    true = Seed =:= 7,
    true = invoke(F,Mode,[{foo,1},foo,unused]),
    true = invoke(F,Mode,[{foo},foo,unused]),
    false = invoke(F,Mode,[{bar,1},foo,unused]),
    false = invoke(F,Mode,[{},foo,unused]),
    false = invoke(F,Mode,[not_a_tuple,foo,unused]),
    true = badarg(fun() -> invoke(F,Mode,[{foo,1},1,unused]) end),
    true.
badarg(Fun) ->
    try Fun() of _ -> false catch error:badarg -> true; _:_ -> false end.
