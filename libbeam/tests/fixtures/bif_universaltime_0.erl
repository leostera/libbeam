% SPDX-License-Identifier: Apache-2.0
% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(bif_universaltime_0).
-export([entry/1,identity/1,invoke/3,check/3]).
entry(Input) ->
    {F,Mode,V} = erlang:binary_to_term(Input),
    case check(F,Mode,V) of true -> <<"ok">>; _ -> <<"contract mismatch">> end.
identity(Input) -> Input.
invoke(_F,direct,[]) -> erlang:'universaltime'();
invoke(F,_DynamicMode,Args) -> erlang:apply(erlang,F,Args).
check(F,Mode,V) ->
    case invoke(F, Mode, []) of {{Y,M,D},{H,N,S}} -> is_integer(Y) andalso M >= 1 andalso M =< 12 andalso D >= 1 andalso D =< 31 andalso H >= 0 andalso H < 24 andalso N >= 0 andalso N < 60 andalso S >= 0 andalso S =< 60; _ -> false end.
