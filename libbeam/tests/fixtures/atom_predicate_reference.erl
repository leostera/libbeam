%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(atom_predicate_reference).
-export([run/0]).
run() ->
    lists:foreach(fun(Value) ->
        true = atom_predicate_slice:check(Value),
        yes = atom_predicate_slice:guarded(Value),
        {atom,Value} = atom_predicate_slice:allocate_guard(Value),
        true = atom_predicate_slice:guard_y(Value,true)
    end, [true,false,no]),
    lists:foreach(fun(Value) ->
        false = atom_predicate_slice:check(Value),
        no = atom_predicate_slice:guarded(Value),
        no = atom_predicate_slice:allocate_guard(Value),
        no = atom_predicate_slice:guard_y(Value,true)
    end, [[],[1],[1|true],-5,1 bsl 64,-(1 bsl 64),1.0,{1},#{},<<"data">>,self(),make_ref(),fun erlang:is_atom/1]),
    yes = atom_predicate_literals:atom(0),
    no = atom_predicate_literals:nonatom(0),
    {false,false,false,false} = atom_predicate_slice:other_types(0),
    <<"payload">> = atom_predicate_slice:copy(<<"payload">>),
    io:format("OTP_ATOM_PREDICATE_REFERENCE_OK booleans_and_atoms=true nil_is_not_atom=true guards=true~n").
