%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(floor_slice).
-export([body/1, guarded/1, kept/1, identity/1, copy/1]).
body(A) -> erlang:floor(A).
guarded(A) when erlang:floor(A) =:= -2 -> yes;
guarded(_) -> no.
kept(A) ->
    identity(dummy),
    X = erlang:floor(A),
    identity(dummy),
    X.
identity(Input) -> Input.
copy(Input) ->
    -2 = body(-1.25),
    yes = guarded(-1.25),
    no = guarded(bad),
    1267650600228229401496703205376 = kept(1267650600228229401496703205376.0),
    Input.
