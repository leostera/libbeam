% SPDX-License-Identifier: Apache-2.0
% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(bif_fun_to_list_1).
-export([entry/1,identity/1,invoke/3,check/3]).
entry(Input) ->
    {F,Mode,V} = erlang:binary_to_term(Input),
    case check(F,Mode,V) of true -> <<"ok">>; _ -> <<"contract mismatch">> end.
identity(Input) -> Input.
invoke(_F,direct,[A0]) -> erlang:'fun_to_list'(A0);
invoke(F,_DynamicMode,Args) -> erlang:apply(erlang,F,Args).
check(F,Mode,_V) ->
    invoke(F, Mode, [fun ?MODULE:identity/1]) =:= "fun bif_fun_to_list_1:identity/1".
