% SPDX-License-Identifier: Apache-2.0
% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(bif_fun_info_1).
-export([entry/1,identity/1,invoke/3,check/3]).
entry(Input) ->
    {F,Mode,V} = erlang:binary_to_term(Input),
    case check(F,Mode,V) of true -> <<"ok">>; _ -> <<"contract mismatch">> end.
identity(Input) -> Input.
invoke(_F,direct,[A0]) -> erlang:'fun_info'(A0);
invoke(F,_DynamicMode,Args) -> erlang:apply(erlang,F,Args).
check(F,Mode,V) ->
    I = invoke(F, Mode, [fun ?MODULE:identity/1]), lookup(module,I) =:= ?MODULE andalso lookup(name,I) =:= identity andalso lookup(arity,I) =:= 1.
lookup(K,[{K,V}|_]) -> V; lookup(K,[_|Xs]) -> lookup(K,Xs); lookup(_,[]) -> missing.
