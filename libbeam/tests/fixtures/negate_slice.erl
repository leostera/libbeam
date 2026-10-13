%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(negate_slice).
-export([negate/1, guarded/1, kept/1, identity/1, copy/1]).
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
