% SPDX-License-Identifier: Apache-2.0
% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(bif_spawn_opt_5).
-export([entry/1,identity/1,invoke/3,check/3,child/1]).
entry(Input) ->
    {F,Mode,V} = erlang:binary_to_term(Input),
    case check(F,Mode,V) of true -> <<"ok">>; _ -> <<"contract mismatch">> end.
identity(Input) -> Input.
invoke(_F,direct,[A0,A1,A2,A3,A4]) -> erlang:'spawn_opt'(A0,A1,A2,A3,A4);
invoke(F,_DynamicMode,Args) -> erlang:apply(erlang,F,Args).
check(F,Mode,V) ->
    {P,R} = invoke(F, Mode, [node(),?MODULE,child,[self()],[monitor]]), receive done -> receive {'DOWN',R,process,P,normal} -> true after 1000 -> false end after 1000 -> false end.
child(Parent) -> Parent ! done.
