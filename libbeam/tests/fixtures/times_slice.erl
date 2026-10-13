%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(times_slice).
-export([product/2, guarded/2, small/0, wide/0, large/0, negative/0,
         floating/0, float_overflow/0, huge_float/0, bad_number/0,
         guard_yes/0, guard_bad/0, kept/2, identity/1, copy/1]).
product(A, B) -> A * B.
guarded(A, B) when A * B =:= 42 -> yes;
guarded(_, _) -> no.
small() -> product(6, 7).
wide() -> product(18446744073709551615, 18446744073709551615).
large() -> product((1 bsl 193)-1, (1 bsl 129)+1).
negative() -> product(-18446744073709551615, 18446744073709551615).
floating() -> product(1.5, 4).
float_overflow() -> product(1.0e308, 2.0).
huge_float() -> product(1 bsl 1100, 0.0).
bad_number() -> product(not_a_number, 0).
guard_yes() -> guarded(6, 7).
guard_bad() -> guarded(not_a_number, 3).
kept(A, B) ->
    identity(dummy),
    X = A * B,
    identity(dummy),
    X.
identity(Input) -> Input.
copy(Input) ->
    case wide() of
        340282366920938463426481119284349108225 -> Input
    end.
