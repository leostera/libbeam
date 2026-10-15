% SPDX-License-Identifier: Apache-2.0
% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(bif_read_timer_1).
-export([entry/1,identity/1,invoke/3,check/3]).
entry(Input) ->
    {F,Mode,V} = erlang:binary_to_term(Input),
    case check(F,Mode,V) of true -> <<"ok">>; _ -> <<"contract mismatch">> end.
identity(Input) -> Input.
invoke(_F,direct,[A0]) -> erlang:'read_timer'(A0);
invoke(F,_DynamicMode,Args) -> erlang:apply(erlang,F,Args).
check(F,Mode,V) ->
    R = erlang:send_after(60000,self(),V), T = invoke(F, Mode, [R]), erlang:cancel_timer(R),
    case T of N when is_integer(N) -> true; _ -> false end.
