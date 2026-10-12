%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(work_slice).
-export([pure/1, loop/1, waiting/1, private_wait/1, background/1,
         escaped_wait/1, registered_wait/1, boot/1, request/1, noise/1]).
pure(Binary) -> Binary.
loop(Binary) -> loop(Binary).
waiting(Binary) -> receive after infinity -> Binary end.
private_wait(Binary) ->
    spawn(fun() -> loop(Binary) end),
    waiting(Binary).
background(Binary) ->
    spawn(fun() -> receive after 5 -> loop(Binary) end end),
    <<"ready">>.
escaped_wait(Binary) ->
    Pid = spawn(fun() -> loop(Binary) end),
    register(work_actor, Pid),
    waiting(Binary).
registered_wait(Binary) ->
    Pid = spawn(fun() -> waiting(Binary) end),
    register(work_actor, Pid),
    waiting(Binary).
boot(_) -> register(work_actor, spawn(fun() -> actor() end)), <<"boot">>.
request(Binary) ->
    work_actor ! {request, self(), Binary},
    receive {answer, Reply} -> Reply end.
noise(Binary) -> work_actor ! {noise, Binary}, <<"noise">>.
actor() ->
    receive {request, From, Binary} -> From ! {answer, Binary}, actor() end.
