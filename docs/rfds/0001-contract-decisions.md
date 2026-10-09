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

# W1 contract decisions and change-review requirements

These are adopted engineering contracts for subsequent implementation, not a
statement that all described enforcement exists. The
[current API](0001-api-contract.md), [operation coverage](0001-operation-policy.md),
and [work plan](0001-work-plan.md) distinguish implemented behavior from gaps.
**Untrusted deployment remains no-go.** Closing a design task does not close its
runtime, platform, performance, or security acceptance gate.

## W1-11: identity, management, endpoints, and nested lifetime

Adopt the existing identity and lifecycle semantics, rather than replacing them
with bearer-authority IDs:

- Identity is an opaque comparable observation local to one VM lifetime. It is
  never resolved into permission or a process join operation. Copies and stale
  observations confer no authority. No persistence or cross-node representation
  is promised. Numeric diagnostic IDs are not public identity.
- A management handle is a typed reference to retained metadata. Possession alone
  is insufficient: every use checks actual execution context and ancestry.
  Wrong-kind, forged, sibling/ancestor/unrelated management and malformed arguments
  raise `badarg`. A valid closed handle stays identifiable/manageable; it cannot
  reopen admission. New root admission into it raises `system_limit`.
- Ordinary spawn inherits immutable membership before publication. Authorized
  creation starts new roots; it never moves an existing process. Parent policy,
  every ancestor ceiling, closure, and the depth-64 bound constrain admission.
  Explicit delegation cannot widen an ancestor's authority or budget.
- Managers may create multiple roots in an open Realm. Root exit or dropping a
  handle does not close it. Close is monotonic and inherited; stop is resumable and
  idempotent. Logical drain and physical reclamation are distinct observations.
- Management references retain metadata/count charges. Canonical identities do
  not retain Realm metadata, parents, or Realm-count reservations. The identity
  value still occupies memory; future byte charging must preserve that distinction.
- Endpoint references are typed rights selectors **plus** caller binding,
  direction, operation, lifetime, and queue limits. They are not transferable
  bearer grants. Descendants do not inherit a parent's endpoint rights. Current
  creation/revocation is host-only. Revocation does not undo a dequeue committed
  before revocation or cancel already-consumed application effects.
- Bytes are not authenticated sender claims. A broker must derive its client and
  resource scope from the runtime-bound grant; decoding a supplied PID, identity,
  handle representation, or MFA cannot authorize it. No generic host `apply`
  service is part of this contract.

Evidence: current native implementations and the lifecycle/endpoint/process
suites; the public API tests additionally assert stale identity, wrong-kind
handles, wrapper non-escalation, and no target closure after rejected management.
Future resource adapters must preserve these semantics, not infer complete
resource shutdown from the prototype's process/endpoint-only `stop` result.

## W1-12: retain narrow manager control exceptions

**Decision: retain ordinary process-control and inspection APIs for authorized
managers.** Do not require a second exclusively scoped control API for operations
already accepting a precise target PID. This preserves supervision, monitoring,
shutdown, and authenticated VM task forwarding without making internal module
names privileged.

The host may manage non-host Realms. A creation-enabled Realm may manage strict
descendants; it cannot manage itself through a Realm management handle, ancestors,
siblings, or unrelated Realms. Ordinary same-Realm process operations remain
ordinary local operations, independent of Realm creation permission.

The exception covers only the explicit control/inspection matrix: links,
monitors, exits, process information/flags, group leaders, suspension, GC and
code-check requests. Each explicit reply destination also needs authorization.
It does **not** authorize foreign ordinary sends, ETS access, code mutation,
filesystem/network effects, arbitrary VM controls, or endpoint delegation.
Adding a new resource/operation requires its own reviewed decision.

Managers are trusted with the child data exposed by permitted inspection
(including mailbox/stack-related process information). A tenant requiring
confidentiality from its host or creation-enabled ancestor needs a different
trust boundary; this design does not provide it. Global listing is a host
administrative operation, not a tenant's route around scoped enumeration.

## W1-13: candidate profile and unresolved compatibility pins

The first production candidate is single-node, restricted-only, byte-grant based,
with immutable shared platform code and private deployment code environments.
Legacy unrestricted creation remains a trusted prototype facility and must be
unavailable in the production candidate, not merely hidden by an API wrapper.

Required activation invariants: no active/pending distribution, no late activation
race, no raw tenant OS/driver/debug authority, no unreviewed native effects,
scoped OTP services, complete owned-resource teardown, and enforced ancestor
memory/CPU/admission limits. Native allowlisting is at **use time**, including
already-loaded exports, dirty work and callbacks; module names/loading permission
are not an allowlist. Missing caller context never defaults to host privilege.

The tested compiler/runtime baseline is OTP `30.0-rc0` at the pinned source
revision. Exact supported Elixir/Gleam/compiler/package versions and the required
OTP-service compatibility matrix are **not established**. W1-13 remains open;
W7 must validate and pin those artifacts before any supported-profile claim.
This is not an Erlang-only reduction of the full RFD. The local installation
reports Elixir 1.19.5 built with OTP 28; no `gleam` executable is available.
Neither observation validates compatibility with this OTP 30 fork or its proposed
scoped OTP/code environment.

## W1-14: visibility and side-channel limits

- Permit ordinary local monotonic/system time and duration measurement. There is
  no virtual clock or timing noninterference guarantee. Scheduling, GC, cache,
  memory pressure and shared platform execution can expose co-tenancy timing.
- Preserve each operation's missing-current-incarnation-PID semantics. A known
  live foreign PID may be denied while an absent PID yields `noproc`, `false`, or
  successful no-delivery. This existence distinction is accepted and documented,
  not a secretly stronger claim of tenant invisibility.
- Tenant enumeration must be constructed in scope, including yielded/deleted
  tracking and continuations. Raw global process/port/table/name lists, foreign
  stacks/mailboxes, code environments and native resources are not tenant views.
- Platform identity needed for compatibility (OTP/API version, architecture and
  declared supported features) may be visible. VM-global resource statistics,
  module/atom inventories and global tracing are **not automatically approved**.
  Until every query is classified, production must reject unknown queries.
- Tenant-facing errors/logs must not include foreign payloads, stack locals,
  filesystem secrets, cookies, host credentials, endpoint payloads, or unredacted
  native handles. Public errors use the documented operation-specific categories;
  detailed host diagnostics require host authority and bounded retention.
- Host diagnostics may correlate Realm identities and usage for operations, with
  explicit access control and redaction at external export. Do not treat a host
  log collector's execution context as permission to service arbitrary tenant
  diagnostic commands. Retain origin and charge when emitting deferred events.

These are visibility decisions, not implemented redaction or an audited
`system_info` allowlist. Microarchitectural covert channels, malicious privileged
host/native code and whole-VM/OS crashes remain outside the in-VM boundary.
Use OS isolation where those threats or confidentiality from the host matter.

## W1-15: ownership and charging vocabulary

| Term | Required meaning |
| --- | --- |
| Owner | Realm/account responsible for a resource's effects and release; possession or last caller is not automatic ownership |
| Reservation | Atomically admitted capacity across all relevant ancestors before publication/allocation/effect commit |
| Live usage | Committed resource usage; includes admitted work even if its creating process exits |
| Retained object | Logically closed/unused object still pinned by handles, queues, native jobs, code/funs or callbacks; its applicable charges remain |
| In-flight work | Work between admission and commit/completion; has retained provenance, reserved capacity and a cancellation/finalization owner |
| Shared charge | Explicit bounded shared-pool charge or conservative retainer charge; never an unowned discount because a binary/code image is shared |
| Transfer | Authorized reassignment after reserving receiver/ancestor capacity; commit must not expose an uncharged interval |
| Rollback | Exactly-once release of uncommitted reservations; does not claim to reverse already committed external effects |
| Logical completion | No further admission and defined owned work drained/cancelled; may precede final native/reference release |
| Physical reclamation | Last dependent reference/job/callback is gone and storage/charges can actually be released |
| Overshoot | Explicit pre-reserved, finite headroom needed by bounded operations; not unlimited post-hoc debt |
| Host headroom | Separately reserved capacity for control, cleanup and diagnostics that tenants cannot exhaust through ordinary admission |

Adapters must specify the charge basis (bytes, objects, operations, CPU service,
queued messages), reservation/commit/release linearization points, refund behavior,
allocator failure and exit races. Caller death is not a refund; a retry cannot
charge or release the same reservation twice. Cross-owner sharing/transfer must
recheck both authority and receiver budget before commit. The release path must
not execute arbitrary code while holding a Realm/subtree accounting lock.

CPU service includes dirty work, callbacks, copying, parsing and failed work;
reductions alone are not a complete CPU budget. Heap size alone is not a complete
memory budget. Identity storage, shared binaries/code, allocator overhead,
transient copies and retained closed objects must have explicit charging rules.
The resource-specific representation and measured bounds are W5/W6 work, not
implemented by this vocabulary or the existing count/endpoint limits.

## W1-16: experimental public API and versioning

The public module is `realm` in Kernel. Version discovery is
`realm:api_version() -> {experimental, 0}`; it is not security capability discovery.
The implementation adds caller-transparent wrappers for identity, creation,
root spawn, convenience spawn, close/stop, and endpoint operations. Identity,
management and endpoint types are separately opaque. Internal snapshot/diagnostic
primitives are not public API. Current return values/exceptions are preserved;
there is no new arbitrary `apply`, Realm-join, or authority-changing service.

Version 0 may change incompatibly. Consumers must pin the experimental API and
runtime build, not infer support from the existence of `erts_internal` names.
Before a stable release, version/type/error changes require compatibility tests,
release notes and explicit review. Native wrappers and BIFs must enforce authority
even if callers bypass this module. The complete future code-environment, broker,
resource and budget API is not silently promised by version 0.

## W1-17: required authority/lifetime change review

Every enforcement/resource patch must attach a record with source revision,
operation/path IDs, responsible implementation workstream, reviewer, decisions,
remaining findings, and executable evidence. An unassigned independent reviewer
is a blocker, never an implied approval. Use this checklist before merging an
implementation claim:

1. Identify actual caller provenance on normal, optimized, interpreter, dirty,
   callback, native-thread and no-live-caller paths; reject forged sender claims.
2. Enumerate all targets, alias/name rebinding, explicit reply destinations,
   callbacks, and transitive effects. Check authority at the last relevant use.
3. Prove check-before-effect and no mutation/delivery/consumption on rejection;
   preserve VM housekeeping and legitimate monitor/exit/cleanup replies.
4. Identify owner, every retained reference, reservations, ancestor checks,
   transfer, rollback, close/revoke/cancel linearization and final release.
5. Test allowed local behavior, each foreign relationship, wrong-kind/forged/
   stale/leaked/revoked handles, malformed inputs and missing-target conventions.
6. Exercise races, failure/OOM, caller exit, pending work, repeated shutdown and
   resource reclamation; distinguish logical completion from physical release.
7. Run applicable legacy regressions and runtime/platform variants, measure
   performance-sensitive paths, and classify every failure/skip/unavailable cell.
8. Update API/operation coverage, inventory, evidence and remaining tasks. Do not
   approve unknown native paths or substitute functional tests for security review.

### Open findings and accountable workstreams

| Finding | Owner | Required closure |
| --- | --- | --- |
| R-01: unreviewed native/debug/shared-state effects | W4 implementer/reviewer | Per-entry use-time checks and negative/indirect-path evidence |
| R-02: deferred delivery/incoming distribution | W3 implementer/reviewer | Retained origin, activation-race closure and late-delivery tests |
| R-03: incomplete resource cancellation/reclamation | W5 implementer/reviewer | Resource adapters and post-exit/native cleanup evidence |
| R-04: missing aggregate budgets/fairness | W6 implementer/reviewer | Measured, enforced ancestor charges and host headroom |
| R-05: global code and shared OTP services | W2/W7 implementer/reviewer | Genuine same-MFA isolation, lifetime/purge and compatibility evidence |
| R-06: incomplete performance/platform matrix | W1 implementer/reviewer | Baselines, threshold agreement and executed CI cells |
| R-07: independent hostile-workload review unassigned | W8 release owner | Named independent review and explicit release go/no-go |

This establishes review requirements and ownership by workstream; it does not
claim that any open finding has been resolved or independently reviewed.
