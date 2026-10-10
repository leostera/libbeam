%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(local_slice).
-compile(no_inline).
-export([identity/1, gc/1, tail/1, pair/1, loop/1]).
-payload({0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
          16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,
          32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,
          48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63}).

identity(Binary) -> leaf(Binary).
leaf(Binary) -> Binary.

%% Binary is a real Y-root across private calls and native copying/GC.
gc(Binary) -> touch(), Binary.
touch() -> erlang:get_module_info(?MODULE).
tail(Binary) -> touch(), leaf(Binary).
pair(Binary) -> {leaf(Binary), leaf(Binary)}.

%% Reductions expire at private local entries as well as exported ones.
loop(Binary) -> again(Binary).
again(Binary) -> loop(Binary).
