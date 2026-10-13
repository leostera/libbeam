%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(tuple_list_reference).
-export([run/0]).
run() ->
    lists:foreach(fun(N) ->
        lists:foreach(fun(Bytes) ->
            Binary = binary:copy(<<113>>,Bytes),
            Expected = [case I rem 2 of 1 -> -I; 0 -> Binary end || I <- lists:seq(1,N)],
            Input = list_to_tuple(Expected),
            Expected = tuple_list_slice:convert(Input),
            Expected = tuple_list(Input,1,N)
        end,[16,100])
    end,[0,1,4,64,129]),
    lists:foreach(fun(Value) ->
        try tuple_list_slice:convert(Value) of Result -> erlang:error({expected_badarg,Value,Result})
        catch error:badarg -> ok end
    end,[[],ok,42,<<1,2,3>>,[ok],1.0]),
    <<1,2,3>> = tuple_list_slice:copy(<<1,2,3>>),
    io:format("OTP_TUPLE_LIST_REFERENCE_OK empty=true order=true values=true source_unchanged=true badarg=true~n").
tuple_list(_,I,N) when I>N -> [];
tuple_list(Tuple,I,N) -> [element(I,Tuple)|tuple_list(Tuple,I+1,N)].
