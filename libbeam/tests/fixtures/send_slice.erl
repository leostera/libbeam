%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(send_slice).
-export([identity/1, copy/1, named/1, dead/1, bad_destination/1, missing/1]).
identity(B) -> B.
copy(B) ->
    B = erlang:send(self(), B),
    receive B -> B end.
named(B) ->
    true = register(send_probe, self()),
    B = erlang:send(send_probe, B),
    true = unregister(send_probe),
    receive B -> B end.
dead(B) ->
    {Pid, Ref} = spawn_monitor(fun() -> ok end),
    receive {'DOWN', Ref, process, Pid, normal} ->
        B = erlang:send(Pid, B), B
    end.
bad_destination(B) -> erlang:send(binary_to_term(B), B).
missing(B) -> erlang:send(send_missing, B).
