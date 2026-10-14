%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(unique_reference).
-export([run/0]).
run() ->
    lists:foreach(fun(Flags) ->
        Values=[erlang:unique_integer(Flags++Flags) || _ <- lists:seq(1,128)],
        128=length(lists:usort(Values)), true=lists:all(fun erlang:is_integer/1,Values),
        case lists:member(positive,Flags) of true -> true=lists:all(fun(N)->N>0 end,Values); false -> ok end,
        case lists:member(monotonic,Flags) of true -> Values=lists:sort(Values); false -> ok end
    end,[[],[positive],[monotonic],[positive,monotonic]]),
    Mixed=lists:append([[erlang:unique_integer(),erlang:unique_integer([])] || _ <- lists:seq(1,128)]),
    256=length(lists:usort(Mixed)),
    lists:foreach(fun(Bad) ->
        try erlang:unique_integer(Bad) of _ -> error(expected_badarg)
        catch error:badarg -> ok end
    end,[true,0,{},1.0,[true],[positive|bad_tail],[monotonic,0]]),
    lists:foreach(fun(A) -> lists:foreach(fun(B) ->
        Ge=A>=B, Lt=A<B, GeC=0>=A, GeX=A>=0, LtC=0<A, LtX=A<0,
        Ge=order_slice:ge_xx({A,B}), Ge=order_slice:ge_yy({A,B}),
        Lt=order_slice:lt_xx({A,B}), Lt=order_slice:lt_yy({A,B}),
        GeC=order_slice:ge_cx({A,B}), GeX=order_slice:ge_xc({A,B}),
        LtC=order_slice:lt_cx({A,B}), LtX=order_slice:lt_xc({A,B})
    end,lists:seq(-2,2)) end,lists:seq(-2,2)),
    <<"copy">>=unique_slice:copy(<<"copy">>),
    <<"family">>=unique_slice:family(<<"family">>),
    io:format("OTP_UNIQUE_REFERENCE_OK modifiers=true duplicate_modifiers=true uniqueness=true positive=true monotonic=true children=true badarg=true~n").
