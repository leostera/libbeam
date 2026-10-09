%% %CopyrightBegin%
%%
%% SPDX-License-Identifier: Apache-2.0
%%
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
%%
%% Licensed under the Apache License, Version 2.0 (the "License");
%% you may not use this file except in compliance with the License.
%% You may obtain a copy of the License at
%%
%%     http://www.apache.org/licenses/LICENSE-2.0
%%
%% Unless required by applicable law or agreed to in writing, software
%% distributed under the License is distributed on an "AS IS" BASIS,
%% WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
%% See the License for the specific language governing permissions and
%% limitations under the License.
%%
%% %CopyrightEnd%

-module(realm_resource_SUITE).
-include_lib("stdlib/include/assert.hrl").
-export([suite/0, all/0, local_operations/1, spawn_inheritance/1, creator_exit/1,
         foreign_host/1, foreign_sibling/1, foreign_manager/1, stopped_owner/1,
         retained_owner_quota/1, persistent_denied/1, run/2, owner/1]).

%% Bootstrap closures and manager process_info intentionally leak handles. These
%% fixtures do not claim private-code/fun isolation or an approved broker protocol.
suite() -> [{timetrap, {seconds, 60}}].
all() -> [local_operations, spawn_inheritance, creator_exit, foreign_host,
          foreign_sibling, foreign_manager, stopped_owner, retained_owner_quota,
          persistent_denied].

new_resources() ->
    [atomics:new(1, []), counters:new(1, [atomics]),
     counters:new(1, [write_concurrency])].

local_operations(_Config) ->
    local(),
    check(#{restrict_process_access => true}, fun local/0).

local() ->
    [A, CA, CW] = new_resources(),
    ok = atomics:put(A, 1, 10),
    ok = atomics:add(A, 1, 2),
    13 = atomics:add_get(A, 1, 1),
    ok = atomics:sub(A, 1, 2),
    10 = atomics:sub_get(A, 1, 1),
    10 = atomics:exchange(A, 1, 20),
    ok = atomics:compare_exchange(A, 1, 20, 30),
    30 = atomics:compare_exchange(A, 1, 20, 99),
    30 = atomics:get(A, 1),
    #{size := 1} = atomics:info(A),
    ?assertError(badarg, atomics:get(A, 0)),
    ?assertError(badarg, atomics:put(A, 2, 10)),
    ?assertError(badarg, atomics:info(make_ref())),
    lists:foreach(fun(C) ->
        ok = counters:put(C, 1, 10),
        ok = counters:add(C, 1, 2),
        ok = counters:sub(C, 1, 3),
        9 = counters:get(C, 1),
        #{size := 1} = counters:info(C),
        ?assertError(badarg, counters:get(C, 0))
    end, [CA, CW]),
    {write_concurrency, Raw} = CW,
    9 = erts_internal:counters_get(Raw, 1),
    ok = erts_internal:counters_add(Raw, 1, 1),
    ok = erts_internal:counters_put(Raw, 1, 11),
    #{size := 1} = erts_internal:counters_info(Raw),
    ?assertError(badarg, atomics:get(Raw, 1)),
    ?assertError(badarg, erts_internal:counters_get(A, 1)),
    ok.

spawn_inheritance(_Config) ->
    check(#{restrict_process_access => true}, fun() ->
        [A, CA, CW] = new_resources(),
        Workers = [spawn_monitor(fun() ->
            ok = atomics:add(A, 1, 1),
            ok = counters:add(CA, 1, 1),
            ok = counters:add(CW, 1, 1)
        end) || _ <- lists:seq(1, 64)],
        [receive {'DOWN', M, process, P, normal} -> ok end || {P, M} <- Workers],
        64 = atomics:get(A, 1),
        64 = counters:get(CA, 1),
        64 = counters:get(CW, 1),
        ok
    end).

creator_exit(_Config) ->
    check(#{restrict_process_access => true}, fun() ->
        Parent = self(),
        {Pid, Mon} = spawn_monitor(fun() -> Parent ! {resources, new_resources()} end),
        Resources = receive {resources, R} -> R end,
        receive {'DOWN', Mon, process, Pid, normal} -> ok end,
        [A, CA, CW] = Resources,
        ok = atomics:put(A, 1, 42),
        [ok = counters:put(C, 1, 42) || C <- [CA, CW]],
        42 = atomics:get(A, 1),
        [42 = counters:get(C, 1) || C <- [CA, CW]],
        ok
    end).

foreign_host(_Config) ->
    Resources = new_resources(),
    [check(#{restrict_process_access => Restricted}, fun() -> denied(Resources) end)
     || Restricted <- [false, true]],
    zeros(Resources).

foreign_sibling(_Config) ->
    lists:foreach(fun({AR, BR}) ->
        A = realm:create(#{restrict_process_access => AR}),
        try
            Pid = realm:spawn_root(A, ?MODULE, owner, [[]]),
            Resources = resources(Pid),
            denied(Resources), % Even the host manager has no ordinary-use exception.
            check(#{restrict_process_access => BR}, fun() -> denied(Resources) end),
            %% Inspect the owner only for test readiness/data, not resource authority.
            ?assert(is_process_alive(Pid))
        after realm:stop(A) end
    end, [{false, false}, {false, true}, {true, false}, {true, true}]).

foreign_manager(_Config) ->
    check(#{restrict_process_access => true, allow_create_realms => true}, fun() ->
        ParentResources = new_resources(),
        Child = realm:create(#{}),
        try
            Pid = realm:spawn_root(Child, ?MODULE, owner, [ParentResources]),
            ChildResources = resources(Pid),
            denied(ChildResources),
            zeros(ParentResources)
        after realm:stop(Child) end
    end).

stopped_owner(_Config) ->
    lists:foreach(fun(_) ->
        R = realm:create(#{restrict_process_access => true}),
        Handles = try
            Pid = realm:spawn_root(R, ?MODULE, owner, [[]]),
            resources(Pid)
        after realm:stop(R) end,
        denied(Handles),
        check(#{restrict_process_access => true}, fun() -> denied(Handles) end)
    end, lists:seq(1, 20)),
    erlang:garbage_collect(),
    ok.

retained_owner_quota(_Config) ->
    check(#{restrict_process_access => true, allow_create_realms => true,
            max_realms => 2}, fun() ->
        %% Parent never receives a resource or child management handle. Only the
        %% holder keeps those resources after the creator and child root exit.
        Holder = spawn(fun holder/0),
        Parent = self(),
        {Worker, Mon} = spawn_monitor(fun() ->
            Child = realm:create(#{}),
            try
                Pid = realm:spawn_root(Child, ?MODULE, owner, [[]]),
                Holder ! {hold, resources(Pid), self()},
                receive held -> ok end
            after realm:stop(Child) end,
            Parent ! child_stopped
        end),
        receive child_stopped -> ok end,
        receive {'DOWN', Mon, process, Worker, normal} -> ok end,
        ?assertError(system_limit, realm:create(#{})),
        HolderMon = monitor(process, Holder),
        Holder ! release,
        receive {'DOWN', HolderMon, process, Holder, normal} -> ok end,
        await_quota(erlang:monotonic_time(millisecond) + 5000)
    end).

persistent_denied(_Config) ->
    Key = {?MODULE, make_ref()},
    Missing = {?MODULE, make_ref()},
    Value = {host_private, new_resources()},
    ok = persistent_term:put(Key, Value),
    try
        check(#{restrict_process_access => true}, fun() ->
            ?assertError(badarg, persistent_term:get(Key)),
            ?assertError(badarg, persistent_term:get(Missing, fallback)),
            ?assertError(badarg, persistent_term:get()),
            ?assertError(badarg, persistent_term:info()),
            ?assertError(badarg, persistent_term:put(Key, corrupted)),
            ?assertError(badarg, persistent_term:put(Missing, inserted)),
            ?assertError(badarg, persistent_term:put_new(Missing, inserted)),
            ?assertError(badarg, persistent_term:erase(Key)),
            ?assertError(badarg, erts_internal:erase_persistent_terms()),
            %% Indirect dispatch must not bypass the native entry check.
            [ ?assertError(badarg, apply(persistent_term, F, Args)) ||
                {F, Args} <- [{get, [Key]}, {get, [Missing, fallback]}, {get, []},
                              {info, []}, {put, [Key, corrupted]},
                              {put_new, [Missing, inserted]}, {erase, [Key]}]],
            ok
        end),
        check(#{restrict_process_access => false}, fun() ->
            ?assertError(badarg, erts_internal:erase_persistent_terms()),
            ok
        end),
        Value = persistent_term:get(Key),
        absent = persistent_term:get(Missing, absent),
        ok
    after
        persistent_term:erase(Key),
        persistent_term:erase(Missing)
    end.

holder() ->
    receive {hold, Resources, From} -> From ! held, holding(Resources) end.
holding(Resources) ->
    receive
        release -> ok;
        {keep, From} -> From ! {kept, Resources}, holding(Resources)
    end.

await_quota(Deadline) ->
    try realm:create(#{}) of
        R -> realm:stop(R)
    catch error:system_limit ->
        true = erlang:monotonic_time(millisecond) < Deadline,
        receive after 1 -> await_quota(Deadline) end
    end.

owner(Foreign) ->
    case Foreign of [] -> ok; _ -> denied(Foreign) end,
    put(realm_resource_test_handles, new_resources()),
    receive after infinity -> ok end.

resources(Pid) -> resources(Pid, erlang:monotonic_time(millisecond) + 5000).
resources(Pid, Deadline) ->
    {dictionary, Dict} = process_info(Pid, dictionary),
    case lists:keyfind(realm_resource_test_handles, 1, Dict) of
        {_, Resources} -> Resources;
        false ->
            true = erlang:monotonic_time(millisecond) < Deadline,
            receive after 1 -> resources(Pid, Deadline) end
    end.

zeros([A, CA, CW]) ->
    0 = atomics:get(A, 1),
    [0 = counters:get(C, 1) || C <- [CA, CW]],
    ok.

denied([A, CA, CW]) ->
    Ops = [fun() -> atomics:get(A, 1) end, fun() -> atomics:put(A, 1, 99) end,
           fun() -> atomics:add(A, 1, 99) end, fun() -> atomics:add_get(A, 1, 99) end,
           fun() -> atomics:sub(A, 1, 99) end, fun() -> atomics:sub_get(A, 1, 99) end,
           fun() -> atomics:exchange(A, 1, 99) end,
           fun() -> atomics:compare_exchange(A, 1, 0, 99) end,
           fun() -> atomics:info(A) end] ++
        lists:append([[fun() -> counters:get(C, 1) end,
                       fun() -> counters:put(C, 1, 99) end,
                       fun() -> counters:add(C, 1, 99) end,
                       fun() -> counters:sub(C, 1, 99) end,
                       fun() -> counters:info(C) end] || C <- [CA, CW]]),
    [ ?assertError(badarg, F()) || F <- Ops ],
    {write_concurrency, Raw} = CW,
    ?assertError(badarg, erts_internal:counters_get(Raw, 1)),
    ?assertError(badarg, erts_internal:counters_put(Raw, 1, 99)),
    ?assertError(badarg, erts_internal:counters_add(Raw, 1, 99)),
    ?assertError(badarg, erts_internal:counters_info(Raw)),
    ok.

check(Options, Fun) ->
    R = realm:create(Options),
    try
        Out = realm:endpoint_create(R, to_host, 1, 16384),
        _ = realm:spawn_root(R, ?MODULE, run, [Out, Fun]),
        <<"ok">> = await(Out, erlang:monotonic_time(millisecond) + 10000),
        ok
    after realm:stop(R) end.
run(Out, Fun) ->
    ok = Fun(),
    ok = realm:endpoint_send(Out, <<"ok">>),
    receive after infinity -> ok end.
await(Out, Deadline) ->
    case realm:endpoint_receive(Out) of
        {ok, Binary} -> Binary;
        empty ->
            true = erlang:monotonic_time(millisecond) < Deadline,
            receive after 1 -> await(Out, Deadline) end
    end.
