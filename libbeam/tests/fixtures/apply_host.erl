%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(apply_host).
-export([identity/1, list/1, list_call/1, list_last/1, fixed/1, fixed_call/1, external/1, copy/1, fun_list/1, fun_call/1, fun_last/1,
         fun_external/1, fun_dynamic/1, fun_copy/1, fun_bad/1, fun_badarity/1, fun_badargs/1, fun_denied/1]).
identity(Value) -> Value.
list({Module,Function,Args}) -> apply(Module,Function,Args).
list_call({Module,Function,Args}) ->
    Result = apply(Module,Function,Args), {applied,Result}.
list_last(Input) ->
    {Module,Function,Args} = identity(Input), apply(Module,Function,Args).
fixed({Module,Function,Value}) -> Module:Function(Value).
fixed_call({Module,Function,Value}) ->
    Result = Module:Function(Value), {applied,Result}.
external({Module,Function,Args}) ->
    Fun = identity(fun erlang:apply/3), Fun(Module,Function,Args).
copy(Input) -> list({?MODULE,identity,[Input]}).
fun_list({Fun,Args}) -> apply(Fun,Args).
fun_call({Fun,Args}) -> Result=apply(Fun,Args), {applied,Result}.
fun_last(Input) -> {Fun,Args}=identity(Input), apply(Fun,Args).
fun_external({Fun,Args}) -> Apply=identity(fun erlang:apply/2), Apply(Fun,Args).
fun_dynamic({Fun,Args}) -> list({erlang,apply,[Fun,Args]}).
fun_copy(B) ->
    Captured=fun(_) -> true=erlang:garbage_collect(), B end,
    B=fun_list({Captured,[ignored]}),
    true=fun_call({Captured,[ignored]})=:={applied,B},
    B=fun_last({Captured,[ignored]}),
    B=fun_external({Captured,[ignored]}),
    B=fun_dynamic({Captured,[ignored]}),
    42=fun_list({fun erlang:abs/1,[-42]}),
    B=fun_list({fun() -> B end,[]}), B.
fun_denied(B) -> fun_list({fun erlang:display/1,[B]}).
fun_bad(B) -> fun_list({B,[]}).
fun_badarity(B) -> fun_list({fun identity/1,[B,B]}).
fun_badargs(B) -> fun_list({fun identity/1,B}).
