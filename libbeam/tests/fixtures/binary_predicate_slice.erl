%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(binary_predicate_slice).
-export([check/1, guarded/1, allocate_guard/1, guard_y/2, other_types/1, identity/1, copy/1]).
check(Value) -> is_binary(Value).
guarded(Value) when is_binary(Value) -> yes;
guarded(_) -> no.
allocate_guard(Value) when is_binary(Value) ->
    case check(Value) of true -> {binary, Value} end;
allocate_guard(_) -> no.
guard_y(Value, Other) ->
    Result = identity(Other),
    case is_binary(Value) of true -> Result; false -> no end.
other_types(_) ->
    {check(self()), check(make_ref()), check(fun identity/1), check(fun erlang:is_binary/1)}.
identity(Input) -> Input.
copy(Input) ->
    case {check(<<>>), check(<<1:7>>), check([]), check(Input),
          guarded(Input), guard_y(Input, yes)} of
        {true, false, false, true, yes, yes} -> Input
    end.
