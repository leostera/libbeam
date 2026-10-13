%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(apply_host).
-export([identity/1, list/1, list_call/1, list_last/1, fixed/1, fixed_call/1, external/1, copy/1]).
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
