%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(list_tuple_reference).
-export([run/0]).
run() ->
    lists:foreach(fun(N) ->
        lists:foreach(fun(Bytes) ->
            Binary = binary:copy(<<113>>,Bytes),
            Input = [case I rem 2 of 1 -> -I; 0 -> Binary end || I <- lists:seq(1,N)],
            Expected = lists:foldl(fun({I,V},T) -> setelement(I,T,V) end,
                erlang:make_tuple(N,unused),lists:zip(lists:seq(1,N),Input)),
            Expected = list_tuple_slice:convert(Input),
            Input = tuple_to_list(Expected)
        end,[16,100])
    end,[0,1,4,64,129]),
    lists:foreach(fun(Value) ->
        try list_tuple_slice:convert(Value) of Result -> erlang:error({expected_badarg,Value,Result})
        catch error:badarg -> ok end
    end,[{},ok,42,<<1,2,3>>,[ok|true],1.0,lists:duplicate(65,ok) ++ tail]),
    <<1,2,3>> = list_tuple_slice:copy(<<1,2,3>>),
    io:format("OTP_LIST_TUPLE_REFERENCE_OK empty=true proper_lists=true order=true values=true improper_badarg=true~n").
