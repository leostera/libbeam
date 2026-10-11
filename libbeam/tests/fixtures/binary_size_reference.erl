%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
%% Stock-only sizing/error-order oracle. Never loaded into libbeam.
-module(binary_size_reference).
-export([run/0, utf8/2, utf16/2, utf32/2, width/1, multiply/1,
         late/2, binary/2, floating/2, integer_first/2]).
utf8(U, N) -> <<U/utf8, 0:N>>.
utf16(U, N) -> <<U/utf16, 0:N>>.
utf32(U, N) -> <<U/utf32, 0:N>>.
width(N) -> <<0:N>>.
multiply(N) -> <<0:N/unit:256>>.
late(A, N) -> <<0:A, 0:N>>.
binary(B, N) -> <<0:1, B/bitstring, 0:N>>.
floating(F, N) -> <<F:7/float, 0:N>>.
integer_first(N, U) -> <<0:N, U/utf32>>.
run() ->
    Big = 1 bsl 64,
    Small = (1 bsl 59) - 1,
    Cases = [{utf8, utf8, [-1,-1], -1},
             {utf16, utf16, [-1,-1], -1},
             {utf32, utf32, [-1,-1], -1},
             {positive_big, width, [Big], Big},
             {negative_big, width, [-Big], -Big},
             {noninteger, width, [false], false},
             {multiply, multiply, [Small], Small},
             {late, late, [Small,-1], -1},
             {binary, binary, [false,-1], false},
             {floating, floating, [false,-1], -1},
             {integer_first, integer_first, [-1,-1], -1}],
    lists:foreach(fun({Name, Function, Args, Value}) ->
        try apply(?MODULE, Function, Args) of
            _ -> error(expected_failure)
        catch error:Reason:Stack ->
            [{_,_,_,Extra}|_] = Stack,
            Info = proplists:get_value(error_info, Extra),
            {Segment, Operation, Detail, Value} = maps:get(cause, Info),
            io:format("~w:~w:~w:~w:~w~n", [Name, Reason, Segment, Operation, Detail])
        end
    end, Cases).
