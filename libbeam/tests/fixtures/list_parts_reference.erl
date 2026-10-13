%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(list_parts_reference).
-export([run/0]).
run() ->
    lists:foreach(fun({List, Head, Tail, HeadGuard, TailGuard}) ->
        Head = list_parts_slice:head(List),
        Tail = list_parts_slice:tail(List),
        HeadGuard = list_parts_slice:guarded_head(List),
        TailGuard = list_parts_slice:guarded_tail(List),
        HeadGuard = list_parts_slice:guarded_head_y(List,yes),
        TailGuard = list_parts_slice:guarded_tail_y(List,yes)
    end, [{[7],7,[],yes,no}, {[7|done],7,done,yes,yes},
          {[{42}|done],{42},done,no,yes}, {[7|{42}],7,{42},yes,no}]),
    lists:foreach(fun(Value) ->
        badarg(fun() -> list_parts_slice:head(Value) end),
        badarg(fun() -> list_parts_slice:tail(Value) end),
        no = list_parts_slice:guarded_head(Value),
        no = list_parts_slice:guarded_tail(Value),
        no = list_parts_slice:guarded_head_y(Value,yes),
        no = list_parts_slice:guarded_tail_y(Value,yes)
    end, [[],9]),
    Payload = binary:copy(<<0,1,255>>,10000),
    Payload = list_parts_slice:copy(Payload),
    io:format("OTP_LIST_PARTS_REFERENCE_OK head_tail=true proper_improper=true badarg=true guards=true~n").
badarg(F) ->
    try F() of _ -> error(expected_badarg)
    catch error:badarg -> ok end.
