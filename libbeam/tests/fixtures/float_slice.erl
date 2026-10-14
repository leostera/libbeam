%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(float_slice).
-export([body/1, guarded/1, kept/1, dynamic/2, identity/1]).
body(A) -> erlang:float(A).
guarded(A) when erlang:float(A) =:= 0.0 -> yes;
guarded(_) -> no.
kept(A) -> identity(dummy), R=erlang:float(A), identity(dummy), R.
dynamic(Function, A) -> apply(erlang, Function, [A]).
identity(A) -> A.
