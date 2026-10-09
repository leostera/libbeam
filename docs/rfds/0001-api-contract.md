<!--
%CopyrightBegin%

SPDX-License-Identifier: Apache-2.0

Copyright 2026 Leandro Ostera <leandro@ostera.io>

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

%CopyrightEnd%
-->

# Realm prototype API contract

Companion to [RFD 0001](0001-beam-realms.md) and the
[implementation record](0001-implementation.md). This documents the current
experimental native operations and their public `realm` wrappers (API version 0),
not a stable release API or a complete operation-policy inventory. The prototype is **not a sandbox**.
The [initial process-operation policy](0001-operation-policy.md) specifies the
opt-in message/control checks, temporary unsupported operations, and bypass gaps.

## Public API version 0

`realm:api_version/0` returns `{experimental, 0}`, not a security capability claim.
The Kernel module exports `identity/0,1`, `create/1`, `spawn_root/4`, `spawn/3,4`,
`close/1`, `stop/1`, and `endpoint_create/4`, `endpoint_send/2`,
`endpoint_receive/1`, `endpoint_revoke/1`. These correspond to the native names
below without the `realm_` prefix. Identity, management handle and endpoint types
are separately opaque. Numeric IDs and stop-work snapshots remain internal.
Wrappers execute in the caller and preserve runtime checks and return conventions;
calling a public or internal module never grants authority.

`realm_api_SUITE` verifies all exported operations, wrong-kind/stale handles and
wrapper non-escalation: five cases pass on optimized JIT, debug JIT and debug
interpreter. Version 0 remains incompatible-change eligible until release review.
The [adopted contracts](0001-contract-decisions.md) fix manager control exceptions,
visibility, lifetime vocabulary and versioning policy; required implementation
and independent review are still tracked separately.

## Mutable resource use

The [shared-resource enforcement record](0001-shared-resource-enforcement.md)
adds same-Realm use checks to atomics and both counter backends, including internal
counter BIFs. Resource references retain their owner Realm; passing/inspecting a
reference does not authorize host/ancestor/sibling use. This rule applies to legacy
process profiles as well. Missing/foreign context raises `badarg` before effects.

Restricted persistent-term entry points are temporarily denied, and global internal
erase is host-only. These checks do not implement scoped persistent terms, general
resource revocation on close, admission budgets or a safe native-effect profile.

## Identity, handles, and authority

These are distinct:

- **Identity:** a canonical opaque reference issued by the runtime. Repeated
  queries for the same Realm compare equal with `=:=`, including queries through
  independently obtained management handles. Different Realms compare unequal.
  Ordinary processes inherit their Realm's identity. The host has an opaque
  identity too; its numeric diagnostic ID remains zero.
- **Management handle:** a typed, runtime-validated resource reference retaining
  Realm metadata. Multiple unequal handles may refer to the same Realm. Their
  canonical identities, rather than the handles, are suitable map keys for
  grouping observations by Realm.
- **Authority:** determined from the executing process's immutable Realm policy
  and ancestry, not from claimed sender identity or possession of either value.
  Host callers can manage any non-host Realm. A creation-enabled Realm can manage
  strict descendants, but not itself, ancestors, siblings, or unrelated Realms.

An identity is not a bearer credential, proof of message origin, or management
handle. No operation resolves an identity to management authority, joins a Realm
by identity, or reopens a Realm. Reference encoding and printed representation
are not API contracts or authentication mechanisms. Copying an existing value
does not create a new Realm. Management operations always validate the resource
type and execution-context authority.

Identities are local to one VM lifetime. Persistence, cross-node management,
restoring a deployment's identity, and live migration are unsupported. An old
identity can remain as an inert observation after its Realm is reclaimed; a new
Realm never reuses it.

## Operations and errors

All names below are in `erts_internal`. These are Erlang exceptions unless a
reply or return value is explicitly described.

| Operation | Success | Rejection |
| --- | --- | --- |
| `realm_identity/0` | Current Realm's canonical identity | `badarg` if execution has no Realm context |
| `realm_identity/1` | Canonical identity from a genuine retained management handle; no management permission required | `badarg` for other terms, including identity tokens and unrelated magic resources |
| `realm_id/0` | Numeric diagnostic identity | Not a management or public identity interface |
| `realm_create/1` | Management handle for an empty Realm with the supplied policy | `badarg` for malformed policy or missing creation permission; `system_limit` for Realm/depth admission limits |
| `realm_spawn_root/4` | PID of a new unlinked process in an existing Realm, authorized by its management handle and caller ancestry | `badarg` for malformed MFA, invalid handles, or unauthorized callers; `system_limit` for closed or exhausted admission |
| `realm_spawn/3` | `{Handle, RootPid}` with default child policy | `badarg` for malformed MFA or missing creation permission; `system_limit` for exhausted admission or VM limits |
| `realm_spawn/4` | Same, with explicit policy | Additionally `badarg` for malformed/unknown policy fields or widening inherited ceilings |
| `realm_close/1` | `ok`; idempotently closes admission throughout the subtree | `badarg` for invalid handles or unauthorized callers |
| `realm_stop_children/1` | At most 64 retained handles for active direct child Realms | `badarg` for invalid handles, unauthorized callers, or an open subtree |
| `realm_stop_members/1` | At most 64 direct member PIDs, or `done` | Same rejection as child snapshots; `done` requires no direct members, pending admissions, or active children |
| `realm_stop/1` | `ok` after descendant process shutdown, deferred cleanup, and endpoint queue drainage | `badarg` for invalid handles or unauthorized callers; native work can delay completion indefinitely |

`realm_spawn/3,4` creates a new Realm and its initial unlinked process. Alternatively,
`realm_create/1` creates an empty Realm so the host can prepare grants before
`realm_spawn_root/4` starts execution there. Authorized ancestors may start multiple
roots in an open Realm; each root is its own group leader. These operations create
new processes, never move existing ones. Ordinary spawn always inherits the
caller's Realm and cannot select a target Realm.
Malformed function/module names and improper argument lists are rejected before
creation; successful process creation is not proof that the target function
exists or starts successfully. Ordinary OTP execution failures still apply.

Closure or process-budget exhaustion rejects synchronous local spawning with
`system_limit`. Asynchronous `spawn_request` uses its ordinary error reply with
reason `system_limit`. A previously admitted process may finish initialization
but remains tracked for shutdown. There is no promise that every native allocator
failure is recoverable: general OOM fault injection remains unvalidated.

## Policy and delegation

Supported immutable keys:

| Key | Default | Semantics |
| --- | --- | --- |
| `allow_create_realms` | `false`, even for children of managers | Boolean; explicitly permits creation, further delegation, and strict-descendant management |
| `restrict_process_access` | Parent setting; top-level `false` during fixture migration | Boolean; opts into the initial process-operation checks. Restricted parents cannot create unrestricted children. Neither setting is a sandbox |
| `max_realms` | Parent ceiling; top-level `infinity` | Positive small integer or `infinity`; counts this Realm and retained descendant Realm objects |
| `max_processes` | Parent ceiling; top-level `infinity` | Positive small integer or `infinity`; counts reserved and live/deferred processes across the subtree |

Limits must fit every ancestor's remaining budget. Children can request smaller
ceilings, not larger ones. `infinity` is internally bounded by `MAX_SMALL`.
Ancestor closure and quota checks are serialized with admission. Non-host depth
is limited to 64; exceeding it raises `system_limit`. There is no mutable policy
or per-process override. Creation permission does not grant system-process
creation or other host-only operations.

## Initial controlled host channels

The first endpoint profile is a unidirectional, pull-based FIFO of copied bytes,
not an Erlang process mailbox, generic RPC system, or host-effects broker. It
accepts no PIDs, references, funs, resource handles, or other terms as payloads.
A binary encoding such terms is still just bytes: the runtime does not decode it.
Host handlers must validate their application protocol and must not interpret a
claimed identity in the bytes as authenticated caller authority.

| Operation | Success/status | Rejection |
| --- | --- | --- |
| `realm_endpoint_create(RealmHandle, Direction, MaxMessages, MaxBytes)` | Endpoint reference, or `closed` if the Realm/ancestor is already closed | Host only; `badarg` for unauthorized callers, invalid handles, directions, or bounds; `system_limit` when the subtree has 64 retained endpoints |
| `realm_endpoint_send(Endpoint, Binary)` | `ok` after enqueue; `full` without enqueue if capacity is insufficient; `closed` after revocation/closure | `badarg` for wrong resource type, wrong caller Realm/direction, non-binary or oversized payload; recoverable queue allocation failure raises `system_limit` |
| `realm_endpoint_receive(Endpoint)` | `{ok, Binary}`, `empty`, or `closed` | `badarg` for wrong resource type or wrong caller Realm/direction |
| `realm_endpoint_revoke(Endpoint)` | `ok`, idempotently; discards queued data | Host only; `badarg` for invalid handles or unauthorized callers |

`to_host` permits sending only from the specified Realm and receiving only from
the host. `to_realm` reverses those roles. Other Realms, including descendants,
cannot use a leaked endpoint; ordinary processes in the bound Realm share its
authority. Only the host can create or revoke grants in this initial profile;
Realm creation permission does not imply endpoint-grant authority.

Bounds are 1..1024 messages and 1..1048576 bytes per queue, with a maximum payload
of 65536 bytes. Empty binaries consume message slots. Whole-byte binary values
with unaligned backing storage are supported; partial-byte bitstrings are not.
A top-level subtree may retain at most 64 endpoint objects across all descendants,
including revoked endpoints whose handles remain reachable. This fixed prototype
limit bounds aggregate queued bytes to 64 MiB per subtree, not total Realm memory.
Consumed/discarded messages release queue capacity; endpoint-count reservations
are released at final endpoint reclamation.

Admission, dequeue, revocation, and ancestor closure are serialized under the
subtree lock. A dequeue committed before revocation may finish copying its result
after revocation returns; already-consumed application work is not cancelled.
Send copies are bounded and committed only after rechecking closure/capacity.
Transient sender buffers and returned receiver binaries are outside queued-byte
accounting. Copying is charged reductions, but aggregate CPU/memory isolation
remains unimplemented.

Closure immediately prevents further queue admission or consumption. Queued
payloads are discarded by revocation, endpoint destruction, a closed receive, or
shutdown. Each stop-members pass drains at most 64 queued messages, and `done`
requires zero queued messages. Queued work keeps an otherwise empty descendant
in the active tree until drained. Endpoints retain Realm metadata until their
last reference is reclaimed, but do not prevent logical shutdown after drainage.

The prepared bootstrap test provisions both directions before starting tenant
code and uses only endpoints for its data exchange. Most older tests still use
ordinary cross-Realm fixture messages in the legacy unrestricted profile; they
must be migrated before restrictions become the default. The separate process
suite uses endpoint-only reporting and explicitly opts into restrictions.
No asynchronous wakeup, RPC reply/deadline protocol, general grant delegation,
configurable endpoint-count policy, or non-host-to-non-host profile is claimed.

## Lifetime and shutdown

A Realm owns one reference to its canonical identity token. The token contains no
pointer back to the Realm. Retaining it after shutdown does not retain Realm
metadata, processes, ancestry, or a creator's Realm-count reservation. The token
itself still consumes memory while retained; aggregate memory charging is not
implemented.

A management handle does retain metadata and its Realm-count charge, even after
shutdown. Each descendant object retains its parent, independently of the
creating process's lifetime. Closed, drained descendants leave the active tree
without making surviving handles invalid. Counts are released at their defined
reclamation points, not merely when a root exits or a monitor reports `DOWN`.

Snapshots do not consume members or children. Empty snapshots do not establish
completion, and concurrent controllers can observe the same work. Shutdown is
idempotent and resumable by an authorized ancestor with a management handle.
Dropping handles does not automatically kill running processes.

Process shutdown now drains these endpoint transport queues. It does not yet
cancel timers, all other queued work, application-level requests already consumed
by a broker, or external side effects. Cross-Realm messaging, native effects, distribution,
and shared code/state remain unrestricted in important ways. The contract above
must not be interpreted as completion of M2 or M3.

## Remaining contract work before isolation

Track these decisions and their implementation in the
[remaining-work checklist](0001-work-plan.md), especially W1, W2, W5, and W7.
The [native inventory seed](0001-native-inventory.md) supplies dispatch facts, not
completed authority decisions or additional supported APIs.

- Stabilize version 0 only after compatibility and release review; extend the public
  API for future broker/code/resource features without exposing private primitives.
- Extend the implemented byte-channel profile with reviewed grant delegation,
  broker operations, replies, deadlines, cancellation, and asynchronous wakeups.
  Do not confuse transport queue cleanup with cancellation of host-side work.
- Complete the initial operation-policy matrix for all remaining native and
  asynchronous paths, without suppressing VM cleanup signals. The adopted contract
  retains narrow manager exceptions for covered ordinary process controls.
- Complete the BIF/internal/native operation inventory and restricted runtime
  profile, including distribution activation and indirect host-service bypasses.
- Define memory/CPU charging, allowed overshoot, atom behavior, and exhaustion
  responses before claiming resource isolation.
