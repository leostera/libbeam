%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(decimal_slice).
-export([number/1, sum/2, small/1, negative/1, large/1, too_large/1, slow/1, boundary64/1, boundary65/1]).
number(Value) -> integer_to_binary(Value).
sum(A, B) -> integer_to_binary(A + B).
small(_) -> ?MODULE:number(42).
negative(_) -> ?MODULE:number(-18446744073709551617).
boundary64(_) -> ?MODULE:number(1 bsl 212).
boundary65(_) -> ?MODULE:number(1 bsl 213).
large(_) -> ?MODULE:number(1 bsl 8192).
too_large(_) -> ?MODULE:number(1 bsl 262144).
slow(_) -> ?MODULE:number(1 bsl 4194176).
