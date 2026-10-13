%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(div_slice).
-export([divide/2, guarded/2, kept/2, identity/1, copy/1]).
divide(A, B) -> A div B.
guarded(A, B) when A div B =:= 42 -> yes;
guarded(_, _) -> no.
kept(A, B) ->
    identity(dummy),
    X = A div B,
    identity(dummy),
    X.
identity(Input) -> Input.
copy(Input) ->
    case divide(340282366920938463426481119284349108225, 18446744073709551615) of
        18446744073709551615 -> Input
    end.
