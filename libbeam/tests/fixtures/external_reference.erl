%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(external_reference).
-export([run/0]).
run() ->
    Data = <<131,104,2,119,2,"ok",109,0,0,0,3,1,2,3,255>>,
    {ok,<<1,2,3>>} = external_slice:decode(Data),
    <<_:3,Unaligned:(byte_size(Data))/binary,_:5>> = <<0:3,Data/binary,0:5>>,
    {ok,<<1,2,3>>} = external_slice:decode(Unaligned),
    {decode_fresh_atom,42} = external_slice:decode(<<131,104,2,119,17,"decode_fresh_atom",97,42>>),
    lists:foreach(fun(N) -> badarg(binary:part(Data,0,N)) end,lists:seq(0,byte_size(Data)-2)),
    lists:foreach(fun badarg/1,[42,ok,[],<<1:1>>,<<131,255>>,<<131,116,0,0,0,2,97,1,97,2,97,1,97,3>>]),
    Map = maps:from_list([{N,N+1} || N <- lists:seq(0,32)]),
    Encoded = <<131,116,33:32,(iolist_to_binary([<<97,N,97,(N+1)>> || N <- lists:seq(0,32)]))/binary>>,
    Map = external_slice:decode(Encoded),
    <<1,2,3>> = external_slice:copy(<<1,2,3>>),
    io:format("OTP_EXTERNAL_REFERENCE_OK data=true atoms=true maps=true trailing=true unaligned=true malformed=true~n").
badarg(Input) ->
    try external_slice:decode(Input) of Value -> erlang:error({expected_badarg,Input,Value})
    catch error:badarg -> ok end.
