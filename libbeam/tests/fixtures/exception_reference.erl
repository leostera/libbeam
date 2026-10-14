%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
%% Stock-only driver; exception_slice itself is also executed by libbeam.
-module(exception_reference).
-export([run/0]).
run() ->
    Value = binary:copy(<<87>>, 65),
    lists:foreach(fun(Function) ->
        try apply(exception_slice, Function, [Value]) of
            _ -> error(expected_failure)
        catch error:Reason ->
            case Reason of
                {Tag, Value} ->
                    io:format("~w:~w:65:87~n", [Function, Tag]);
                Atom when is_atom(Atom) ->
                    io:format("~w:~w~n", [Function, Atom])
            end
        end
    end, [badmatch, case_clause, function_clause, bif]),
    {'EXIT', Value} = (catch exception_slice:exiting(Value)),
    {'EXIT', normal} = (catch exception_slice:exiting(normal)).
