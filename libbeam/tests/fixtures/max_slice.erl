%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(max_slice).
-export([maximum/2, float_first/0, integer_first/0, tuple_refusal/0, guard_refusal/0, guarded/2]).
maximum(A, B) -> erlang:max(A, B).
float_first() -> maximum(1.0, 1).
integer_first() -> maximum(1, 1.0).
tuple_refusal() -> maximum({1}, {2}).
guard_refusal() -> guarded({1}, {2}).
guarded(A, B) when erlang:max(A, B) =:= A -> chosen;
guarded(_, _) -> other.
