%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(apply_target).
-export([zero/0, one/1, pair/2]).
zero() -> zero.
one(Value) -> Value.
pair(A,B) -> {A,B}.
