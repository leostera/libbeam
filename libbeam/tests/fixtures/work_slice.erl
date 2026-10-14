%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(work_slice).
-export([pure/1, loop/1, waiting/1, private_wait/1, background/1,
         escaped_wait/1, registered_wait/1, boot/1, request/1, noise/1,
         foreign_wait/1, deliver/1, native_decimal/1, format_number/1,
         native_equal/1, gc_wait/1, monitor_work/1, native_small_decimal/1,
         native_big_float_binary/1, format_float/1]).
pure(Binary) -> Binary.
loop(Binary) -> loop(Binary).
waiting(Binary) -> receive after infinity -> Binary end.
private_wait(Binary) ->
    spawn(fun() -> loop(Binary) end),
    waiting(Binary).
background(Binary) ->
    Parent = self(),
    spawn(fun() ->
        Ref = monitor(process, Parent),
        receive {'DOWN', Ref, process, Parent, _} -> loop(Binary) end
    end),
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
foreign_wait(_) ->
    register(work_listener, self()),
    receive {foreign, Binary} -> Binary end.
deliver(Binary) -> work_listener ! {foreign, Binary}, <<"delivered">>.
native_decimal(_) -> ?MODULE:format_number(1 bsl 8192).
format_number(Value) -> integer_to_binary(Value).
native_small_decimal(_) -> ?MODULE:format_number(7).
native_big_float_binary(_) -> ?MODULE:format_float(1 bsl 400).
format_float(Value) -> <<Value:64/float>>.
native_equal(_) -> compare_lists(make_list(0, []), make_list(0, [])).
compare_lists(A, B) ->
    case A =:= B of true -> <<"equal">>; false -> <<"unequal">> end.
make_list(256, Acc) -> Acc;
make_list(N, Acc) -> make_list(N + 1, [N | Acc]).
gc_wait(_) ->
    List = make_list(0, []),
    receive finish -> case List of [_ | _] -> <<"collected">> end end.
monitor_work(Binary) -> monitor_work(Binary, 8).
monitor_work(Binary, 0) -> Binary;
monitor_work(Binary, Count) ->
    {Pid, Ref} = erlang:spawn_opt(erlang, apply, [fun() -> ok end, []], [monitor]),
    receive {'DOWN', Ref, process, Pid, normal} -> monitor_work(Binary, Count + (-1)) end.
