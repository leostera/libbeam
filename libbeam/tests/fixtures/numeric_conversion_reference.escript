#!/usr/bin/env escript
%%! +S 1:1 +SDcpu 1:1 +SDio 1 +A 0
%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
main([Vectors]) ->
    {ok,Data}=file:read_file(Vectors),
    Rows=[L || L <- binary:split(Data,<<"\n">>,[global]), L=/=<<>>],
    %% Installed OTP is versioned evidence, not the pinned big.c implementation.
    %% Characterize discrepancies instead of silently blessing them as parity.
    Differences=lists:filtermap(fun(Line) ->
        [Hex,Expected]=binary:split(Line,<<" ">>), N=binary_to_integer(Hex,16),
        Actual=try erlang:float(N) of F -> <<Bits:64>> = <<F:64/float>>, Bits
               catch error:badarg -> badarg end,
        Want=case Expected of <<"badarg">> -> badarg; _ -> binary_to_integer(Expected,16) end,
        case Actual=:=Want of true -> false; false -> {true,{Hex,Want,Actual}} end
    end,Rows),
    case Differences of [] -> ok; [First|_] -> io:format("INSTALLED_FLOAT_DIFFERENCE_FIRST ~p~n",[First]) end,
    Boundaries=[0,16#8000000000000000,1,16#8000000000000001,
        16#3fdfffffffffffff,16#3fe0000000000000,16#3fe0000000000001,
        16#bfdfffffffffffff,16#bfe0000000000000,16#bfe0000000000001,
        16#3ff8000000000000,16#bff8000000000000,16#3ff4000000000000,16#bff4000000000000,
        16#3fefffffffffffff,16#bfefffffffffffff,16#439fffffffffffff,16#43a0000000000000,
        16#c39fffffffffffff,16#c3a0000000000000,16#c3a0000000000001,16#43f0000000000000,
        16#4630000000000000,16#7fefffffffffffff,16#ffefffffffffffff],
    Sweep=[(S bsl 63) bor (E bsl 52) bor M || S<-[0,1],E<-lists:seq(0,2046,17),M<-[0,1,16#fffffffffffff]],
    lists:foreach(fun(Op) -> lists:foreach(fun(Bits) ->
        <<F:64/float>> = <<Bits:64>>, Want=expected(Op,Bits), Actual=apply(erlang,Op,[F]),
        true=integer_to_binary(Actual)=:=integer_to_binary(Want),
        case Bits of 16#c3a0000000000000 -> false=Actual=:=Want,2=erts_debug:flat_size(Actual); _ -> Want=Actual end,
        Actual=apply(erlang,Op,[Actual])
    end,Boundaries++Sweep) end,[floor,ceil,trunc,round]),
    io:format("NUMERIC_REFERENCE_CHARACTERIZATION otp=~s erts=~s float_vectors=~B float_differences=~B rounding_vectors_per_mode=~B rounding_modes_passed=4~n",
        [erlang:system_info(otp_release),erlang:system_info(version),length(Rows),length(Differences),length(Boundaries)+length(Sweep)]).
expected(Op,Bits) ->
    Sign=Bits bsr 63, Exponent=(Bits bsr 52) band 2047, Fraction=Bits band 16#fffffffffffff,
    Mantissa=case Exponent of 0->Fraction;_->Fraction bor (1 bsl 52) end, E=Exponent-1023,
    Magnitude=if
        E<0 ->
            case {Op,Sign,Mantissa,E} of
                {round,_,_,-1}->1; {floor,1,M,_} when M=/=0->1; {ceil,0,M,_} when M=/=0->1; _->0
            end;
        E<52 ->
            Shift=52-E, Rem=Mantissa band ((1 bsl Shift)-1),
            Extra=case {Op,Sign,Rem} of
                {round,_,R} when R >= (1 bsl (Shift-1))->1;
                {floor,1,R} when R=/=0->1; {ceil,0,R} when R=/=0->1; _->0
            end,
            (Mantissa bsr Shift)+Extra;
        true -> Mantissa bsl (E-52)
    end,
    case Sign of 0->Magnitude;1->-Magnitude end.
