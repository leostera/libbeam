%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(abs_reference).
-export([run/0]).
run() ->
    lists:foreach(fun(N) ->
        Expected = if N < 0 -> -N; true -> N end,
        Expected = abs_slice:magnitude(N)
    end, [-(1 bsl 59),(1 bsl 59)-1,-42,0,42,-340282366920938463426481119284349108225]),
    lists:foreach(fun(Bits) ->
        <<Value:64/float>> = <<Bits:64>>,
        <<Actual:64>> = <<(abs_slice:magnitude(Value)):64/float>>,
        Expected = Bits band 16#7fffffffffffffff,
        Expected = Actual
    end, [0,16#8000000000000000,16#3ff8000000000000,16#bff8000000000000,
          1,16#8000000000000001,16#7fefffffffffffff,16#ffefffffffffffff]),
    yes = abs_slice:guarded(-42),
    lists:foreach(fun(Value) ->
        no = abs_slice:guarded(Value),
        try abs_slice:magnitude(Value) of _ -> error(expected_badarg)
        catch error:badarg -> ok end
    end, [true,[]]),
    42 = abs_slice:kept(-42),
    Payload = binary:copy(<<0,1,255>>, 10000),
    Payload = abs_slice:copy(Payload),
    io:format("OTP_ABS_REFERENCE_OK integers=true float_bits=true signed_zero=true badarg=true guard=true~n").
