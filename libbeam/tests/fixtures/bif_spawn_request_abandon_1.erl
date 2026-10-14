% SPDX-License-Identifier: Apache-2.0
% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(bif_spawn_request_abandon_1).
-export([entry/1,identity/1,invoke/3,check/3,child/1]).
entry(Input) ->
    {F,Mode,V} = erlang:binary_to_term(Input),
    case check(F,Mode,V) of true -> <<"ok">>; _ -> <<"contract mismatch">> end.
identity(Input) -> Input.
invoke(_F,direct,[A0]) -> erlang:'spawn_request_abandon'(A0);
invoke(F,_DynamicMode,Args) -> erlang:apply(erlang,F,Args).
check(F,Mode,V) ->
    R = erlang:spawn_request(node(),?MODULE,child,[self()],[]), case invoke(F, Mode, [R]) of true -> receive {spawn_reply,R,_,_} -> false after 0 -> true end; false -> receive {spawn_reply,R,ok,P} -> is_pid(P) after 1000 -> false end end.
child(Parent) -> Parent ! done.
