% SPDX-License-Identifier: Apache-2.0
% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(bif_spawn_request_5).
-export([entry/1,identity/1,invoke/3,check/3,child/1]).
entry(Input) ->
    {F,Mode,V} = erlang:binary_to_term(Input),
    case check(F,Mode,V) of true -> <<"ok">>; _ -> <<"contract mismatch">> end.
identity(Input) -> Input.
invoke(_F,direct,[A0,A1,A2,A3,A4]) -> erlang:'spawn_request'(A0,A1,A2,A3,A4);
invoke(F,_DynamicMode,Args) -> erlang:apply(erlang,F,Args).
check(F,Mode,V) ->
    R = invoke(F, Mode, [node(),?MODULE,child,[self()],[]]), receive {spawn_reply,R,ok,P} -> true = is_pid(P), receive done -> true after 1000 -> false end after 1000 -> false end.
child(Parent) -> Parent ! done.
