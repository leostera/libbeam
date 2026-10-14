%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(binary_list_reference).
-export([run/0]).
bad(Args) ->
    try apply(erlang, binary_to_list, Args) of
        _ -> error(expected_badarg)
    catch error:badarg -> ok end.
run() ->
    lists:foreach(fun(N) ->
        Expected = [(I*173+19) band 255 || I <- lists:seq(0,N-1)],
        Bin = list_to_binary(Expected),
        Expected = binary_to_list(Bin)
    end, [0,1,63,64,65,257]),
    Data = list_to_binary([(I*173+19) band 255 || I <- lists:seq(0,256)]),
    lists:foreach(fun(Offset) ->
        <<_:Offset, View:64/binary, _/bitstring>> = Data,
        Expected = [(((binary:at(Data,I) bsl Offset) bor
                       (binary:at(Data,I+1) bsr (8-Offset))) band 255)
                    || I <- lists:seq(0,63)],
        Expected = binary_to_list(View),
        Middle = lists:sublist(Expected,2,62),
        Middle = binary_to_list(View,2,63),
        Last = lists:last(Expected),
        [Last] = binary_to_list(View,64,64),
        lists:foreach(fun({First,Stop}) -> bad([View,First,Stop]) end,
                      [{0,1},{1,0},{-1,2},{2,1},{1,65},{1,1 bsl 64},{false,1},{1,1.0}]),
        <<_:Offset, Bits:511/bitstring, _/bitstring>> = Data,
        bad([Bits]), bad([Bits,1,1])
    end, lists:seq(0,7)),
    bad([<<>>,1,1]), bad([true]), bad([[]]),
    io:format("BINARY_LIST_REFERENCE_OK~n").
