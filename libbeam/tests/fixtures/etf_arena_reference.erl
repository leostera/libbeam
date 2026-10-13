%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(etf_arena_reference).
-export([run/0]).
run() ->
    Data = list_to_binary(lists:seq(0,99)),
    Encoded = <<131,104,4,107,0,2,97,98,110,9,0,0,0,0,0,0,0,0,0,1,
                70,128,0,0,0,0,0,0,0,109,0,0,0,100,Data/binary>>,
    {[97,98],Big,Float,Data} = binary_to_term(<<Encoded/binary,255>>),
    Big = 1 bsl 64,
    <<16#8000000000000000:64>> = <<Float:64/float>>,
    {_,Used} = binary_to_term(<<Encoded/binary,255>>,[used]),
    Used = byte_size(Encoded),
    lists:foreach(fun(N) ->
        try binary_to_term(binary:part(Encoded,0,N)) of _ -> error(expected_badarg)
        catch error:badarg -> ok end
    end,lists:seq(0,Used-1)),
    Pairs = << <<97,I,97,(I+1)>> || I <- lists:seq(0,32) >>,
    Map = binary_to_term(<<131,116,33:32,Pairs/binary>>),
    33 = map_size(Map), 33 = map_get(32,Map),
    io:format("OTP_ETF_ARENA_REFERENCE_OK scalar_composite=true signed_zero=true offheap_bytes=true trailing_consumed=true truncated_badarg=true map=true~n").
