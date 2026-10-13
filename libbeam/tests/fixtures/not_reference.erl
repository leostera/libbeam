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
    io:format("OTP_NOT_REFERENCE_OK strict_boolean=true badarg=true y_guard=true~n").
