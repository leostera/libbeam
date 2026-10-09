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

-module(realm_SUITE).

-include_lib("common_test/include/ct.hrl").
-include_lib("stdlib/include/assert.hrl").

-export([suite/0, all/0]).
-export([host_identity/1, unique_roots/1, spawn_inheritance/1,
         denied_root_creation/1, invalid_arguments/1,
         immutable_membership/1, surviving_child/1, dirty_execution/1,
         invalid_handles/1, denied_management/1, close_admission/1,
         stop_tree/1, concurrent_stop/1, spawn_close_race/1,
         handle_lifetime/1, allocation_failure/1, resumable_stop/1,
         dirty_shutdown/1, policy_validation/1, delegated_creation/1,
         management_scope/1, subtree_shutdown/1, subtree_limits/1,
         retained_realm_limit/1, quota_reclamation/1, concurrent_realm_limit/1,
         closed_delegation/1, depth_limit/1, delegated_stop_race/1,
         opaque_identity/1, canonical_identity/1, identity_authority/1,
         identity_reclamation/1, endpoint_exchange/1, endpoint_bounds/1,
         endpoint_authority/1, endpoint_revocation/1, endpoint_subtree_stop/1,
         endpoint_quota/1, endpoint_reclamation/1, endpoint_stop_race/1,
         prepared_bootstrap/1, empty_realm_lifecycle/1, root_authority/1,
         prepared_quota_rollback/1]).
-export([fill_process_table/0]).
-export([realm_entry/2, report_identity/2, wait_identity/0, prepared_root/3]).

%% These tests exercise identity and lifecycle. The fixture channel uses ordinary
%% cross-Realm messages, which are not yet restricted by the runtime.

suite() ->
    [{timetrap, {seconds, 30}}].

all() ->
    [host_identity, unique_roots, spawn_inheritance,
     denied_root_creation, invalid_arguments, immutable_membership,
     surviving_child, dirty_execution, invalid_handles, denied_management,
     close_admission, stop_tree, concurrent_stop, spawn_close_race,
     handle_lifetime, allocation_failure, resumable_stop, dirty_shutdown,
     policy_validation, delegated_creation, management_scope, subtree_shutdown,
     subtree_limits, retained_realm_limit, quota_reclamation,
     concurrent_realm_limit, closed_delegation, depth_limit, delegated_stop_race,
     opaque_identity, canonical_identity, identity_authority, identity_reclamation,
     endpoint_exchange, endpoint_bounds, endpoint_authority, endpoint_revocation,
     endpoint_subtree_stop, endpoint_quota, endpoint_reclamation, endpoint_stop_race,
     prepared_bootstrap, empty_realm_lifecycle, root_authority, prepared_quota_rollback].

prepared_bootstrap(_Config) ->
    Realm = erts_internal:realm_create(#{max_processes => 1,
                                         restrict_process_access => true}),
    try
        Id = erts_internal:realm_identity(Realm),
        In = erts_internal:realm_endpoint_create(Realm, to_realm, 2, 100),
        Out = erts_internal:realm_endpoint_create(Realm, to_host, 2, 100),
        ok = erts_internal:realm_endpoint_send(In, <<"start">>),
        ?assertError(badarg, erts_internal:realm_spawn_root(Realm, 42, start, [])),
        Root = erts_internal:realm_spawn_root(Realm, ?MODULE, prepared_root, [In, Out, Id]),
        <<"started">> = endpoint_await(Out, 5000),
        ?assertError(system_limit,
                     erts_internal:realm_spawn_root(Realm, ?MODULE, wait_identity, [])),
        ok = erts_internal:realm_stop(Realm),
        false = is_process_alive(Root),
        closed = erts_internal:realm_endpoint_receive(Out),
        ?assertError(system_limit,
                     erts_internal:realm_spawn_root(Realm, ?MODULE, wait_identity, []))
    after
        ok = erts_internal:realm_stop(Realm)
    end.

prepared_root(In, Out, Id) ->
    Id = erts_internal:realm_identity(),
    {ok, <<"start">>} = erts_internal:realm_endpoint_receive(In),
    ?assertError(system_limit, spawn(fun() -> ok end)),
    ok = erts_internal:realm_endpoint_send(Out, <<"started">>),
    wait_identity().

endpoint_await(_Ep, 0) -> ct:fail(endpoint_receive_timeout);
endpoint_await(Ep, N) ->
    case erts_internal:realm_endpoint_receive(Ep) of
        {ok, Binary} -> Binary;
        empty -> receive after 1 -> ok end, endpoint_await(Ep, N - 1);
        closed -> ct:fail(endpoint_closed)
    end.

empty_realm_lifecycle(_Config) ->
    ?assertError(badarg, erts_internal:realm_create(#{unknown => true})),
    Realm = erts_internal:realm_create(#{}),
    try
        Id = erts_internal:realm_identity(Realm),
        true = Id =/= erts_internal:realm_identity(),
        ?assertError(badarg, erts_internal:realm_stop_members(Realm)),
        ok = erts_internal:realm_close(Realm),
        done = erts_internal:realm_stop_members(Realm),
        Id = erts_internal:realm_identity(Realm),
        ?assertError(system_limit,
                     erts_internal:realm_spawn_root(Realm, ?MODULE, wait_identity, []))
    after
        ok = erts_internal:realm_stop(Realm)
    end,
    ok = in_realm(fun() ->
        ?assertError(badarg, erts_internal:realm_create(#{})),
        ?assertError(badarg, erts_internal:realm_create(#{allow_create_realms => true})),
        ok
    end).

root_authority(_Config) ->
    {Ancestor, {Child, First}} = in_realm_with_handle(fun() ->
        C = erts_internal:realm_create(#{}),
        P = erts_internal:realm_spawn_root(C, ?MODULE, wait_identity, []),
        {C, P}
    end, #{allow_create_realms => true}),
    try
        %% Host management can start a descendant even after its creator exits.
        Second = erts_internal:realm_spawn_root(Child, ?MODULE, wait_identity, []),
        lists:foreach(fun(Policy) ->
            ok = in_policy(Policy, fun() ->
                ?assertError(badarg,
                             erts_internal:realm_spawn_root(Child, ?MODULE, wait_identity, [])),
                ok
            end)
        end, [#{}, #{allow_create_realms => true}]),
        ?assertError(badarg,
                     erts_internal:realm_spawn_root(erts_internal:realm_identity(Child),
                                                   ?MODULE, wait_identity, [])),
        ok = erts_internal:realm_stop(Ancestor),
        false = is_process_alive(First),
        false = is_process_alive(Second)
    after
        ok = erts_internal:realm_stop(Ancestor)
    end.

prepared_quota_rollback(_Config) ->
    ok = in_policy(#{allow_create_realms => true, max_realms => 2,
                     max_processes => 1}, fun() ->
        Child = erts_internal:realm_create(#{}),
        ?assertError(system_limit,
                     erts_internal:realm_spawn_root(Child, ?MODULE, wait_identity, [])),
        ?assertError(system_limit, erts_internal:realm_create(#{})),
        %% Failed root admission must not leak a pending reservation.
        ok = erts_internal:realm_stop(Child),
        done = erts_internal:realm_stop_members(Child),
        ok
    end).

endpoint_exchange(_Config) ->
    endpoint_fixture(fun(Realm, Root, Ref) ->
        In = erts_internal:realm_endpoint_create(Realm, to_realm, 4, 100),
        Out = erts_internal:realm_endpoint_create(Realm, to_host, 4, 100),
        <<_:3, Unaligned:2/binary, _:5>> = <<0:3, 42, 17, 0:5>>,
        ok = erts_internal:realm_endpoint_send(In, Unaligned),
        ok = erts_internal:realm_endpoint_send(In, <<>>),
        Root ! {Ref, fun() ->
            {ok, <<42, 17>>} = erts_internal:realm_endpoint_receive(In),
            {ok, <<>>} = erts_internal:realm_endpoint_receive(In),
            empty = erts_internal:realm_endpoint_receive(In),
            {P, M} = spawn_monitor(fun() ->
                ok = erts_internal:realm_endpoint_send(Out, <<99>>)
            end),
            await_exit(P, M)
        end},
        expect_ok(Ref),
        {ok, <<99>>} = erts_internal:realm_endpoint_receive(Out),
        empty = erts_internal:realm_endpoint_receive(Out)
    end).

endpoint_bounds(_Config) ->
    endpoint_fixture(fun(Realm, Root, Ref) ->
        lists:foreach(fun({Direction, Count, Bytes}) ->
            ?assertError(badarg,
                         erts_internal:realm_endpoint_create(Realm, Direction, Count, Bytes))
        end, [{bad, 1, 1}, {to_host, 0, 1}, {to_host, 1025, 1},
              {to_realm, 1, 0}, {to_realm, 1, 1048577}, {to_host, 1.0, 1}]),
        In = erts_internal:realm_endpoint_create(Realm, to_realm, 2, 3),
        LargeIn = erts_internal:realm_endpoint_create(Realm, to_realm, 1, 65536),
        Large = binary:copy(<<42>>, 65536),
        ok = erts_internal:realm_endpoint_send(LargeIn, Large),
        full = erts_internal:realm_endpoint_send(LargeIn, <<>>),
        lists:foreach(fun(Payload) ->
            ?assertError(badarg, erts_internal:realm_endpoint_send(In, Payload))
        end, [[1], self(), make_ref(), fun() -> ok end, <<1:1>>,
              binary:copy(<<0>>, 65537)]),
        ok = erts_internal:realm_endpoint_send(In, <<1, 2>>),
        full = erts_internal:realm_endpoint_send(In, <<3, 4>>),
        ok = erts_internal:realm_endpoint_send(In, <<3>>),
        full = erts_internal:realm_endpoint_send(In, <<>>),
        Root ! {Ref, fun() ->
            {ok, Large} = erts_internal:realm_endpoint_receive(LargeIn),
            {ok, <<1, 2>>} = erts_internal:realm_endpoint_receive(In),
            {ok, <<3>>} = erts_internal:realm_endpoint_receive(In),
            empty = erts_internal:realm_endpoint_receive(In),
            ok
        end},
        expect_ok(Ref),
        %% Both byte and message charges are reusable after consumption.
        ok = erts_internal:realm_endpoint_send(In, <<4, 5, 6>>),
        ok = erts_internal:realm_endpoint_send(In, <<>>),
        full = erts_internal:realm_endpoint_send(In, <<>>)
    end).

endpoint_authority(_Config) ->
    endpoint_fixture(fun(Realm, Root, Ref) ->
        In = erts_internal:realm_endpoint_create(Realm, to_realm, 2, 100),
        Out = erts_internal:realm_endpoint_create(Realm, to_host, 2, 100),
        ?assertError(badarg, erts_internal:realm_endpoint_send(Out, <<>>)),
        ?assertError(badarg, erts_internal:realm_endpoint_receive(In)),
        ?assertError(badarg, erts_internal:realm_close(In)),
        lists:foreach(fun(Value) ->
            ?assertError(badarg, erts_internal:realm_endpoint_send(Value, <<>>)),
            ?assertError(badarg, erts_internal:realm_endpoint_receive(Value)),
            ?assertError(badarg, erts_internal:realm_endpoint_revoke(Value))
        end, [Realm, erts_internal:realm_identity(Realm), make_ref(), atomics:new(1, [])]),
        ok = in_realm(fun() ->
            lists:foreach(fun(Ep) ->
                ?assertError(badarg, erts_internal:realm_endpoint_send(Ep, <<>>)),
                ?assertError(badarg, erts_internal:realm_endpoint_receive(Ep)),
                ?assertError(badarg, erts_internal:realm_endpoint_revoke(Ep))
            end, [In, Out]),
            ok
        end),
        ok = in_policy(#{allow_create_realms => true}, fun() ->
            {Child, _} = erts_internal:realm_spawn(?MODULE, wait_identity, []),
            ?assertError(badarg,
                         erts_internal:realm_endpoint_create(Child, to_host, 1, 1)),
            ok
        end),
        Root ! {Ref, fun() ->
            ?assertError(badarg, erts_internal:realm_endpoint_send(In, <<>>)),
            ?assertError(badarg, erts_internal:realm_endpoint_receive(Out)),
            ?assertError(badarg, erts_internal:realm_endpoint_revoke(Out)),
            ok = erts_internal:realm_endpoint_send(Out, <<1>>)
        end},
        expect_ok(Ref),
        {ok, <<1>>} = erts_internal:realm_endpoint_receive(Out)
    end).

endpoint_revocation(_Config) ->
    endpoint_fixture(fun(Realm, Root, Ref) ->
        A = erts_internal:realm_endpoint_create(Realm, to_host, 128, 1024),
        B = erts_internal:realm_endpoint_create(Realm, to_host, 1, 10),
        In = erts_internal:realm_endpoint_create(Realm, to_realm, 1, 10),
        ok = erts_internal:realm_endpoint_send(In, <<1>>),
        ok = erts_internal:realm_endpoint_revoke(In),
        closed = erts_internal:realm_endpoint_send(In, <<2>>),
        Root ! {Ref, fun() ->
            closed = erts_internal:realm_endpoint_receive(In),
            lists:foreach(fun(_) ->
                ok = erts_internal:realm_endpoint_send(A, <<42>>)
            end, lists:seq(1, 100)),
            ok = erts_internal:realm_endpoint_send(B, <<17>>)
        end},
        expect_ok(Ref),
        ok = erts_internal:realm_endpoint_revoke(A),
        ok = erts_internal:realm_endpoint_revoke(A),
        closed = erts_internal:realm_endpoint_receive(A),
        {ok, <<17>>} = erts_internal:realm_endpoint_receive(B)
    end).

endpoint_subtree_stop(_Config) ->
    {Ancestor, Child} = in_realm_with_handle(fun() ->
        {H, ok} = in_realm_with_handle(fun() -> ok end),
        H
    end, #{allow_create_realms => true}),
    try
        In = erts_internal:realm_endpoint_create(Child, to_realm, 1024, 1024),
        lists:foreach(fun(_) ->
            ok = erts_internal:realm_endpoint_send(In, <<>>)
        end, lists:seq(1, 1000)),
        ok = erts_internal:realm_close(Ancestor),
        %% Queued work keeps an otherwise empty child in the active tree.
        true = is_list(erts_internal:realm_stop_members(Child)),
        [_] = erts_internal:realm_stop_children(Ancestor),
        ok = erts_internal:realm_stop(Ancestor),
        [] = erts_internal:realm_stop_children(Ancestor),
        done = erts_internal:realm_stop_members(Child),
        closed = erts_internal:realm_endpoint_send(In, <<>>),
        closed = erts_internal:realm_endpoint_create(Child, to_realm, 1, 1)
    after
        ok = erts_internal:realm_stop(Ancestor)
    end.

endpoint_quota(_Config) ->
    {Ancestor, {A, B}} = in_realm_with_handle(fun() ->
        {AH, _} = erts_internal:realm_spawn(?MODULE, wait_identity, []),
        {BH, _} = erts_internal:realm_spawn(?MODULE, wait_identity, []),
        {AH, BH}
    end, #{allow_create_realms => true}),
    try
        Endpoints = [erts_internal:realm_endpoint_create(H, to_host, 1, 1)
                     || H <- [A, B], _ <- lists:seq(1, 32)],
        ?assertError(system_limit,
                     erts_internal:realm_endpoint_create(A, to_host, 1, 1)),
        lists:foreach(fun(Ep) -> ok = erts_internal:realm_endpoint_revoke(Ep) end,
                      Endpoints),
        garbage_collect(),
        ?assertError(system_limit,
                     erts_internal:realm_endpoint_create(B, to_host, 1, 1)),
        64 = length(Endpoints)
    after
        ok = erts_internal:realm_stop(Ancestor)
    end.

endpoint_reclamation(_Config) ->
    endpoint_fixture(fun(Realm, _Root, _Ref) ->
        drop_endpoints(Realm),
        Endpoint = retry_endpoint_create(Realm, 1000),
        true = is_reference(Endpoint)
    end).

drop_endpoints(Realm) ->
    lists:foreach(fun(_) ->
        Ep = erts_internal:realm_endpoint_create(Realm, to_realm, 2, 2),
        ok = erts_internal:realm_endpoint_send(Ep, <<1>>)
    end, lists:seq(1, 64)).

retry_endpoint_create(_Realm, 0) -> ct:fail(endpoint_quota_not_reclaimed);
retry_endpoint_create(Realm, N) ->
    garbage_collect(),
    try erts_internal:realm_endpoint_create(Realm, to_realm, 1, 1) of
        Ep -> Ep
    catch error:system_limit ->
        receive after 1 -> ok end,
        retry_endpoint_create(Realm, N - 1)
    end.

endpoint_stop_race(_Config) ->
    lists:foreach(fun(_) ->
        endpoint_fixture(fun(Realm, _Root, _Ref) ->
            In = erts_internal:realm_endpoint_create(Realm, to_realm, 128, 65536),
            Parent = self(),
            Ready = make_ref(),
            Workers = [spawn_monitor(fun() ->
                ok = erts_internal:realm_endpoint_send(In, <<1>>),
                Parent ! Ready,
                receive go -> ok end,
                endpoint_flood(In)
            end) || _ <- lists:seq(1, 2)],
            lists:foreach(fun(_) -> receive Ready -> ok
                                   after 5000 -> ct:fail(endpoint_race_timeout)
                                   end end, Workers),
            lists:foreach(fun({P, _}) -> P ! go end, Workers),
            ok = erts_internal:realm_stop(Realm),
            lists:foreach(fun({P, M}) -> await_exit(P, M) end, Workers),
            done = erts_internal:realm_stop_members(Realm)
        end)
    end, lists:seq(1, 20)).

endpoint_flood(In) ->
    case erts_internal:realm_endpoint_send(In, <<1>>) of
        closed -> ok;
        full -> receive after 1 -> ok end, endpoint_flood(In);
        ok -> endpoint_flood(In)
    end.

endpoint_fixture(Fun) ->
    Ref = make_ref(),
    {Realm, Root} = erts_internal:realm_spawn(?MODULE, realm_entry, [self(), Ref]),
    try Fun(Realm, Root, Ref)
    after ok = erts_internal:realm_stop(Realm)
    end.

opaque_identity(_Config) ->
    Host = erts_internal:realm_identity(),
    true = is_reference(Host),
    Parent = self(),
    Ref = make_ref(),
    Workers = [spawn_monitor(fun() ->
        Parent ! {Ref, erts_internal:realm_identity()}
    end) || _ <- lists:seq(1, 32)],
    lists:foreach(fun({P, M}) ->
        receive {Ref, Host} -> ok after 5000 -> ct:fail(identity_mismatch) end,
        await_exit(P, M)
    end, Workers),
    Ids = [in_realm(fun() ->
        Id = erts_internal:realm_identity(),
        Owner = self(),
        {P, M} = spawn_monitor(fun() ->
            Owner ! {Ref, erts_internal:realm_identity()}
        end),
        receive {Ref, Id} -> ok after 5000 -> ct:fail(identity_inheritance) end,
        await_exit(P, M),
        garbage_collect(),
        Id = erts_internal:realm_identity()
    end) || _ <- lists:seq(1, 32)],
    33 = map_size(maps:from_list([{Id, true} || Id <- [Host | Ids]])),
    garbage_collect(),
    Host = erts_internal:realm_identity(),
    ok.

canonical_identity(_Config) ->
    {Parent, Child} = in_realm_with_handle(fun() ->
        {H, _} = erts_internal:realm_spawn(?MODULE, wait_identity, []),
        H
    end, #{allow_create_realms => true}),
    try
        Id = erts_internal:realm_identity(Child),
        true = Id =/= erts_internal:realm_identity(Parent),
        ok = erts_internal:realm_close(Parent),
        [Snapshot1] = erts_internal:realm_stop_children(Parent),
        [Snapshot2] = erts_internal:realm_stop_children(Parent),
        true = Snapshot1 =/= Snapshot2,
        Id = erts_internal:realm_identity(Snapshot1),
        Id = erts_internal:realm_identity(Snapshot2),
        ok = erts_internal:realm_stop(Parent),
        Id = erts_internal:realm_identity(Child),
        Id = erts_internal:realm_identity(Snapshot1)
    after
        ok = erts_internal:realm_stop(Parent)
    end,
    ok.

identity_authority(_Config) ->
    {Realm, Root} = erts_internal:realm_spawn(?MODULE, wait_identity, []),
    try
        Id = erts_internal:realm_identity(Realm),
        ok = in_realm(fun() ->
            %% Identification of an already-held handle grants no authority.
            Id = erts_internal:realm_identity(Realm),
            ?assertError(badarg, erts_internal:realm_close(Realm)),
            lists:foreach(fun(Value) ->
                ?assertError(badarg, erts_internal:realm_identity(Value)),
                ?assertError(badarg, erts_internal:realm_close(Value)),
                ?assertError(badarg, erts_internal:realm_stop_children(Value)),
                ?assertError(badarg, erts_internal:realm_stop_members(Value))
            end, [Id, make_ref(), atomics:new(1, []), 0, {realm, 1}]),
            ok
        end),
        %% Even host authority cannot turn an identity into a management handle.
        ?assertError(badarg, erts_internal:realm_stop(Id)),
        true = is_process_alive(Root)
    after
        ok = erts_internal:realm_stop(Realm)
    end.

identity_reclamation(_Config) ->
    {Old, New} = in_policy(#{allow_create_realms => true, max_realms => 2}, fun() ->
        %% Only keep the child's identity, not its management handle. It must
        %% not retain the child metadata or consume the creator's Realm quota.
        Id = in_realm(fun erts_internal:realm_identity/0),
        {Next, _} = retry_realm_spawn(1000),
        NextId = erts_internal:realm_identity(Next),
        ok = erts_internal:realm_stop(Next),
        {Id, NextId}
    end),
    garbage_collect(),
    true = is_reference(Old),
    true = Old =/= New,
    ?assertError(badarg, erts_internal:realm_close(Old)),
    #{Old := old, New := new} = #{Old => old, New => new},
    ok.

host_identity(_Config) ->
    0 = erts_internal:realm_id(),
    Ref = make_ref(),
    spawn(?MODULE, report_identity, [self(), Ref]),
    0 = receive_identity(Ref),
    ok.

unique_roots(_Config) ->
    Parent = self(),
    Workers = [spawn_monitor(fun() ->
                                Id = in_realm(fun erts_internal:realm_id/0),
                                Parent ! {self(), Id}
                            end) || _ <- lists:seq(1, 32)],
    try
        Ids = [receive
                   {Pid, Id} when is_integer(Id), Id > 0 -> Id
               after 10000 -> ct:fail({missing_realm, Pid})
               end || {Pid, _} <- Workers],
        32 = length(lists:usort(Ids)),
        lists:foreach(fun({Pid, Mon}) -> await_exit(Pid, Mon) end, Workers)
    after
        lists:foreach(fun({Pid, Mon}) ->
                          erlang:exit_signal(Pid, kill),
                          demonitor(Mon, [flush])
                      end, Workers)
    end.

spawn_inheritance(_Config) ->
    ok = in_realm(fun() ->
        Id = erts_internal:realm_id(),
        true = Id > 0,
        Self = self(),
        Self = group_leader(),
        Ref = make_ref(),
        Report = fun() -> report_identity(Self, Ref) end,
        spawn(Report),
        spawn(?MODULE, report_identity, [Self, Ref]),
        spawn_link(Report),
        spawn_link(?MODULE, report_identity, [Self, Ref]),
        {_, Mon1} = spawn_monitor(Report),
        {_, Mon2} = spawn_monitor(?MODULE, report_identity, [Self, Ref]),
        spawn_opt(Report, [{message_queue_data, off_heap}]),
        spawn_opt(?MODULE, report_identity, [Self, Ref], []),
        Req = spawn_request(?MODULE, report_identity, [Self, Ref], []),
        receive
            {spawn_reply, Req, ok, _Pid} -> ok
        after 5000 -> ct:fail(spawn_request_timeout)
        end,
        lists:foreach(fun(_) -> Id = receive_identity(Ref) end,
                      lists:seq(1, 9)),
        demonitor(Mon1, [flush]),
        demonitor(Mon2, [flush]),
        ok
    end).

denied_root_creation(_Config) ->
    ok = in_realm(fun() ->
        ?assertError(badarg,
                     erts_internal:realm_spawn(?MODULE, wait_identity, [])),
        ?assertError(badarg,
                     erts_internal:realm_spawn(?MODULE, wait_identity, [],
                                               #{allow_create_realms => true})),
        ?assertError(badarg,
                     erts_internal:realm_spawn(?MODULE, wait_identity, [], #{})),
        ?assertError(badarg,
                     erts_internal:spawn_system_process(?MODULE, wait_identity, [])),
        ok
    end).

invalid_arguments(_Config) ->
    lists:foreach(fun({M, F, A}) ->
                      ?assertError(badarg, erts_internal:realm_spawn(M, F, A))
                  end,
                  [{42, wait_identity, []},
                   {?MODULE, 42, []},
                   {?MODULE, wait_identity, not_a_list},
                   {?MODULE, wait_identity, [a | b]}]),
    0 = erts_internal:realm_id(),
    true = in_realm(fun erts_internal:realm_id/0) > 0,
    ok.

immutable_membership(_Config) ->
    ok = in_realm(fun() ->
        Id = erts_internal:realm_id(),
        ?assertError(badarg, process_flag(realm, 0)),
        ?assertError(badarg, spawn_opt(fun() -> ok end, [{realm, 0}])),
        Id = erts_internal:realm_id(),
        ok
    end).

surviving_child(_Config) ->
    {Realm, {Id, Child}} = in_realm_with_handle(fun() ->
        {erts_internal:realm_id(), spawn(?MODULE, wait_identity, [])}
    end),
    try
        Ref = make_ref(),
        Child ! {identity, self(), Ref},
        Id = receive_identity(Ref)
    after
        ok = erts_internal:realm_stop(Realm)
    end,
    false = is_process_alive(Child),
    done = erts_internal:realm_stop_members(Realm).

dirty_execution(_Config) ->
    ok = in_realm(fun() ->
        Id = erts_internal:realm_id(),
        lists:foreach(fun(_) ->
            dirty_cpu = erts_debug:dirty_cpu(scheduler, type),
            dirty_io = erts_debug:dirty_io(scheduler, type),
            Id = erts_internal:realm_id()
        end, lists:seq(1, 20))
    end).

invalid_handles(_Config) ->
    Alias = alias(),
    try
        lists:foreach(fun(Handle) ->
            ?assertError(badarg, erts_internal:realm_close(Handle)),
            ?assertError(badarg, erts_internal:realm_stop_members(Handle)),
            ?assertError(badarg, erts_internal:realm_stop(Handle))
        end, [0, self(), make_ref(), Alias, atomics:new(1, [])]),
        {Realm, Root} = erts_internal:realm_spawn(?MODULE, wait_identity, []),
        try
            ?assertError(badarg, erts_internal:realm_stop_members(Realm)),
            true = is_process_alive(Root)
        after
            ok = erts_internal:realm_stop(Realm)
        end
    after
        unalias(Alias)
    end.

denied_management(_Config) ->
    {Realm, Root} = erts_internal:realm_spawn(?MODULE, wait_identity, []),
    try
        ok = in_realm(fun() ->
            ?assertError(badarg, erts_internal:realm_close(Realm)),
            ?assertError(badarg, erts_internal:realm_stop_members(Realm)),
            ?assertError(badarg, erts_internal:realm_stop(Realm)),
            ok
        end),
        true = is_process_alive(Root)
    after
        ok = erts_internal:realm_stop(Realm)
    end.

close_admission(_Config) ->
    Ref = make_ref(),
    {Realm, Root} = erts_internal:realm_spawn(?MODULE, realm_entry, [self(), Ref]),
    Mon = monitor(process, Root),
    try
        ok = erts_internal:realm_close(Realm),
        ok = erts_internal:realm_close(Realm),
        Root ! {Ref, fun() ->
            ?assertError(system_limit, spawn(fun() -> ok end)),
            ?assertError(system_limit, spawn(?MODULE, wait_identity, [])),
            ?assertError(system_limit, spawn_link(fun() -> ok end)),
            ?assertError(system_limit, spawn_monitor(fun() -> ok end)),
            ?assertError(system_limit, spawn_opt(fun() -> ok end, [monitor])),
            Req = spawn_request(?MODULE, wait_identity, []),
            receive {spawn_reply, Req, error, system_limit} -> ok
            after 5000 -> ct:fail(closed_spawn_request_timeout)
            end
        end},
        receive {Ref, {ok, ok}} -> ok;
                {Ref, Error} -> ct:fail(Error)
        after 5000 -> ct:fail(closed_admission_timeout)
        end,
        await_exit(Root, Mon),
        ok = erts_internal:realm_stop(Realm),
        done = erts_internal:realm_stop_members(Realm)
    after
        ok = erts_internal:realm_stop(Realm),
        demonitor(Mon, [flush])
    end.

stop_tree(_Config) ->
    {Other, OtherRoot} = erts_internal:realm_spawn(?MODULE, wait_identity, []),
    {Realm, Root, Children} = start_tree(130),
    try
        ok = erts_internal:realm_close(Realm),
        64 = length(erts_internal:realm_stop_members(Realm)),
        ok = erts_internal:realm_stop(Realm),
        lists:foreach(fun(Pid) -> false = is_process_alive(Pid) end,
                      [Root | Children]),
        done = erts_internal:realm_stop_members(Realm),
        ok = erts_internal:realm_stop(Realm),
        true = is_process_alive(OtherRoot)
    after
        ok = erts_internal:realm_stop(Realm),
        ok = erts_internal:realm_stop(Other)
    end.

concurrent_stop(_Config) ->
    {Realm, _Root, _Children} = start_tree(130),
    Workers = [spawn_monitor(fun() -> ok = erts_internal:realm_stop(Realm) end)
               || _ <- lists:seq(1, 4)],
    try
        lists:foreach(fun({Pid, Mon}) -> await_exit(Pid, Mon) end, Workers),
        done = erts_internal:realm_stop_members(Realm)
    after
        lists:foreach(fun({Pid, Mon}) ->
            erlang:exit_signal(Pid, kill),
            demonitor(Mon, [flush])
        end, Workers),
        ok = erts_internal:realm_stop(Realm)
    end.

spawn_close_race(_Config) ->
    lists:foreach(fun(_) ->
        Parent = self(),
        Ref = make_ref(),
        {Realm, _Root} = erts_internal:realm_spawn(erlang, apply, [fun() ->
            [spawn(fun() -> spawn_racer(Parent, Ref, 10000) end)
             || _ <- lists:seq(1, 4)],
            Parent ! {Ref, ready},
            wait_identity()
        end, []]),
        try
            receive {Ref, ready} -> ok
            after 5000 -> ct:fail(racers_not_ready)
            end,
            ok = erts_internal:realm_close(Realm),
            lists:foreach(fun(_) ->
                receive {Ref, closed} -> ok;
                        {Ref, exhausted} -> ct:fail(racer_exhausted)
                after 5000 -> ct:fail(racer_not_closed)
                end
            end, lists:seq(1, 4)),
            ok = erts_internal:realm_stop(Realm),
            done = erts_internal:realm_stop_members(Realm)
        after
            ok = erts_internal:realm_stop(Realm)
        end
    end, lists:seq(1, 10)).

spawn_racer(Parent, Ref, 0) ->
    Parent ! {Ref, exhausted};
spawn_racer(Parent, Ref, N) ->
    try spawn(?MODULE, wait_identity, []) of
        _ -> spawn_racer(Parent, Ref, N - 1)
    catch error:system_limit -> Parent ! {Ref, closed}
    end.

handle_lifetime(_Config) ->
    Parent = self(),
    {Creator, Mon} = spawn_monitor(fun() ->
        Pair = erts_internal:realm_spawn(?MODULE, wait_identity, []),
        Parent ! {self(), Pair}
    end),
    receive {Creator, {Realm, Root}} ->
        try
            await_exit(Creator, Mon),
            garbage_collect(),
            true = is_process_alive(Root),
            ok = erts_internal:realm_stop(Realm),
            garbage_collect(),
            done = erts_internal:realm_stop_members(Realm),
            ok = erts_internal:realm_stop(Realm)
        after
            ok = erts_internal:realm_stop(Realm)
        end
    after 5000 -> ct:fail(handle_transfer_timeout)
    end.

resumable_stop(_Config) ->
    {Realm, _Root, Children} = start_tree(130),
    Parent = self(),
    {Stopper, Mon} = spawn_monitor(fun() ->
        ok = erts_internal:realm_close(Realm),
        Batch = erts_internal:realm_stop_members(Realm),
        lists:foreach(fun(Pid) -> erlang:exit_signal(Pid, kill) end, Batch),
        Parent ! {partial_stop, self()},
        receive continue -> erts_internal:realm_stop(Realm) end
    end),
    try
        receive {partial_stop, Stopper} -> ok
        after 5000 -> ct:fail(partial_stop_timeout)
        end,
        erlang:exit_signal(Stopper, kill),
        receive {'DOWN', Mon, process, Stopper, killed} -> ok
        after 5000 -> ct:fail(stopper_exit_timeout)
        end,
        ok = erts_internal:realm_stop(Realm),
        done = erts_internal:realm_stop_members(Realm),
        lists:foreach(fun(Pid) -> false = is_process_alive(Pid) end, Children)
    after
        erlang:exit_signal(Stopper, kill),
        demonitor(Mon, [flush]),
        ok = erts_internal:realm_stop(Realm)
    end.

dirty_shutdown(_Config) ->
    {Realm, Root} = erts_internal:realm_spawn(erts_debug, dirty_io,
                                            [ready_wait6_done, self()]),
    try
        receive {ready, Root} -> ok
        after 5000 -> ct:fail(dirty_start_timeout)
        end,
        ok = erts_internal:realm_stop(Realm),
        %% The native helper sends 'done' before returning, even after an exit
        %% signal. Stop must wait for its deferred process cleanup, not just DOWN.
        receive {done, Root} -> ok
        after 0 -> ct:fail(premature_dirty_shutdown)
        end,
        done = erts_internal:realm_stop_members(Realm)
    after
        ok = erts_internal:realm_stop(Realm)
    end.

allocation_failure(_Config) ->
    %% Exhaust a separate VM's process table, not the Common Test VM.
    Dir = filename:dirname(code:which(?MODULE)),
    Args = ["+P", "1024", "-pa", Dir,
            "-emu_type", atom_to_list(erlang:system_info(build_type)),
            "-emu_flavor", atom_to_list(erlang:system_info(emu_flavor))],
    {ok, Peer, _} = peer:start_link(#{connection => standard_io, args => Args}),
    try
        ok = peer:call(Peer, ?MODULE, fill_process_table, [], 20000)
    after
        peer:stop(Peer)
    end.

fill_process_table() ->
    Fill = fun Loop(N) ->
        try spawn(?MODULE, wait_identity, []) of
            _ -> Loop(N + 1)
        catch error:system_limit -> N
        end
    end,
    Parent = self(),
    Ref = make_ref(),
    {Realm, _Root} = erts_internal:realm_spawn(erlang, apply, [fun() ->
        Parent ! {Ref, Fill(0)},
        wait_identity()
    end, []]),
    try
        receive {Ref, Count} -> true = Count > 100
        after 5000 -> ct:fail(process_table_not_full)
        end,
        %% Also fail creation of a fresh Realm's root. Fill any slots freed by
        %% transient host processes so this exercises constructor rollback.
        ExtraRealms = exhaust_root_creation([]),
        try
            %% A leaked admission reservation would make stop never complete.
            ok = erts_internal:realm_stop(Realm),
            done = erts_internal:realm_stop_members(Realm)
        after
            lists:foreach(fun(H) -> ok = erts_internal:realm_stop(H) end,
                          ExtraRealms)
        end
    after
        ok = erts_internal:realm_stop(Realm)
    end,
    ok = in_realm(fun() -> ok end).

exhaust_root_creation(Handles) ->
    try erts_internal:realm_spawn(?MODULE, wait_identity, []) of
        {Handle, _Root} -> exhaust_root_creation([Handle | Handles])
    catch error:system_limit -> Handles
    end.

start_tree(Count) ->
    Parent = self(),
    Ref = make_ref(),
    {Realm, Root} = erts_internal:realm_spawn(erlang, apply, [fun() ->
        Children = [spawn(fun() ->
            process_flag(trap_exit, true),
            wait_identity()
        end) || _ <- lists:seq(1, Count)],
        Parent ! {Ref, Children},
        wait_identity()
    end, []]),
    receive {Ref, Children} -> {Realm, Root, Children}
    after 5000 ->
        ok = erts_internal:realm_stop(Realm),
        ct:fail(tree_start_timeout)
    end.

policy_validation(_Config) ->
    lists:foreach(fun(Policy) ->
        ?assertError(badarg,
                     erts_internal:realm_spawn(?MODULE, wait_identity, [], Policy))
    end, [[], true, #{unknown => true}, #{allow_create_realms => 1},
          #{max_realms => 0}, #{max_realms => -1}, #{max_processes => 1.5},
          #{max_processes => 0}, #{max_realms => 1 bsl 100}]),
    ok = in_policy(#{allow_create_realms => true, max_realms => 4,
                     max_processes => 8}, fun() ->
        lists:foreach(fun(Policy) ->
            ?assertError(badarg,
                         erts_internal:realm_spawn(?MODULE, wait_identity, [], Policy))
        end, [#{max_realms => 5}, #{max_processes => 9},
              #{max_realms => infinity}, #{max_processes => infinity}]),
        ?assertError(badarg, process_flag(allow_create_realms, true)),
        ok = in_policy(#{allow_create_realms => false, max_realms => 1,
                         max_processes => 1}, fun() ->
            ?assertError(system_limit, spawn(fun() -> ok end)),
            ?assertError(badarg,
                         erts_internal:realm_spawn(?MODULE, wait_identity, [],
                                                   #{allow_create_realms => true})),
            ok
        end)
    end).

delegated_creation(_Config) ->
    ok = in_policy(#{allow_create_realms => true, max_realms => infinity,
                     max_processes => infinity}, fun() ->
        ManagerId = erts_internal:realm_id(),
        ?assertError(badarg,
                     erts_internal:spawn_system_process(?MODULE, wait_identity, [])),
        ChildId = in_realm(fun() ->
            ?assertError(badarg, erts_internal:realm_spawn(?MODULE, wait_identity, [])),
            erts_internal:realm_id()
        end),
        true = ManagerId =/= ChildId,
        ok = in_policy(#{allow_create_realms => true}, fun() ->
            ok = in_realm(fun() -> ok end)
        end),
        %% Ordinary children keep the manager's identity and authority.
        Parent = self(),
        Ref = make_ref(),
        {Worker, Mon} = spawn_monitor(fun() ->
            ManagerId = erts_internal:realm_id(),
            Parent ! {Ref, in_realm(fun() -> ok end)}
        end),
        receive {Ref, ok} -> ok after 5000 -> ct:fail(delegation_timeout) end,
        await_exit(Worker, Mon)
    end).

management_scope(_Config) ->
    Ref = make_ref(),
    Host = self(),
    {Ancestor, {Child, ChildPid, Sibling}} =
        in_realm_with_handle(fun() ->
            {H, P} = erts_internal:realm_spawn(?MODULE, realm_entry, [Host, Ref],
                                             #{allow_create_realms => true}),
            {S, _} = erts_internal:realm_spawn(?MODULE, wait_identity, []),
            {H, P, S}
        end, #{allow_create_realms => true}),
    {Other, OtherPid} = erts_internal:realm_spawn(?MODULE, wait_identity, []),
    try
        ChildPid ! {Ref, fun() ->
            lists:foreach(fun(H) ->
                ?assertError(badarg, erts_internal:realm_close(H)),
                ?assertError(badarg, erts_internal:realm_stop_children(H)),
                ?assertError(badarg, erts_internal:realm_stop_members(H))
            end, [Ancestor, Child, Sibling, Other]),
            ok = in_realm(fun() -> ok end)
        end},
        expect_ok(Ref),
        ok = erts_internal:realm_stop(Ancestor),
        true = is_process_alive(OtherPid)
    after
        ok = erts_internal:realm_stop(Ancestor),
        ok = erts_internal:realm_stop(Other)
    end.

subtree_shutdown(_Config) ->
    {Ancestor, {Pairs, Grandchild}} = in_realm_with_handle(fun() ->
        Pairs0 = [erts_internal:realm_spawn(?MODULE, wait_identity, [])
                  || _ <- lists:seq(1, 70)],
        %% Both ancestor roots exit before the host shuts down the subtree.
        {_Child, {_Leaf, LeafPid}} = in_realm_with_handle(fun() ->
            erts_internal:realm_spawn(?MODULE, wait_identity, [])
        end, #{allow_create_realms => true}),
        {Pairs0, LeafPid}
    end, #{allow_create_realms => true}),
    try
        ok = erts_internal:realm_close(Ancestor),
        64 = length(erts_internal:realm_stop_children(Ancestor)),
        ok = erts_internal:realm_stop(Ancestor),
        lists:foreach(fun({_H, P}) -> false = is_process_alive(P) end, Pairs),
        false = is_process_alive(Grandchild),
        done = erts_internal:realm_stop_members(Ancestor)
    after
        ok = erts_internal:realm_stop(Ancestor)
    end.

subtree_limits(_Config) ->
    ok = in_policy(#{allow_create_realms => true, max_realms => 4,
                     max_processes => 4}, fun() ->
        {A, APid} = erts_internal:realm_spawn(erlang, apply, [fun spawn_server/0, []]),
        {B, BPid} = erts_internal:realm_spawn(erlang, apply, [fun spawn_server/0, []]),
        {ok, _} = server_spawn(APid),
        {error, system_limit} = server_spawn(BPid),
        ?assertError(system_limit, spawn(fun() -> ok end)),
        ?assertError(system_limit, spawn_link(fun() -> ok end)),
        ?assertError(system_limit, spawn_monitor(fun() -> ok end)),
        ?assertError(system_limit, spawn_opt(fun() -> ok end, [monitor])),
        Req = spawn_request(?MODULE, wait_identity, []),
        receive {spawn_reply, Req, error, system_limit} -> ok
        after 5000 -> ct:fail(quota_spawn_request_timeout)
        end,
        %% Metadata admission succeeds, but root admission must roll it back.
        ?assertError(system_limit, erts_internal:realm_spawn(?MODULE, wait_identity, [])),
        ok = erts_internal:realm_stop(B),
        {C, _} = erts_internal:realm_spawn(?MODULE, wait_identity, []),
        ok = erts_internal:realm_stop(A),
        ok = erts_internal:realm_stop(C),
        ok = erts_internal:realm_stop(B)
    end).

spawn_server() ->
    receive {spawn, From, Ref} ->
        Result = try spawn(?MODULE, wait_identity, []) of
                     Pid -> {ok, Pid}
                 catch error:system_limit -> {error, system_limit}
                 end,
        From ! {Ref, Result},
        spawn_server()
    after 10000 -> exit(fixture_timeout)
    end.

server_spawn(Pid) ->
    Ref = make_ref(),
    Pid ! {spawn, self(), Ref},
    receive {Ref, Result} -> Result
    after 5000 -> ct:fail(server_spawn_timeout)
    end.

retained_realm_limit(_Config) ->
    ok = in_policy(#{allow_create_realms => true, max_realms => 2}, fun() ->
        {Child, _} = erts_internal:realm_spawn(?MODULE, wait_identity, []),
        ok = erts_internal:realm_stop(Child),
        put(retained_realm, Child),
        garbage_collect(),
        %% Retaining closed handles must not evade the Realm metadata budget.
        ?assertError(system_limit, erts_internal:realm_spawn(?MODULE, wait_identity, [])),
        Child = erase(retained_realm),
        ok = erts_internal:realm_stop(Child)
    end).

quota_reclamation(_Config) ->
    ok = in_policy(#{allow_create_realms => true, max_realms => 2}, fun() ->
        ok = in_realm(fun() -> ok end),
        {Child, _} = retry_realm_spawn(1000),
        ok = erts_internal:realm_stop(Child)
    end).

retry_realm_spawn(0) -> ct:fail(realm_quota_not_reclaimed);
retry_realm_spawn(N) ->
    garbage_collect(),
    try erts_internal:realm_spawn(?MODULE, wait_identity, []) of
        Pair -> Pair
    catch error:system_limit ->
        receive after 1 -> ok end,
        retry_realm_spawn(N - 1)
    end.

concurrent_realm_limit(_Config) ->
    ok = in_policy(#{allow_create_realms => true, max_realms => 2}, fun() ->
        Parent = self(),
        Ref = make_ref(),
        Workers = [spawn_monitor(fun() ->
            receive go -> ok end,
            Result = try erts_internal:realm_spawn(?MODULE, wait_identity, []) of
                         Pair -> {ok, Pair}
                     catch error:system_limit -> denied
                     end,
            Parent ! {Ref, Result},
            receive release -> ok end
        end) || _ <- lists:seq(1, 2)],
        lists:foreach(fun({P, _}) -> P ! go end, Workers),
        Results = [receive {Ref, R} -> R
                   after 5000 -> ct:fail(quota_race_timeout)
                   end || _ <- Workers],
        [denied] = [R || R <- Results, R =:= denied],
        [{ok, {Child, _}}] = [R || R = {ok, _} <- Results],
        ok = erts_internal:realm_stop(Child),
        lists:foreach(fun({P, M}) -> P ! release, await_exit(P, M) end, Workers)
    end).

closed_delegation(_Config) ->
    Host = self(),
    Ref = make_ref(),
    {Ancestor, {Child, ChildPid}} = in_realm_with_handle(fun() ->
        erts_internal:realm_spawn(?MODULE, realm_entry, [Host, Ref],
                                  #{allow_create_realms => true})
    end, #{allow_create_realms => true}),
    try
        ok = erts_internal:realm_close(Ancestor),
        ChildPid ! {Ref, fun() ->
            ?assertError(system_limit, spawn(fun() -> ok end)),
            ?assertError(system_limit, erts_internal:realm_spawn(?MODULE, wait_identity, [])),
            ok
        end},
        expect_ok(Ref),
        ok = erts_internal:realm_stop(Ancestor),
        done = erts_internal:realm_stop_members(Child)
    after
        ok = erts_internal:realm_stop(Ancestor)
    end.

delegated_stop_race(_Config) ->
    lists:foreach(fun(_) ->
        Host = self(),
        Ref = make_ref(),
        {Realm, Root} = erts_internal:realm_spawn(erlang, apply, [fun() ->
            Creator = fun Loop() ->
                try erts_internal:realm_spawn(erlang, apply, [fun() ->
                    Host ! {Ref, self()},
                    wait_identity()
                end, []]) of
                    _ -> Loop()
                catch error:system_limit -> wait_identity()
                end
            end,
            Creator()
        end, []], #{allow_create_realms => true, max_realms => 256}),
        try
            First = receive {Ref, P} -> P
                    after 5000 -> ct:fail(no_delegated_child)
                    end,
            Workers = [spawn_monitor(fun() -> erts_internal:realm_stop(Realm) end)
                       || _ <- lists:seq(1, 2)],
            lists:foreach(fun({P, M}) -> await_exit(P, M) end, Workers),
            false = is_process_alive(Root),
            false = is_process_alive(First),
            assert_stopped_children(Ref),
            done = erts_internal:realm_stop_members(Realm)
        after
            ok = erts_internal:realm_stop(Realm)
        end
    end, lists:seq(1, 20)).

assert_stopped_children(Ref) ->
    receive {Ref, Pid} ->
        false = is_process_alive(Pid),
        assert_stopped_children(Ref)
    after 0 -> ok
    end.

depth_limit(_Config) ->
    64 = in_policy(#{allow_create_realms => true}, fun() -> descend_realms(1) end).

descend_realms(N) ->
    try in_policy(#{allow_create_realms => true}, fun() -> descend_realms(N + 1) end)
    catch error:system_limit -> N
    end.

expect_ok(Ref) ->
    receive {Ref, {ok, ok}} -> ok;
            {Ref, Error} -> ct:fail(Error)
    after 5000 -> ct:fail(policy_reply_timeout)
    end.

in_policy(Policy, Fun) ->
    {Realm, Result} = in_realm_with_handle(Fun, Policy),
    ok = erts_internal:realm_stop(Realm),
    Result.

in_realm(Fun) ->
    {Realm, Result} = in_realm_with_handle(Fun),
    ok = erts_internal:realm_stop(Realm),
    Result.

in_realm_with_handle(Fun) ->
    in_realm_with_handle(Fun, undefined).

in_realm_with_handle(Fun, Policy) ->
    Ref = make_ref(),
    {Realm, Pid} = case Policy of
        undefined -> erts_internal:realm_spawn(?MODULE, realm_entry, [self(), Ref]);
        _ -> erts_internal:realm_spawn(?MODULE, realm_entry, [self(), Ref], Policy)
    end,
    Mon = monitor(process, Pid),
    try
        Pid ! {Ref, Fun},
        Result = receive
            {Ref, {ok, Value}} -> Value;
            {Ref, {error, Class, Reason, Stack}} ->
                erlang:raise(Class, Reason, Stack);
            {'DOWN', Mon, process, Pid, Reason} ->
                ct:fail({realm_exit, Reason})
        after 10000 -> ct:fail(realm_timeout)
        end,
        await_exit(Pid, Mon),
        {Realm, Result}
    catch ErrorClass:Reason0:ErrorStack ->
        ok = erts_internal:realm_stop(Realm),
        erlang:raise(ErrorClass, Reason0, ErrorStack)
    after
        demonitor(Mon, [flush])
    end.

realm_entry(Parent, Ref) ->
    receive
        {Ref, Fun} ->
            Result = try Fun() of
                         Value -> {ok, Value}
                     catch Class:Reason:Stack ->
                         {error, Class, Reason, Stack}
                     end,
            Parent ! {Ref, Result}
    after 10000 -> exit(fixture_timeout)
    end.

report_identity(Parent, Ref) ->
    Parent ! {Ref, erts_internal:realm_id()}.

wait_identity() ->
    receive
        {identity, Parent, Ref} ->
            report_identity(Parent, Ref),
            wait_identity()
    after 10000 -> exit(fixture_timeout)
    end.

receive_identity(Ref) ->
    receive {Ref, Id} -> Id
    after 5000 -> ct:fail(identity_timeout)
    end.

await_exit(Pid, Mon) ->
    receive
        {'DOWN', Mon, process, Pid, normal} -> ok;
        {'DOWN', Mon, process, Pid, Reason} -> ct:fail({child_exit, Reason})
    after 5000 -> ct:fail(exit_timeout)
    end.
