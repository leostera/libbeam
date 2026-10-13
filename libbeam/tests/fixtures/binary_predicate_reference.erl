%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(binary_predicate_reference).
-export([run/0]).
run() ->
    Bytes = binary:copy(<<165>>, 100),
    <<_:3, Unaligned:8/bitstring, _/bitstring>> = Bytes,
    <<_:3, Partial:7/bitstring, _/bitstring>> = Bytes,
    lists:foreach(fun(Value) ->
        true = binary_predicate_slice:check(Value),
        yes = binary_predicate_slice:guarded(Value),
        {binary,Value} = binary_predicate_slice:allocate_guard(Value),
        yes = binary_predicate_slice:guard_y(Value,yes)
    end, [<<>>,<<1>>,Bytes,Unaligned]),
    lists:foreach(fun(Value) ->
        false = binary_predicate_slice:check(Value),
        no = binary_predicate_slice:guarded(Value),
        no = binary_predicate_slice:allocate_guard(Value),
        no = binary_predicate_slice:guard_y(Value,yes)
    end, [<<1:7>>,Partial,[],[1|bad_tail],0,1 bsl 100,1.0,true,false,ordinary,{tuple},#{},self(),make_ref(),fun() -> ok end]),
    {false,false,false,false} = binary_predicate_slice:other_types(ignored),
    yes = binary_predicate_literals:binary(ignored),
    no = binary_predicate_literals:nonbinary(ignored),
    <<"payload">> = binary_predicate_slice:copy(<<"payload">>),
    io:format("OTP_BINARY_PREDICATE_REFERENCE_OK byte_length=true partial_false=true unaligned=true guards=true~n").
