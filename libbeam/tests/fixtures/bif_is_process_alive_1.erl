% SPDX-License-Identifier: Apache-2.0
% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(bif_is_process_alive_1).
-export([entry/1,identity/1,invoke/3,check/3]).
entry(Input) ->
    {F,Mode,V} = erlang:binary_to_term(Input),
    case check(F,Mode,V) of true -> <<"ok">>; _ -> <<"contract mismatch">> end.
identity(Input) -> Input.
invoke(_F,direct,[A0]) -> erlang:'is_process_alive'(A0);
invoke(F,_DynamicMode,Args) -> erlang:apply(erlang,F,Args).
check(F,Mode,V) ->
    true = invoke(F, Mode, [self()]),
    Dead = spawn(fun() -> ok end),
    Ref = erlang:monitor(process,Dead),
    receive {'DOWN',Ref,process,Dead,_} -> ok after 1000 -> error(child_did_not_exit) end,
    invoke(F, Mode, [Dead]) =:= false.
