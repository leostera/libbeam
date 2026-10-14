%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(hash_reference).
-export([run/0]).
run() ->
    Pids = [list_to_pid(S) || S <- ["<0.0.0>","<0.1.0>","<0.32767.0>","<0.32768.0>","<0.268435455.0>","<0.3.1>"]],
    Refs = [binary_to_term(<<131,90,3:16,119,13,"nonode@nohost",0:32,
                            (N band 16#3ffff):32,((N band 16#fffc0000) bor 1):32,(N bsr 32):32>>)
            || N <- [0,1,262143,262144,1 bsl 32]],
    lists:foreach(fun(T) -> io:put_chars(hash_slice:row(T)) end,Pids++Refs),
    io:put_chars(hash_slice:base(<<>>)),
    lists:foreach(fun(N) -> io:put_chars(hash_slice:all(integer_to_binary(N))) end,
                  [0,1,3,4,7,8,11,12,13,23,24,25,63,64,65,257]).
