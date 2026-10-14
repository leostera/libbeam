%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(bit_size_reference).
-export([run/0]).
run() ->
    Backing=binary:copy(<<149>>,1025),
    lists:foreach(fun(Offset) -> lists:foreach(fun(Size) ->
        <<_:Offset,Part:Size/bitstring,_/bitstring>>=Backing,
        Size=bit_size_slice:direct(Part), Size=bit_size_slice:dynamic(Part),
        Size=bit_size_slice:guarded(Part), Size=bit_size_guard_slice:direct(Part),
        {Size,Part}=bit_size_slice:kept(Part)
    end,[0,1,7,8,9,15,16,17,503,504,505,511,512,513,519,520,521,8192]) end,lists:seq(0,7)),
    lists:foreach(fun(Value) ->
        no=bit_size_slice:guarded(Value), no=bit_size_guard_slice:direct(Value),
        try bit_size_slice:direct(Value) of _ -> error(expected_badarg)
        catch error:badarg -> ok end,
        try bit_size_slice:dynamic(Value) of _ -> error(expected_badarg)
        catch error:badarg -> ok end
    end,[[],true,0,1.0,{0},[0],1 bsl 64]),
    Large=binary:copy(<<0>>,64*1024*1024), 536870912=bit_size_slice:direct(Large),
    <<"payload">>=bit_size_slice:copy(<<"payload">>),
    io:format("OTP_BIT_SIZE_REFERENCE_OK offsets=8 partial_bits=true body_dynamic_guards=true moving_roots=true badarg=true ceiling=true~n").
