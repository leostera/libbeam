%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(list_predicate_slice).
-export([check/1, guarded/1, allocate_guard/1, guard_y/2, other_types/1, identity/1, copy/1]).
check(Value) -> is_list(Value).
guarded(Value) when is_list(Value) -> yes;
guarded(_) -> no.
allocate_guard(Value) when is_list(Value) ->
    case check(Value) of true -> {list, Value} end;
allocate_guard(_) -> no.
guard_y(Value, Other) ->
    Result = identity(Other),
    case is_list(Value) of true -> Result; false -> no end.
other_types(_) ->
    {check(self()), check(make_ref()), check(fun identity/1), check(fun erlang:is_list/1)}.
identity(Input) -> Input.
copy(Input) ->
    case {check([]), check([1]), check([1 | done]), check(1), check(Input),
          guarded([]), guard_y([1], yes)} of
        {true, true, true, false, false, yes, yes} -> Input
    end.
