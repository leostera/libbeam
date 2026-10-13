%% SPDX-License-Identifier: Apache-2.0
%% Reference oracle only; exception execution is not yet admitted by libbeam.
-module(raise_trace_reference).
-export([run/0, execution/0]).
capture(Class, Trace) ->
    try erlang:raise(Class, reason, Trace) of
        Returned -> Returned
    catch
        C:R:S -> {C, R, S}
    end.
run() ->
    badarg = capture(true, []),
    badarg = capture(error, false),
    Legacy = {erlang, error, true},
    Frames = lists:duplicate(10, Legacy),
    badarg = capture(error, Frames ++ [42]),
    badarg = capture(error, Frames ++ false),
    {throw, reason, Normalized} = capture(throw, Frames),
    8 = length(Normalized),
    true = lists:all(fun(Item) -> Item =:= {erlang, error, true, []} end, Normalized),
    {exit, reason, []} = capture(exit, []),
    Fun = fun() -> ok end,
    {error, reason, [{Fun, arbitrary_args, []}]} = capture(error, [{Fun, arbitrary_args}]),
    Location = [arbitrary | improper],
    {error, reason, [{erlang, error, arbitrary_args, Location}]} =
        capture(error, [{erlang, error, arbitrary_args, Location}]),
    badarg = capture(error, [{erlang, error, 1, invalid_location}]),
    io:format("RAISE_TRACE_REFERENCE_OK~n").
execution() ->
    Value = <<"payload">>,
    Trace = [{raise_slice, origin, 1, []}],
    lists:foreach(fun({Function, Expected}) ->
        {Pid, Ref} = spawn_monitor(fun() -> apply(raise_slice, Function, [Value]) end),
        receive
            {'DOWN', Ref, process, Pid, Actual} -> Expected = Actual
        end
    end, [{raised_error, {Value, Trace}}, {raised_exit, Value},
          {raised_throw, {{nocatch, Value}, Trace}}]),
    badarg = raise_slice:invalid_class(Value),
    badarg = raise_slice:invalid_trace(Value),
    Value = raise_slice:monitored(Value),
    io:format("RAISE_EXECUTION_REFERENCE_OK~n").
