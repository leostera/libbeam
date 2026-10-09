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

-module(realm).
-moduledoc """
Experimental single-node Realm lifecycle and byte channels (API version 0).

This module is **not a sandbox**. Native effects, shared state and shared code
remain insufficiently mediated. Restrictions are currently opt-in. Do not run
untrusted workloads with either policy setting.

Identity values are comparable observations, not authority. Management and
endpoint handles are checked by the runtime against the executing process's
immutable Realm context. These wrappers execute in the caller; the module name
does not confer privilege. No process can move into another Realm.

Version 0 may change incompatibly until the release gates in RFD 0001 pass.
Handles are local to this VM lifetime and have no persistence/wire contract.
""".
-export([api_version/0, identity/0, identity/1, create/1, spawn_root/4,
         spawn/3, spawn/4, close/1, stop/1,
         endpoint_create/4, endpoint_send/2, endpoint_receive/1, endpoint_revoke/1]).
-export_type([identity/0, handle/0, endpoint/0, policy/0, direction/0]).
-compile({no_auto_import, [spawn/3, spawn/4]}).

-opaque identity() :: reference().
-opaque handle() :: reference().
-opaque endpoint() :: reference().
-type direction() :: to_host | to_realm.
-type policy() :: #{allow_create_realms => boolean(),
                    restrict_process_access => boolean(),
                    max_realms => pos_integer() | infinity,
                    max_processes => pos_integer() | infinity}.

-doc "Returns the experimental API version, not security capability discovery.".
-spec api_version() -> {experimental, 0}.
api_version() -> {experimental, 0}.

-doc "Canonical opaque identity of the executing process's Realm.".
-spec identity() -> identity().
identity() -> erts_internal:realm_identity().

-doc "Identity of a genuine management handle. This grants no management rights.".
-spec identity(handle()) -> identity().
identity(Handle) -> erts_internal:realm_identity(Handle).

-doc "Create an empty Realm with immutable, ancestor-attenuated policy.".
-spec create(policy()) -> handle().
create(Policy) -> erts_internal:realm_create(Policy).

-doc "Start an unlinked root in an authorized open Realm; its group leader is itself.".
-spec spawn_root(handle(), module(), atom(), [term()]) -> pid().
spawn_root(Handle, Module, Function, Arguments) ->
    erts_internal:realm_spawn_root(Handle, Module, Function, Arguments).

-doc "Create a Realm and its initial unlinked process using inherited defaults.".
-spec spawn(module(), atom(), [term()]) -> {handle(), pid()}.
spawn(Module, Function, Arguments) ->
    erts_internal:realm_spawn(Module, Function, Arguments).

-doc "Create a Realm and initial unlinked process with explicit immutable policy.".
-spec spawn(module(), atom(), [term()], policy()) -> {handle(), pid()}.
spawn(Module, Function, Arguments, Policy) ->
    erts_internal:realm_spawn(Module, Function, Arguments, Policy).

-doc "Idempotently close subtree admission. Existing work is not thereby reclaimed.".
-spec close(handle()) -> ok.
close(Handle) -> erts_internal:realm_close(Handle).

-doc """
Stop descendant processes and drain endpoint queues. Idempotent and resumable.
This does not yet cancel all native/resource work or host effects. Native work can
delay completion indefinitely; no shutdown deadline or full reclamation is promised.
""".
-spec stop(handle()) -> ok.
stop(Handle) -> erts_internal:realm_stop(Handle).

-doc """
Host-only creation of a bounded, caller-bound FIFO of copied bytes.
Limits: 1..1024 messages, 1..1048576 queued bytes, 65536 bytes per payload,
and 64 retained endpoints per top-level subtree. Other Realm descendants do not
inherit use rights. This is transport, not a generic host-effects broker.
""".
-spec endpoint_create(handle(), direction(), pos_integer(), pos_integer()) -> endpoint() | closed.
endpoint_create(Handle, Direction, Messages, Bytes) ->
    erts_internal:realm_endpoint_create(Handle, Direction, Messages, Bytes).

-doc "Enqueue copied bytes if direction, caller, lifetime and capacity permit it.".
-spec endpoint_send(endpoint(), binary()) -> ok | full | closed.
endpoint_send(Endpoint, Payload) -> erts_internal:realm_endpoint_send(Endpoint, Payload).

-doc "Dequeue copied bytes. A committed dequeue may return after revocation.".
-spec endpoint_receive(endpoint()) -> {ok, binary()} | empty | closed.
endpoint_receive(Endpoint) -> erts_internal:realm_endpoint_receive(Endpoint).

-doc "Host-only, idempotent revocation; already consumed application work is not cancelled.".
-spec endpoint_revoke(endpoint()) -> ok.
endpoint_revoke(Endpoint) -> erts_internal:realm_endpoint_revoke(Endpoint).
