%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(list_integer_reference).
-export([run/0]).
run() ->
    lists:foreach(fun({Text,Expected}) ->
        Expected = list_integer_slice:parse(Text),
        Expected = list_integer_slice:binary_parse(list_to_binary(Text))
    end, [{"0",0},{"-0",0},{"+000",0},{"+123",123},{"-123",-123},
          {"576460752303423487",(1 bsl 59)-1},{"-576460752303423488",-(1 bsl 59)},
          {"576460752303423488",1 bsl 59},{"-18446744073709551616",-(1 bsl 64)}]),
    lists:foreach(fun(Text) ->
        badarg(fun() -> list_integer_slice:parse(Text) end),
        badarg(fun() -> list_integer_slice:binary_parse(list_to_binary(Text)) end)
    end, ["","+","-"," 1","1 ","1_2","--1","++1","1.0","1a","1\n",[$1,0]]),
    lists:foreach(fun(Value) -> badarg(fun() -> list_integer_slice:parse(Value) end) end,
        [[$1|2],[true],[-1],[256],true,[299,49]]),
    0 = list_integer_slice:parse([304]),
    12 = list_integer_slice:parse([305,306]),
    12 = list_integer_slice:parse([-207,-206]),
    1 = list_integer_slice:parse([304,49]),
    10000000000000000 = list_integer_slice:parse([305|lists:duplicate(16,$0)]),
    badarg(fun() -> list_integer_slice:parse([305|lists:duplicate(17,$0)]) end),
    0 = list_integer_slice:parse(lists:duplicate(5000,$0)),
    Payload = binary:copy(<<0,1,255>>, 10000),
    Payload = list_integer_slice:copy(Payload),
    io:format("OTP_LIST_INTEGER_REFERENCE_OK syntax=true small_big=true native_byte_cast=true fallback_byte_validation=true~n").
badarg(F) ->
    try F() of _ -> error(expected_badarg)
    catch error:badarg -> ok end.
