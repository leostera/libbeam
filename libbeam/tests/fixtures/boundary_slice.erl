%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(boundary_slice).
-export([setup/1, identity/1, ordinary/1, uncaught/1, nested_after/1, reraised/1,
         denied_catch/1, denied_try/1, denied_child/1, denied_output/1,
         denied_port/1, denied_library/1, denied_halt/1, work_catch/1, child_work/1,
         memory_catch/1, after_count/1, allowed_after/1]).

setup(_) ->
    Pid = spawn(fun() -> watch(0) end),
    true = register(boundary_watch, Pid),
    <<"ready">>.
identity(Input) -> Input.
uncaught(Input) -> error(Input).
watch(Count) ->
    receive
        after_ran -> watch(Count + 1);
        {read, From} -> From ! {after_count, Count}, watch(Count)
    end.
after_count(_) ->
    boundary_watch ! {read, self()},
    receive
        {after_count, 0} -> <<"clean">>;
        {after_count, _} -> <<"after-ran">>
    end.
allowed_after(Input) ->
    try identity(Input)
    after boundary_watch ! after_ran end.

ordinary(Input) ->
    {error, ordinary} = try error(ordinary) catch C:R -> {C, R} end,
    {throw, Input} = try throw(Input) catch C2:R2 -> {C2, R2} end,
    {exit, ordinary_exit} = try exit(ordinary_exit) catch C3:R3 -> {C3, R3} end,
    <<"ok">>.
nested_after(_) ->
    put(order, []),
    try
        try error(inner)
        after put(order, [inner_after | get(order)]) end
    catch
        error:inner -> put(order, [caught | get(order)])
    after put(order, [outer_after | get(order)]) end,
    [outer_after, caught, inner_after] = erase(order),
    <<"ok">>.
reraised(Input) ->
    try
        try error(Input)
        catch C:R:Stack -> erlang:raise(C, R, Stack) end
    catch
        error:Input:Stack2 ->
            [{?MODULE, reraised, 1, _} | _] = Stack2,
            <<"ok">>
    end.

denied_catch(_) ->
    _ = (catch erlang:load_nif("must-not-load", 0)),
    <<"masked host failure">>.
denied_try(_) ->
    try erlang:load_nif("must-not-load", 0)
    catch _:_ -> <<"masked host failure">>
    after boundary_watch ! after_ran end.
denied_child(_) ->
    {Pid, Ref} = spawn_monitor(fun() -> denied_try(<<>>) end),
    receive {'DOWN', Ref, process, Pid, _} -> <<"masked child failure">> end.
denied_output(_) ->
    try erlang:display(boundary_forbidden_output)
    catch _:_ -> <<"masked output failure">> end.
denied_port(Command) ->
    try erlang:open_port({spawn, binary_to_list(Command)}, [])
    catch _:_ -> <<"masked port failure">> end.
denied_library(Path) ->
    try erlang:load_nif(binary_to_list(Path), 0)
    catch _:_ -> <<"masked library failure">> end.
denied_halt(_) ->
    try erlang:halt(73)
    catch _:_ -> <<"masked halt failure">> end.

work_catch(_) ->
    try spin()
    catch _:_ -> <<"masked work failure">>
    after boundary_watch ! after_ran end.
spin() -> spin().
child_work(_) ->
    {Pid, Ref} = spawn_monitor(fun spin/0),
    receive {'DOWN', Ref, process, Pid, _} -> <<"masked child work failure">> end.
memory_catch(Input) ->
    Count = binary_to_term(Input),
    try erlang:make_tuple(Count, ok) of _ -> <<"unexpected allocation">>
    catch _:_ -> <<"masked memory failure">>
    after boundary_watch ! after_ran end.
