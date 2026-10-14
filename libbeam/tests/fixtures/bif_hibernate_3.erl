% SPDX-License-Identifier: Apache-2.0
% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(bif_hibernate_3).
-export([entry/1,identity/1,invoke/3,check/3,resume/1]).
entry(Input) ->
    {F,Mode,V} = erlang:binary_to_term(Input),
    case check(F,Mode,V) of true -> <<"ok">>; _ -> <<"contract mismatch">> end.
identity(Input) -> Input.
invoke(_F,direct,[A0,A1,A2]) -> erlang:'hibernate'(A0,A1,A2);
invoke(F,_DynamicMode,Args) -> erlang:apply(erlang,F,Args).
check(F,Mode,V) ->
    self() ! {wake,V}, invoke(F, Mode, [?MODULE,resume,[V]]), false.
resume(V) -> receive {wake,V} -> <<"ok">> after 1000 -> <<"timeout">> end.
