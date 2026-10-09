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

%% Negative isolation witness, NOT acceptance. Use a disposable VM only.
%% Trusted bootstrap deliberately supplies a foreign atomics handle and key.
-module(realm_effects_probe).
-export([run/0, root/6]).

run() ->
    Atomics = atomics:new(1, []),
    ok = atomics:put(Atomics, 1, 41),
    Key = {?MODULE, make_ref()},
    Secret = make_ref(),
    persistent_term:put(Key, Secret),
    try
        Realm = realm:create(#{restrict_process_access => true}),
        try
            Out = realm:endpoint_create(Realm, to_host, 1, 4096),
            Nonce = make_ref(),
            Pid = realm:spawn_root(Realm, ?MODULE, root,
                                  [Out, Atomics, Key, Nonce, realm:identity(), Realm]),
            %% The ordinary send is rejected while deferred host delivery is not.
            denied = try erlang:send(Pid, ordinary_probe), unexpected_allowed
                     catch error:badarg -> denied end,
            Timer = erlang:send_after(0, Pid, {timer_probe, Nonce}),
            try
                {observed, denied, denied, true} = sample(Out, erlang:monotonic_time(millisecond) + 5000),
                41 = atomics:get(Atomics, 1),
                Secret = persistent_term:get(Key),
                #{decision => shared_effects_bypasses_observed,
                  foreign_atomics_read_write => denied,
                  global_persistent_read_write => denied,
                  host_timer_into_restricted_realm => observed,
                  ordinary_host_send => denied,
                  security_acceptance => not_met}
            after
                erlang:cancel_timer(Timer)
            end
        after
            realm:stop(Realm)
        end
    after
        persistent_term:erase(Key)
    end.

root(Out, Atomics, Key, Nonce, HostIdentity, Realm) ->
    Identity = realm:identity(),
    true = Identity =/= HostIdentity,
    Identity = realm:identity(Realm),
    Before = try atomics:get(Atomics, 1), unexpected_allowed
             catch error:badarg -> denied end,
    denied = try atomics:put(Atomics, 1, 42), unexpected_allowed
             catch error:badarg -> denied end,
    Value = try persistent_term:get(Key), unexpected_allowed
            catch error:badarg -> denied end,
    denied = try persistent_term:put(Key, tenant_modified), unexpected_allowed
             catch error:badarg -> denied end,
    receive
        {timer_probe, Nonce} ->
            ok = realm:endpoint_send(Out, term_to_binary({observed, Before, Value, true})),
            receive after infinity -> ok end
    after 5000 ->
        error(missing_host_timer_delivery)
    end.

sample(Out, Deadline) ->
    case realm:endpoint_receive(Out) of
        {ok, Binary} -> binary_to_term(Binary, [safe]);
        empty ->
            case erlang:monotonic_time(millisecond) < Deadline of
                true -> receive after 1 -> sample(Out, Deadline) end;
                false -> error(effects_probe_timeout)
            end;
        closed -> error(effects_probe_closed)
    end.
