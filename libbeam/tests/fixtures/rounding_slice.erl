%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(rounding_slice).
-export([body/1, guarded/1, kept/1, dynamic/2, identity/1]).
body(A) -> erlang:?OP(A).
guarded(A) when erlang:?OP(A) =:= -2 -> yes;
guarded(_) -> no.
kept(A) -> identity(dummy), X=erlang:?OP(A), identity(dummy), X.
dynamic(Function, A) -> apply(erlang, Function, [A]).
identity(A) -> A.
