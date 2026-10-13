%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(raise_slice).
-export([raised_error/1, raised_exit/1, raised_throw/1, plain_throw/1,
         invalid_class/1, invalid_trace/1, fun_frame/1, identity/1, monitored/1]).
raised_error(Value) -> erlang:raise(error, Value, [{?MODULE, origin, 1}]).
raised_exit(Value) -> erlang:raise(exit, Value, [{?MODULE, origin, 1}]).
raised_throw(Value) -> erlang:raise(throw, Value, [{?MODULE, origin, 1}]).
plain_throw(Value) -> throw(Value).
invalid_class(Value) -> erlang:raise(invalid, Value, []).
invalid_trace(Value) -> erlang:raise(error, Value, [invalid]).
fun_frame(Value) -> erlang:raise(error, Value, [{fun erlang:self/0, []}]).
identity(Value) -> Value.
monitored(Value) ->
    {Pid, Ref} = spawn_monitor(fun() -> raised_error(Value) end),
    receive
        {'DOWN', Ref, process, Pid, {Value, [{?MODULE, origin, 1, []}]}} -> Value
    end.
