%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(integer_predicate_slice).
-export([check/1, guarded/1, allocate_guard/1, guard_y/2, other_types/1, identity/1, copy/1]).
check(Value) -> is_integer(Value).
guarded(Value) when is_integer(Value) -> yes;
guarded(_) -> no.
allocate_guard(Value) when is_integer(Value) ->
    case check(Value) of true -> {integer, Value} end;
allocate_guard(_) -> no.
guard_y(Value, Other) ->
    Result = identity(Other),
    case is_integer(Value) of true -> Result; false -> no end.
other_types(_) ->
    {check(self()), check(make_ref()), check(fun identity/1), check(fun erlang:is_integer/1)}.
identity(Input) -> Input.
copy(Input) ->
    case {check(0), check(18446744073709551616), check(-18446744073709551616),
          check(1.0), check(Input), guarded(1), guard_y(1, yes)} of
        {true, true, true, false, false, yes, yes} -> Input
    end.
