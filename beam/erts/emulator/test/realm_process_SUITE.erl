%%
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
%%
-module(realm_process_SUITE).
%% Exercise deprecated exit entry points as well as their replacements.
-compile({nowarn_deprecated_function, [{erlang, exit, 2}, {erlang, exit, 3}]}).
-include_lib("common_test/include/ct.hrl").
-include_lib("stdlib/include/assert.hrl").
-export([suite/0, all/0, policy/1, local_messages/1, local_controls/1,
         denied_sends/1, incoming_denied/1, denied_controls/1,
         names_and_timers/1, tracing_and_distribution/1, local_names/1,
         cleanup_races/1]).
-export([echo/0]).

%% The controller provisions endpoints before execution. No ordinary foreign
%% message is used to bootstrap or report results. These remain trusted fixtures,
%% not proof against native/shared-state or incoming distribution bypasses.
suite() -> [{timetrap, {seconds, 30}}].
all() -> [policy, local_messages, local_controls, denied_sends,
          incoming_denied, denied_controls, names_and_timers,
          tracing_and_distribution, local_names, cleanup_races].

policy(_Config) ->
    ?assertError(badarg, erts_internal:realm_create(#{restrict_process_access => 1})),
    Host = self(),
    Marker = make_ref(),
    run(#{allow_create_realms => true}, fun() ->
        ?assertError(badarg, erts_internal:realm_create(#{restrict_process_access => false})),
        ?assertError(badarg, process_flag(restrict_process_access, false)),
        Child = erts_internal:realm_create(#{}),
        P = erts_internal:realm_spawn_root(Child, erlang, apply, [fun() ->
            ?assertError(badarg, erlang:send(Host, Marker)),
            put(inherited_restriction, true),
            receive after infinity -> ok end
        end, []]),
        await_dictionary(P, 5000),
        %% An authorized manager retains lifecycle/inspection authority, but
        %% that does not authorize ordinary cross-Realm message delivery.
        ?assertError(badarg, erlang:send(P, Marker)),
        ok = erts_internal:realm_stop(Child)
    end),
    assert_absent(Marker).

await_dictionary(_P, 0) -> error(inheritance_timeout);
await_dictionary(P, N) ->
    case process_info(P, dictionary) of
        {dictionary, [{inherited_restriction, true}]} -> ok;
        {dictionary, _} -> receive after 1 -> ok end, await_dictionary(P, N - 1);
        undefined -> error(inheritance_child_died)
    end.

local_messages(_Config) ->
    run(fun() -> lists:foreach(fun local_message_round/1, [on_heap, off_heap]) end).

local_message_round(QueueMode) ->
    Parent = self(),
    {P, Mon} = spawn_opt(fun() ->
        Parent ! {self(), alias([priority])},
        echo()
    end, [monitor, {message_queue_data, QueueMode}]),
    A = receive {P, Alias} -> Alias after 5000 -> error(alias_timeout) end,
    {echo, Parent, 1} = erlang:send(P, {echo, Parent, 1}),
    ok = erlang:send(P, {echo, Parent, 2}, [priority]),
    {echo, Parent, 3} = erlang:send(A, {echo, Parent, 3}),
    ok = erlang:send(A, {echo, Parent, 4}, [priority]),
    true = erlang:send_nosuspend(A, {echo, Parent, 5}),
    Replies = [receive {ack, I} -> I after 5000 -> error(message_timeout) end
               || _ <- lists:seq(1, 5)],
    [1, 2, 3, 4, 5] = lists:sort(Replies),
    P ! stop,
    receive {'DOWN', Mon, process, P, normal} -> ok
    after 5000 -> error(down_timeout)
    end.

echo() ->
    receive
        {echo, From, Value} -> From ! {ack, Value}, echo();
        stop -> ok
    end.

local_controls(_Config) ->
    run(fun() ->
        process_flag(trap_exit, true),
        P = spawn_link(fun() -> receive stop -> ok end end),
        true = unlink(P),
        true = link(P, [priority]),
        Mon = monitor(process, P),
        true = group_leader(self(), P),
        LeaderRef = make_ref(),
        ok = erts_internal:group_leader(self(), P, LeaderRef),
        receive {LeaderRef, true} -> ok after 5000 -> error(leader_timeout) end,
        0 = process_flag(P, save_calls, 4),
        true = is_process_alive(P),
        AliveRef = make_ref(),
        ok = erts_internal:is_process_alive(P, AliveRef),
        receive {AliveRef, true} -> ok after 5000 -> error(alive_timeout) end,
        false = erts_internal:is_system_process(P),
        true = is_boolean(erts_internal:is_process_executing_dirty(P)),
        true = garbage_collect(P),
        false = erlang:check_process_code(P, realm_process_SUITE_missing_module),
        true = is_list(process_info(P)),
        true = erlang:suspend_process(P),
        {status, suspended} = process_info(P, status),
        true = erlang:resume_process(P),
        P ! stop,
        receive {'DOWN', Mon, process, P, normal} -> ok
        after 5000 -> error(down_timeout)
        end,
        receive {'EXIT', P, normal} -> ok after 5000 -> error(exit_timeout) end
    end).

denied_sends(_Config) ->
    Host = self(),
    Alias = alias([priority]),
    AliasTarget = spawn(fun() -> receive after infinity -> ok end end),
    MonitorAlias = monitor(process, AliasTarget, [{alias, reply_demonitor}, priority]),
    Marker = make_ref(),
    Name = realm_process_foreign_destination,
    true = register(Name, Host),
    try
        run(fun() ->
            lists:foreach(fun(To) ->
                ?assertError(badarg, erlang:send(To, Marker)),
                ?assertError(badarg, erlang:send(To, Marker, [])),
                ?assertError(badarg, erlang:send(To, Marker, [priority])),
                ?assertError(badarg, erlang:send_nosuspend(To, Marker))
            end, [Host, Alias, MonitorAlias, Name, {Name, node()}])
        end),
        assert_absent(Marker),
        %% Rejected traffic must not consume a reply-demonitor alias.
        Probe = make_ref(),
        ok = erlang:send(MonitorAlias, Probe, [priority]),
        receive Probe -> ok after 5000 -> error(alias_was_consumed) end
    after
        unregister(Name),
        unalias(Alias),
        demonitor(MonitorAlias, [flush]),
        erlang:exit_signal(AliasTarget, kill)
    end.

incoming_denied(_Config) ->
    Realm = new_realm(#{}),
    In = erts_internal:realm_endpoint_create(Realm, to_realm, 2, 1024),
    Out = erts_internal:realm_endpoint_create(Realm, to_host, 2, 1024),
    Marker = make_ref(),
    try
        Root = erts_internal:realm_spawn_root(Realm, erlang, apply, [fun() ->
            A = alias([priority]),
            ok = erts_internal:realm_endpoint_send(Out, term_to_binary(A)),
            <<"finish">> = await_endpoint(In, 5000),
            assert_absent(Marker),
            ok = erts_internal:realm_endpoint_send(Out, <<"ok">>)
        end, []]),
        %% Only trusted fixture-generated data is decoded, never a broker API.
        A = binary_to_term(await_endpoint(Out, 5000), [safe]),
        ?assertError(badarg, erlang:send(Root, Marker)),
        ?assertError(badarg, erlang:send(A, Marker, [priority])),
        run(#{restrict_process_access => false}, fun() ->
            ?assertError(badarg, erlang:send(Root, Marker)),
            ?assertError(badarg, erlang:send(A, Marker, [priority]))
        end),
        ok = erts_internal:realm_endpoint_send(In, <<"finish">>),
        <<"ok">> = await_endpoint(Out, 5000)
    after
        ok = erts_internal:realm_stop(Realm)
    end.

denied_controls(_Config) ->
    Target = spawn(fun() -> receive after infinity -> ok end end),
    Name = realm_process_foreign_control,
    true = register(Name, Target),
    Keys = [group_leader, links, monitored_by, last_calls],
    Before = process_info(Target, Keys),
    try
        run(fun() ->
            Calls = [fun() -> link(Target) end,
                     fun() -> link(Target, [priority]) end,
                     fun() -> unlink(Target) end,
                     fun() -> monitor(process, Target) end,
                     fun() -> monitor(process, Target, [{alias, reply_demonitor}]) end,
                     fun() -> monitor(process, Target, [priority]) end,
                     fun() -> monitor(process, Name) end,
                     fun() -> monitor(process, {Name, node()}) end,
                     fun() -> exit(Target, kill) end,
                     fun() -> exit(Target, kill, [priority]) end,
                     fun() -> erlang:exit_signal(Target, kill) end,
                     fun() -> erlang:exit_signal(Target, kill, [priority]) end,
                     fun() -> garbage_collect(Target) end,
                     fun() -> garbage_collect(Target, [{async, make_ref()}]) end,
                     fun() -> erlang:check_process_code(Target, ?MODULE) end,
                     fun() -> erts_internal:process_display(Target, backtrace) end,
                     fun() -> erts_internal:request_system_task(
                                  Target, normal, {garbage_collect, make_ref(), major}) end,
                     %% A same-Realm task must not become a foreign reply channel.
                     fun() -> erts_internal:request_system_task(
                                  Target, self(), normal, {garbage_collect, make_ref(), major}) end,
                     fun() -> process_info(Target) end,
                     fun() -> process_info(Target, dictionary) end,
                     fun() -> is_process_alive(Target) end,
                     fun() -> erts_internal:is_process_alive(Target, make_ref()) end,
                     fun() -> erts_internal:is_system_process(Target) end,
                     fun() -> erts_internal:is_process_executing_dirty(Target) end,
                     fun() -> process_flag(Target, save_calls, 9) end,
                     fun() -> erts_internal:process_flag(Target, save_calls, 9) end,
                     fun() -> group_leader(Target, self()) end,
                     fun() -> group_leader(self(), Target) end,
                     fun() -> erts_internal:group_leader(self(), Target, make_ref()) end,
                     fun() -> erts_internal:group_leader(Target, self(), make_ref()) end,
                     fun() -> erlang:suspend_process(Target) end,
                     fun() -> erlang:suspend_process(Target, [asynchronous]) end,
                     fun() -> erts_internal:suspend_process(Target, []) end,
                     fun() -> erlang:resume_process(Target) end],
            lists:foreach(fun(F) -> ?assertError(badarg, F()) end, Calls),
            badarg = erts_internal:group_leader(Target, self()),
            {monitors, []} = process_info(self(), monitors),
            {links, []} = process_info(self(), links),
            {messages, []} = process_info(self(), messages)
        end),
        true = is_process_alive(Target),
        Before = process_info(Target, Keys)
    after
        exit(Target, kill)
    end.

names_and_timers(_Config) ->
    Host = self(),
    Name = realm_process_registry_probe,
    T = erlang:send_after(100000, Host, unexpected_timer),
    try
        run(fun() ->
            Calls = [fun processes/0, fun erlang:ports/0, fun registered/0,
                     fun() -> erts_internal:processes_next(0) end,
                     fun() -> register(Name, self()) end,
                     fun() -> unregister(Name) end,
                     fun() -> whereis(Name) end,
                     fun() -> erlang:send_after(0, Host, unexpected_timer) end,
                     fun() -> erlang:send_after(0, self(), unexpected_timer, []) end,
                     fun() -> erlang:start_timer(0, Host, unexpected_timer) end,
                     fun() -> erlang:start_timer(0, self(), unexpected_timer, []) end,
                     fun() -> erlang:cancel_timer(T) end,
                     fun() -> erlang:cancel_timer(T, [{async, true}]) end,
                     fun() -> erlang:read_timer(T) end,
                     fun() -> erlang:read_timer(T, [{async, true}]) end],
            lists:foreach(fun(F) -> ?assertError(badarg, F()) end, Calls),
            {messages, []} = process_info(self(), messages),
            receive after 1 -> ok end
        end),
        true = is_integer(erlang:read_timer(T)),
        undefined = whereis(Name),
        assert_absent(unexpected_timer)
    after
        erlang:cancel_timer(T)
    end.

tracing_and_distribution(_Config) ->
    Remote = 'realm_unreachable@invalid',
    false = lists:member(Remote, nodes(known)),
    run(fun() ->
        Calls = [fun() -> erlang:trace(self(), true, [send]) end,
                 fun() -> erts_internal:trace(self(), true, [send]) end,
                 fun() -> erlang:trace_pattern({'_', '_', '_'}, true, []) end,
                 fun() -> erlang:trace_info(self(), flags) end,
                 fun() -> erlang:trace_delivered(self()) end,
                 fun() -> erts_internal:trace_session_create(realm_test, self(), []) end,
                 fun() -> erlang:seq_trace(label, 1) end,
                 fun() -> erlang:seq_trace_print(probe) end,
                 fun() -> erlang:system_profile() end,
                 fun() -> erlang:system_profile(undefined, []) end,
                 fun() -> erlang:system_monitor() end,
                 fun() -> erlang:system_monitor(self(), [busy_port]) end,
                 fun() -> erlang:system_flag(backtrace_depth, 1) end,
                 fun() -> process_flag(monitor_nodes, true) end,
                 fun nodes/0,
                 fun() -> nodes(known) end,
                 fun() -> nodes(known, #{}) end,
                 fun() -> erlang:monitor_node(Remote, true) end,
                 fun() -> erlang:monitor_node(Remote, true, []) end,
                 fun() -> erlang:send({sink, Remote}, probe, [noconnect]) end,
                 fun() -> monitor(process, {sink, Remote}) end,
                 fun() -> spawn(Remote, erlang, self, []) end,
                 fun() -> erts_internal:dist_spawn_request(Remote, {erlang, self, []}, [], spawn_request) end,
                 fun() -> erlang:setnode(Remote, 0) end,
                 fun() -> erts_internal:create_dist_channel(Remote, self(), []) end],
        lists:foreach(fun(F) -> ?assertError(badarg, F()) end, Calls)
    end),
    false = lists:member(Remote, nodes(known)).

local_names(_Config) ->
    Realm = new_realm(#{}),
    Out = erts_internal:realm_endpoint_create(Realm, to_host, 1, 65536),
    Name = realm_process_local_destination,
    try
        P = erts_internal:realm_spawn_root(Realm, ?MODULE, echo, []),
        true = register(Name, P),
        start(Realm, Out, fun() ->
            Name ! {echo, self(), first},
            {Name, node()} ! {echo, self(), second},
            receive {ack, first} -> ok after 5000 -> error(named_timeout) end,
            receive {ack, second} -> ok after 5000 -> error(named_timeout) end
        end),
        expect_ok(Out)
    after
        ok = erts_internal:realm_stop(Realm)
    end,
    undefined = whereis(Name).

cleanup_races(_Config) ->
    run(fun() ->
        lists:foreach(fun(_) ->
            {P, M} = spawn_monitor(fun() -> ok end),
            %% Sending to a dying same-Realm process keeps ordinary semantics.
            ignored = erlang:send(P, ignored),
            receive {'DOWN', M, process, P, normal} -> ok
            after 5000 -> error(cleanup_timeout)
            end
        end, lists:seq(1, 500))
    end).

new_realm(Policy) ->
    erts_internal:realm_create(maps:merge(#{restrict_process_access => true}, Policy)).
run(Fun) -> run(#{}, Fun).
run(Policy, Fun) ->
    Realm = new_realm(Policy),
    try
        Out = erts_internal:realm_endpoint_create(Realm, to_host, 1, 65536),
        start(Realm, Out, Fun),
        expect_ok(Out)
    after
        ok = erts_internal:realm_stop(Realm)
    end.
start(Realm, Out, Fun) ->
    erts_internal:realm_spawn_root(Realm, erlang, apply, [fun() ->
        Result = try Fun(), <<"ok">>
                 catch C:R:S -> iolist_to_binary(io_lib:format("~p:~p ~p", [C, R, S]))
                 end,
        ok = erts_internal:realm_endpoint_send(Out, Result)
    end, []]).
expect_ok(Out) ->
    case await_endpoint(Out, 10000) of
        <<"ok">> -> ok;
        Error -> ct:fail({realm_failure, Error})
    end.
await_endpoint(_Ep, 0) -> error(endpoint_timeout);
await_endpoint(Ep, N) ->
    case erts_internal:realm_endpoint_receive(Ep) of
        {ok, Value} -> Value;
        empty -> receive after 1 -> ok end, await_endpoint(Ep, N - 1);
        closed -> error(endpoint_closed)
    end.
assert_absent(Marker) ->
    receive Marker -> error(forbidden_delivery) after 0 -> ok end.
