%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(integer_predicate_reference).
-export([run/0]).
run() ->
    lists:foreach(fun(Value) ->
        true = integer_predicate_slice:check(Value),
        yes = integer_predicate_slice:guarded(Value),
        {integer,Value} = integer_predicate_slice:allocate_guard(Value),
        true = integer_predicate_slice:guard_y(Value,true)
    end, [0,-5,(1 bsl 59)-1,-(1 bsl 59),1 bsl 64,-(1 bsl 64)]),
    lists:foreach(fun(Value) ->
        false = integer_predicate_slice:check(Value),
        no = integer_predicate_slice:guarded(Value),
        no = integer_predicate_slice:allocate_guard(Value),
        no = integer_predicate_slice:guard_y(Value,true)
    end, [1.0,true,[],[1],{1},#{},<<"data">>,self(),make_ref(),fun erlang:is_integer/1]),
    yes = integer_predicate_literals:integer(0),
    no = integer_predicate_literals:noninteger(0),
    {false,false,false,false} = integer_predicate_slice:other_types(0),
    <<"payload">> = integer_predicate_slice:copy(<<"payload">>),
    io:format("OTP_INTEGER_PREDICATE_REFERENCE_OK small_big=true false_types=true guards=true~n").
