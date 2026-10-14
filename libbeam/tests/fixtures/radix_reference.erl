%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(radix_reference).
-export([run/0]).
run() ->
    lists:foreach(fun(Base) -> io:put_chars(radix_slice:all(integer_to_binary(Base))) end,lists:seq(2,36)),
    lists:foreach(fun(Base) ->
        Digit = if Base==36 -> $_; Base<10 -> $0+Base; true -> $a+Base-10 end,
        try binary_to_integer(<<Digit>>,Base) of _ -> error(expected_badarg)
        catch error:badarg -> ok end,
        lists:foreach(fun(B) ->
            try binary_to_integer(B,Base) of _ -> error(expected_badarg)
            catch error:badarg -> ok end
        end,[<<>>,<<"+">>,<<"-">>,<<" 1">>,<<"1 ">>,<<"1_0">>,<<"--1">>,<<"+-1">>,<<"1\n">>,<<0>>,<<"1",0>>,<<194,178>>])
    end,lists:seq(2,36)),
    lists:foreach(fun(Base) ->
        try binary_to_integer(<<"1">>,Base) of _ -> error(expected_badarg)
        catch error:badarg -> ok end,
        try integer_to_list(0,Base) of _ -> error(expected_badarg)
        catch error:badarg -> ok end
    end,[0,1,37,-1,2.0,true,[],1 bsl 64]),
    lists:foreach(fun(F) ->
        try apply(radix_slice,F,[<<"10">>]) of _ -> error(expected_badarg)
        catch error:badarg -> ok end
    end,[bad_digit,bad_base,bad_type,bad_bits,bad_integer,bad_integer_two,bad_integer_base]),
    lists:foreach(fun(Value) ->
        try integer_to_list(Value) of _ -> error(expected_badarg)
        catch error:badarg -> ok end,
        try integer_to_list(Value,16) of _ -> error(expected_badarg)
        catch error:badarg -> ok end
    end,[true,[],1.0,{1},[1],<<"1">>]),
    <<"list">> = radix_slice:list_copy(<<"list">>),
    <<>> = radix_slice:list_copy(<<>>),
    ok.
