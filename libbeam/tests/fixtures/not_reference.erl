%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(not_reference).
-export([run/0]).
run() ->
    false = not_slice:negate(true),
    true = not_slice:negate(false),
    false = not_guard_slice:negate(true),
    true = not_guard_slice:negate(false),
    lists:foreach(fun(Value) ->
        try not_slice:negate(Value) of _ -> error(expected_badarg)
        catch error:badarg -> ok end,
        invalid = not_guard_slice:negate(Value)
    end, [invalid,[],0,1.0,{1},[true],1 bsl 64,<<"data">>]),
    <<"payload">> = not_slice:copy(<<"payload">>),
    <<"payload">> = boolean_slice:copy(<<"payload">>),
    Values = [true,false,invalid,[],0,1.0,{1},[true],1 bsl 64,<<"data">>],
    lists:foreach(fun({Op,Body,Guard}) ->
        lists:foreach(fun(A) -> lists:foreach(fun(B) ->
            case is_boolean(A) andalso is_boolean(B) of
                true ->
                    Expected = apply(erlang,Op,[A,B]),
                    Expected = apply(boolean_slice,Body,[A,B]),
                    Expected = apply(boolean_slice,Guard,[A,B]),
                    Expected = apply(boolean_guard_slice,Body,[A,B]),
                    Expected = boolean_slice:dynamic(Op,A,B);
                false ->
                    try apply(boolean_slice,Body,[A,B]) of _ -> error(expected_badarg)
                    catch error:badarg -> ok end,
                    try boolean_slice:dynamic(Op,A,B) of _ -> error(expected_badarg)
                    catch error:badarg -> ok end,
                    invalid = apply(boolean_slice,Guard,[A,B]),
                    invalid = apply(boolean_guard_slice,Body,[A,B])
            end
        end,Values) end,Values)
    end,[{'and',conjunction,guard_and},{'or',disjunction,guard_or},{'xor',exclusive,guard_xor}]),
    io:format("OTP_NOT_REFERENCE_OK strict_boolean=true badarg=true y_guard=true binary_operators=3 operand_pairs=100~n").
