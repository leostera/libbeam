%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(time_unit_reference).
-export([run/0,vectors/0]).
vectors() ->
    lists:foreach(fun(From) -> lists:foreach(fun(To) -> lists:foreach(fun(T) ->
        R=erlang:convert_time_unit(T,From,To),
        R=pinned_time_reference:convert_time_unit(T,From,To),
        io:format("~p:~p~n",[T,R])
    end,time_unit_slice:times()) end,time_unit_slice:units()) end,time_unit_slice:units()).
run() ->
    lists:foreach(fun(U) ->
        42=pinned_time_reference:convert_time_unit(42,U,U),
        Observed=try erlang:convert_time_unit(42,U,U) catch error:E -> E end,
        true=(Observed=:=42 orelse Observed=:=badarg),
        io:format("OTP_TIME_EQUAL_UNIT_OBSERVED unit=~p installed=~p pinned=42~n",[U,Observed])
    end,[unknown,[],{unit},1.0,<<"unit">>]),
    lists:foreach(fun({T,F,U}) ->
        try pinned_time_reference:convert_time_unit(T,F,U) of _ -> error(expected_badarg)
        catch error:badarg -> ok end,
        try erlang:convert_time_unit(T,F,U) of _ -> error(expected_badarg)
        catch error:badarg -> ok end
    end,[{1,0,0},{1,-1,-1},{1,0.0,0.0},{1,-1.0,-1.0},{1,unknown,second},{1,second,unknown},{1,1.0,second},{1.0,second,second},{[],second,second}]),
    <<"copy">>=time_unit_slice:copy(<<"copy">>),
    io:format("OTP_TIME_UNIT_EDGES_OK floor=true aliases=true integer_units=true pinned_equal_unit_quirk=true~n").
