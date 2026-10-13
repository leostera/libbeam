%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(floor_reference).
-export([run/0]).
expected(Bits) ->
    Sign = Bits bsr 63,
    Exponent = (Bits bsr 52) band 2047,
    Fraction = Bits band 16#fffffffffffff,
    Mantissa = case Exponent of 0 -> Fraction; _ -> Fraction bor (1 bsl 52) end,
    E = Exponent - 1023,
    Magnitude = if
        E < 0 -> case {Sign,Mantissa} of {1,M} when M /= 0 -> 1; _ -> 0 end;
        E < 52 ->
            Shift = 52-E,
            Extra = case Sign =:= 1 andalso Mantissa band ((1 bsl Shift)-1) /= 0 of true -> 1; false -> 0 end,
            (Mantissa bsr Shift) + Extra;
        true -> Mantissa bsl (E-52)
    end,
    case Sign of 0 -> Magnitude; 1 -> -Magnitude end.
run() ->
    Boundaries = [0,16#8000000000000000,1,16#8000000000000001,
        16#3ff4000000000000,16#bff4000000000000,16#3fefffffffffffff,16#bfefffffffffffff,
        16#439fffffffffffff,16#43a0000000000000,16#c39fffffffffffff,16#c3a0000000000000,
        16#c3a0000000000001,16#43f0000000000000,16#4630000000000000,
        16#7fefffffffffffff,16#ffefffffffffffff],
    Exponents = [((S bsl 63) bor (E bsl 52) bor M) || S <- [0,1],
        E <- lists:seq(0,2046,17), M <- [0,1,16#fffffffffffff]],
    lists:foreach(fun(Bits) ->
        <<F:64/float>> = <<Bits:64>>,
        Want = expected(Bits),
        lists:foreach(fun(Function) ->
            Actual = Function(F),
            true = integer_to_binary(Actual) =:= integer_to_binary(Want),
            case Bits of
                16#c3a0000000000000 -> false = Actual =:= Want, 2 = erts_debug:flat_size(Actual);
                _ -> Want = Actual
            end,
            Actual = floor_slice:body(Actual)
        end,[fun floor_slice:body/1, fun floor_slice:kept/1])
    end,Boundaries ++ Exponents),
    lists:foreach(fun(Value) ->
        no = floor_slice:guarded(Value),
        try floor_slice:body(Value) of Other -> erlang:error({expected_badarg,Other})
        catch error:badarg -> ok end
    end,[ok,[],{},<<1>>,fun() -> ok end]),
    yes = floor_slice:guarded(-1.25),
    <<1,2,3>> = floor_slice:copy(<<1,2,3>>),
    io:format("OTP_FLOOR_REFERENCE_OK vectors=~p body_guards_y=true native_min_boundary=true integers_unchanged=true~n",[length(Boundaries)+length(Exponents)]).
