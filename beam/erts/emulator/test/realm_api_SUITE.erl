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

-module(realm_api_SUITE).
-include_lib("stdlib/include/assert.hrl").
-export([suite/0, all/0, version_identity/1, lifecycle/1, endpoints/1,
         invalid_handles/1, authority/1, hold/0, echo/2, check_authority/3]).

suite() -> [{timetrap, {seconds, 60}}].
all() -> [version_identity, lifecycle, endpoints, invalid_handles, authority].

version_identity(_Config) ->
    ?assertEqual({experimental, 0}, realm:api_version()),
    Identity = realm:identity(),
    ?assert(is_reference(Identity)),
    Parent = self(),
    {Pid, Ref} = spawn_monitor(fun() -> Parent ! {self(), realm:identity()} end),
    receive {Pid, Identity} -> ok end,
    receive {'DOWN', Ref, process, Pid, normal} -> ok end,
    {Handle, Root} = realm:spawn(?MODULE, hold, []),
    try ?assert(is_pid(Root)) after realm:stop(Handle) end,
    {Restricted, Other} = realm:spawn(?MODULE, hold, [], #{restrict_process_access => true}),
    try ?assert(is_pid(Other)) after realm:stop(Restricted) end.

lifecycle(_Config) ->
    Handle = realm:create(#{restrict_process_access => true}),
    Identity = realm:identity(Handle),
    ?assertNotEqual(realm:identity(), Identity),
    try
        Pid = realm:spawn_root(Handle, ?MODULE, hold, []),
        ?assertEqual({group_leader, Pid}, process_info(Pid, group_leader)),
        ok = realm:close(Handle),
        ok = realm:close(Handle),
        ?assertError(system_limit, realm:spawn_root(Handle, ?MODULE, hold, [])),
        ok = realm:stop(Handle),
        ok = realm:stop(Handle),
        ?assertEqual(false, is_process_alive(Pid)),
        ?assertEqual(Identity, realm:identity(Handle))
    after realm:stop(Handle) end.

endpoints(_Config) ->
    Handle = realm:create(#{restrict_process_access => true}),
    try
        In = realm:endpoint_create(Handle, to_realm, 1, 1024),
        Out = realm:endpoint_create(Handle, to_host, 1, 1024),
        ?assertEqual(empty, realm:endpoint_receive(Out)),
        ?assertError(badarg, realm:endpoint_send(Out, <<"wrong direction">>)),
        ok = realm:endpoint_send(In, <<"ping">>),
        ?assertEqual(full, realm:endpoint_send(In, <<"overflow">>)),
        _ = realm:spawn_root(Handle, ?MODULE, echo, [In, Out]),
        ?assertEqual(<<"ping">>, await(Out)),
        ok = realm:endpoint_revoke(In),
        ok = realm:endpoint_revoke(In),
        ?assertEqual(closed, realm:endpoint_send(In, <<>>))
    after realm:stop(Handle) end.

invalid_handles(_Config) ->
    ?assertError(badarg, realm:close(realm:identity())),
    ?assertError(badarg, realm:stop(make_ref())),
    ?assertError(badarg, realm:identity(make_ref())),
    ?assertError(badarg, realm:endpoint_receive(make_ref())),
    ?assertError(badarg, realm:endpoint_revoke(make_ref())),
    ?assertError(badarg, realm:create(#{max_processes => 0})),
    ?assertError(badarg, realm:create(#{unknown => true})).

authority(_Config) ->
    A = realm:create(#{restrict_process_access => true}),
    try
        B = realm:create(#{restrict_process_access => true}),
        try
            Out = realm:endpoint_create(A, to_host, 1, 1024),
            _ = realm:spawn_root(A, ?MODULE, check_authority, [A, B, Out]),
            ?assertEqual(<<"denied">>, await(Out)),
            %% Rejected management must not have closed the target.
            Pid = realm:spawn_root(B, ?MODULE, hold, []),
            ?assert(is_process_alive(Pid))
        after realm:stop(B) end
    after realm:stop(A) end.

check_authority(Self, Sibling, Out) ->
    ?assertEqual(realm:identity(), realm:identity(Self)),
    ?assertError(badarg, realm:close(Self)),
    ?assertError(badarg, realm:close(Sibling)),
    ?assertError(badarg, realm:spawn_root(Sibling, ?MODULE, hold, [])),
    ?assertError(badarg, realm:create(#{})),
    ?assertError(badarg, realm:endpoint_revoke(Out)),
    ok = realm:endpoint_send(Out, <<"denied">>).

hold() -> receive after infinity -> ok end.
echo(In, Out) ->
    ?assertError(badarg, realm:endpoint_receive(Out)),
    ok = realm:endpoint_send(Out, await(In)).

await(Endpoint) -> await(Endpoint, erlang:monotonic_time(millisecond) + 5000).
await(Endpoint, Deadline) ->
    case realm:endpoint_receive(Endpoint) of
        {ok, Binary} -> Binary;
        empty ->
            case erlang:monotonic_time(millisecond) < Deadline of
                true -> receive after 1 -> await(Endpoint, Deadline) end;
                false -> error(endpoint_timeout)
            end;
        closed -> error(unexpected_closed_endpoint)
    end.
