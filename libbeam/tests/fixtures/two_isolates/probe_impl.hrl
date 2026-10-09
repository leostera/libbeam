%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>

%% Included in both variants. Only the binary version tag differs.
%% No files, ports, NIFs, distribution, on_load, or tenant lifecycle API.
-export([boot/1, request/1]).

boot(<<>>) ->
    Parent = self(),
    {Pid, Monitor} = spawn_monitor(fun() ->
        true = register(probe_state, self()),
        Parent ! {self(), ready},
        loop(0)
    end),
    receive
        {Pid, ready} ->
            demonitor(Monitor, [flush]),
            <<?VERSION/binary, ":ready">>;
        {'DOWN', Monitor, process, Pid, Reason} ->
            error({bootstrap_failed, Reason})
    after 5000 ->
        exit(Pid, kill),
        error(bootstrap_timeout)
    end.

request(Command) when Command =:= <<"next">>; Command =:= <<"read">> ->
    Ref = make_ref(),
    probe_state ! {self(), Ref, Command},
    receive
        {Ref, Reply} -> Reply
    after 5000 ->
        error(request_timeout)
    end.

loop(Count) ->
    receive
        {From, Ref, <<"next">>} ->
            Next = Count + 1,
            From ! {Ref, <<?VERSION/binary, ":", (integer_to_binary(Next))/binary>>},
            loop(Next);
        {From, Ref, <<"read">>} ->
            From ! {Ref, <<?VERSION/binary, ":", (integer_to_binary(Count))/binary>>},
            loop(Count)
    end.
