% SPDX-License-Identifier: Apache-2.0
% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(bif_system_info_1).
-export([entry/1,identity/1,invoke/3,check/3]).
entry(Input) ->
    {F,Mode,V} = erlang:binary_to_term(Input),
    case check(F,Mode,V) of true -> <<"ok">>; _ -> <<"contract mismatch">> end.
identity(Input) -> Input.
invoke(_F,direct,[A0]) -> erlang:'system_info'(A0);
invoke(F,_DynamicMode,Args) -> erlang:apply(erlang,F,Args).
check(F,Mode,_V) ->
    [invoke(F,Mode,[wordsize]),invoke(F,Mode,[{wordsize,internal}]),
     invoke(F,Mode,[{wordsize,external}]),invoke(F,Mode,[threads]),
     invoke(F,Mode,[schedulers]),invoke(F,Mode,[schedulers_online]),
     invoke(F,Mode,[dirty_cpu_schedulers]),invoke(F,Mode,[dirty_cpu_schedulers_online]),
     invoke(F,Mode,[dirty_io_schedulers]),invoke(F,Mode,[thread_pool_size])] =:=
    [8,8,8,true,1,1,0,0,0,0].
