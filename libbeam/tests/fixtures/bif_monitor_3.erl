% SPDX-License-Identifier: Apache-2.0
% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(bif_monitor_3).
-export([entry/1,identity/1,invoke/3,check/3]).
entry(Input) ->
    {F,Mode,V} = erlang:binary_to_term(Input),
    case check(F,Mode,V) of true -> <<"ok">>; _ -> <<"contract mismatch">> end.
identity(Input) -> Input.
invoke(_F,direct,[A0,A1,A2]) -> erlang:'monitor'(A0,A1,A2);
invoke(F,_DynamicMode,Args) -> erlang:apply(erlang,F,Args).
check(F,Mode,V) ->
    P = spawn(fun() -> receive stop -> ok end end), R = invoke(F, Mode, [process,P,[]]), P ! stop, receive {'DOWN',R,process,P,normal} -> true after 1000 -> false end.
