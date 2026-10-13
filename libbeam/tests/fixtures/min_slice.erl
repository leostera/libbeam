%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(min_slice).
-export([minimum/2, float_first/0, integer_first/0, tuple_refusal/0,
         guard_refusal/0, guarded/2, identity/1, copy/1]).
minimum(A, B) -> erlang:min(A, B).
float_first() -> minimum(1.0, 1).
integer_first() -> minimum(1, 1.0).
tuple_refusal() -> minimum({1}, {2}).
guard_refusal() -> guarded({1}, {2}).
guarded(A, B) when erlang:min(A, B) =:= A -> chosen;
guarded(_, _) -> other.
identity(Input) -> Input.
copy(Input) ->
    case minimum(1.0, 1) of
        1.0 -> Input
    end.
