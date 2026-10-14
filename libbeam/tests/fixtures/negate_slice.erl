%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(negate_slice).
-export([negate/1, guarded/1, kept/1, identity/1, copy/1,
         positive/1, positive_dynamic/2, positive_copy/1, positive_bad/1, positive_guarded/1, positive_kept/1]).
positive(A) -> +A.
positive_guarded(A) when +A =:= A -> A;
positive_guarded(_) -> no.
positive_dynamic(F,A) -> apply(erlang,F,[A]).
positive_copy(B) ->
    340282366920938463426481119284349108225 = positive_dynamic('+',340282366920938463426481119284349108225),
    B = identity(B), true = erlang:garbage_collect(), B.
positive_bad(B) -> positive(B).
positive_kept(A) -> R = positive_dynamic('+',A), true = erlang:garbage_collect(), R.
negate(A) -> -A.
guarded(A) when -A =:= 42 -> yes;
guarded(_) -> no.
kept(A) ->
    identity(dummy),
    X = -A,
    identity(dummy),
    X.
identity(Input) -> Input.
copy(Input) ->
    case negate(340282366920938463426481119284349108225) of
        -340282366920938463426481119284349108225 -> Input
    end.
