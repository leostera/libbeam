%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(list_predicate_reference).
-export([run/0]).
run() ->
    lists:foreach(fun(Value) ->
        true = list_predicate_slice:check(Value),
        yes = list_predicate_slice:guarded(Value),
        {list,Value} = list_predicate_slice:allocate_guard(Value),
        true = list_predicate_slice:guard_y(Value,true)
    end, [[],[1],[1|true]]),
    lists:foreach(fun(Value) ->
        false = list_predicate_slice:check(Value),
        no = list_predicate_slice:guarded(Value),
        no = list_predicate_slice:allocate_guard(Value),
        no = list_predicate_slice:guard_y(Value,true)
    end, [-5,1 bsl 64,-(1 bsl 64),1.0,true,{1},#{},<<"data">>,self(),make_ref(),fun erlang:is_list/1]),
    yes = list_predicate_literals:list(0),
    no = list_predicate_literals:nonlist(0),
    {false,false,false,false} = list_predicate_slice:other_types(0),
    <<"payload">> = list_predicate_slice:copy(<<"payload">>),
    io:format("OTP_LIST_PREDICATE_REFERENCE_OK nil_cons_improper=true false_types=true guards=true~n").
