% SPDX-License-Identifier: Apache-2.0
% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(bif_system_flag_2).
-export([entry/1,identity/1,invoke/3,check/3]).
entry(_Input) ->
    _ = erlang:'system_flag'(backtrace_depth,42),
    <<"unexpected success">>.
identity(Input) -> Input.
invoke(_F,direct,[A0,A1]) -> erlang:'system_flag'(A0,A1);
invoke(F,_DynamicMode,Args) -> erlang:apply(erlang,F,Args).
check(F,Mode,V) ->
    Old = invoke(F, Mode, [backtrace_depth,V]), V = invoke(F, Mode, [backtrace_depth,Old]), is_integer(Old).
