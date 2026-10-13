%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(rem_slice).
-export([remainder/2, guarded/2, kept/2, identity/1, copy/1]).
remainder(A, B) -> A rem B.
guarded(A, B) when A rem B =:= 42 -> yes;
guarded(_, _) -> no.
kept(A, B) ->
    identity(dummy),
    X = A rem B,
    identity(dummy),
    X.
identity(Input) -> Input.
copy(Input) ->
    case remainder((1 bsl 192)-1, (1 bsl 64)+1) of
        18446744073709551615 -> Input
    end.
