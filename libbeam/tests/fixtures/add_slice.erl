%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(add_slice).
-export([add/2, increment/1, decrement/1, guarded/2, copied/1, big/1, float/1, rounded/1, saved/2, external/2]).
add(A, B) -> A + B.
external(A, B) -> ?MODULE:add(A, B).
saved(A, B) ->
    erlang:get_module_info(?MODULE),
    Sum = A + B,
    erlang:get_module_info(?MODULE),
    Sum.
increment(A) -> A + 1.
decrement(A) -> A + -1.
guarded(A, B) when A + B =:= 42 -> <<"yes">>;
guarded(_, _) -> <<"no">>.
copied(Binary) -> count(0, Binary).
count(42, Binary) -> Binary;
count(Count, Binary) -> count(Count + 1, Binary).
big(_) -> ?MODULE:add(1152921504606846975, 1152921504606846975).
float(_) -> ?MODULE:add(1152921504606847105, 0.0).
rounded(Binary) ->
    case ?MODULE:add(4503599627370496.0, 0.5) of
        4503599627370496.0 -> Binary
    end.
