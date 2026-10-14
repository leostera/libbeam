%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(iolist_size_reference).
-export([run/0]).
run() ->
    lists:foreach(fun(N) ->
        B = binary:copy(<<201>>,N),
        B = iolist_size_slice:cases(B),
        lists:foreach(fun(F) ->
            try apply(iolist_size_slice,F,[B]) of _ -> error(expected_badarg)
            catch error:badarg -> ok end
        end,[bad_head,bad_negative,bad_tail,bad_bits])
    end,[0,1,64,65,8192]),
    io:format("IOLIST_SIZE_REFERENCE_OK~n").
