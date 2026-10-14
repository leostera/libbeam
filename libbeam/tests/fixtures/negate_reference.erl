%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(negate_reference).
-export([run/0]).
run() ->
    lists:foreach(fun({N,Expected}) ->
        Expected = negate_slice:negate(N), N = negate_slice:positive(N),
        N = negate_slice:positive_dynamic('+',N), N = positive_guard_slice:positive(N), N = negate_slice:positive_guarded(N), N = negate_slice:positive_kept(N)
    end,
        [{-(1 bsl 59),1 bsl 59},{1 bsl 59,-(1 bsl 59)},
         {(1 bsl 59)-1,1-(1 bsl 59)},{-42,42},{0,0},{42,-42},
         {340282366920938463426481119284349108225,-340282366920938463426481119284349108225},
         {-340282366920938463426481119284349108225,340282366920938463426481119284349108225}]),
    lists:foreach(fun(Bits) ->
        <<Value:64/float>> = <<Bits:64>>,
        <<Actual:64>> = <<(negate_slice:negate(Value)):64/float>>,
        Expected = Bits bxor 16#8000000000000000,
        Expected = Actual,
        <<Bits:64>> = <<(negate_slice:positive(Value)):64/float>>,
        <<Bits:64>> = <<(negate_slice:positive_dynamic('+',Value)):64/float>>,
        <<Bits:64>> = <<(positive_guard_slice:positive(Value)):64/float>>,
        <<Bits:64>> = <<(negate_slice:positive_guarded(Value)):64/float>>,
        <<Bits:64>> = <<(negate_slice:positive_kept(Value)):64/float>>
    end, [0,16#8000000000000000,16#3ff8000000000000,16#bff8000000000000,
          1,16#8000000000000001,16#7fefffffffffffff,16#ffefffffffffffff]),
    yes = negate_slice:guarded(-42),
    lists:foreach(fun(Value) ->
        no = negate_slice:guarded(Value),
        try negate_slice:negate(Value) of _ -> error(expected_badarith)
        catch error:badarith -> ok end
    end, [true,[]]),
    lists:foreach(fun(Value) ->
        no = positive_guard_slice:positive(Value), no = negate_slice:positive_guarded(Value),
        try negate_slice:positive(Value) of _ -> error(expected_badarith)
        catch error:badarith -> ok end,
        try negate_slice:positive_dynamic('+',Value) of _ -> error(expected_badarith)
        catch error:badarith -> ok end
    end,[true,[],{1},[1],<<"1">>]),
    42 = negate_slice:kept(-42),
    Payload = binary:copy(<<0,1,255>>, 10000),
    Payload = negate_slice:copy(Payload),
    Payload = negate_slice:positive_copy(Payload),
    io:format("OTP_NEGATE_REFERENCE_OK integers=true float_bits=true signed_zero=true badarith=true guard=true~n").
