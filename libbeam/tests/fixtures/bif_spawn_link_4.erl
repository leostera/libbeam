% SPDX-License-Identifier: Apache-2.0
% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(bif_spawn_link_4).
-export([entry/1,identity/1,invoke/3,check/3,child/1]).
entry(Input) ->
    {F,Mode,V} = erlang:binary_to_term(Input),
    case check(F,Mode,V) of true -> <<"ok">>; _ -> <<"contract mismatch">> end.
identity(Input) -> Input.
invoke(_F,direct,[A0,A1,A2,A3]) -> erlang:'spawn_link'(A0,A1,A2,A3);
invoke(F,_DynamicMode,Args) -> erlang:apply(erlang,F,Args).
check(F,Mode,V) ->
    P = invoke(F,Mode,[node(),?MODULE,child,[self()]]),
    true = is_pid(P),
    {links,Links} = erlang:process_info(self(),links),
    true = has_link(P,Links),
    true = unlink(P),
    receive done -> true after 1000 -> false end.
child(Parent) -> Parent ! done.
has_link(P,[P|_]) -> true;
has_link(P,[_|Rest]) -> has_link(P,Rest);
has_link(_,[]) -> false.
