%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(make_tuple_reference).
-export([run/0]).
run() ->
    lists:foreach(fun(N) ->
        lists:foreach(fun(Value) ->
            Expected = list_to_tuple(lists:duplicate(N,Value)),
            Expected = make_tuple_slice:build({N,Value})
        end,[ok,[1,2],{nested,42},binary:copy(<<127>>,16),binary:copy(<<127>>,100)])
    end,[0,1,4,64,129]),
    lists:foreach(fun(N) ->
        try make_tuple_slice:build({N,ok}) of Value -> erlang:error({expected_badarg,N,Value})
        catch error:badarg -> ok end
    end,[-1,1 bsl 24,[],false,1 bsl 60,1.0]),
    <<1,2,3>> = make_tuple_slice:copy(<<1,2,3>>),
    io:format("OTP_MAKE_TUPLE_REFERENCE_OK empty=true filled=true nested_values=true badarg=true max_plus_one=true~n").
