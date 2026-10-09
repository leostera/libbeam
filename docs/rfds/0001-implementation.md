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

# Realm prototype: implementation status

This is the implementation companion to [RFD 0001](0001-beam-realms.md).
The [remaining-work checklist](0001-work-plan.md) tracks the eight work packages,
individual tasks, dependencies, and acceptance evidence from snapshot `18708c2d4d`.

**This prototype implements identity, creation policy, process lifecycle, and
initial opt-in process-operation checks—not a complete isolation boundary.
Do not run untrusted code in it.** M1 is substantially implemented; M2 is partial,
and M3–M5 and the RFD's full validation requirements remain outstanding.

## Available experimental operations

```erlang
%% Prepare the Realm and grants before starting any code inside it.
Realm = erts_internal:realm_create(#{max_processes => 16,
                                      restrict_process_access => true}),
try
    In = erts_internal:realm_endpoint_create(Realm, to_realm, 1, 32),
    Out = erts_internal:realm_endpoint_create(Realm, to_host, 1, 32),
    ok = erts_internal:realm_endpoint_send(In, <<"ping">>),
    Start = fun() ->
        {ok, <<"ping">>} = erts_internal:realm_endpoint_receive(In),
        ok = erts_internal:realm_endpoint_send(Out, <<"pong">>)
    end,
    Root = erts_internal:realm_spawn_root(Realm, erlang, apply, [Start, []]),
    Poll = fun Loop(0) -> error(timeout);
               Loop(N) ->
                   case erts_internal:realm_endpoint_receive(Out) of
                       {ok, Binary} -> Binary;
                       empty -> receive after 1 -> Loop(N - 1) end;
                       closed -> error(closed)
                   end
           end,
    <<"pong">> = Poll(1000),
    {Root, erts_internal:realm_identity(Realm)}
after
    ok = erts_internal:realm_stop(Realm)
end.
```

This example uses the explicit byte channel, not ordinary cross-Realm messages.
It opts into the initial ordinary-message/control restrictions but still executes
shared, already-loaded trusted fixture code. Independent loading and complete
isolation are not implemented; ordinary automatic loading through the shared
host code server is not a supported restricted-profile startup path.

The [prototype API contract](0001-api-contract.md) specifies the implemented
operations, authority checks, error behavior, and lifetimes.

- `erts_internal:realm_identity/0` returns the caller's canonical opaque identity.
- `erts_internal:realm_identity/1` returns the same identity from any genuine
  management handle for that Realm. Identification does not require or grant
  management authority. An identity token is not a management handle.
- `erts_internal:realm_id/0` returns the caller's numeric diagnostic identity.
- `erts_internal:realm_create/1` creates an empty Realm with immutable policy.
- `erts_internal:realm_spawn_root/4` starts a new process in an existing open
  Realm using a management handle and authorized host/ancestor execution. It can
  be called multiple times within admission limits; no existing process moves.
- `erts_internal:realm_spawn/3` returns `{Handle, RootPid}` for a fresh Realm and
  an unlinked root, using the default child policy. Creation requires the host or
  a caller whose immutable policy grants `allow_create_realms`.
- `erts_internal:realm_spawn/4` additionally accepts a strict policy map; see below.
- `erts_internal:realm_close/1` atomically closes local process and Realm admission
  throughout the target subtree, including existing child Realms. It also closes
  endpoint admission and consumption. Existing
  processes continue running. All local spawn variants reject new admissions
  with `system_limit`; asynchronous spawn requests report that error in their
  reply. A spawn admitted before closure can finish, but remains tracked.
- `erts_internal:realm_stop/1` closes admission, sends untrappable exit signals
  in bounded batches, and waits for admitted processes and their deferred cleanup.
  It also drains endpoint queues in bounded batches, includes all descendant
  Realms, and is idempotent and resumable by another
  authorized caller with a handle for the same Realm.
- `erts_internal:realm_stop_children/1` returns up to 64 active child handles for
  a closed subtree, for recursive shutdown by an authorized ancestor.
- `erts_internal:realm_stop_members/1` rejects open Realms and returns at most 64
  direct member PIDs, or `done` when no admitted spawn, member cleanup, queued
  endpoint message, or active child Realm remains. An empty list is not completion.
  Snapshots do not consume members and may include already-dead PIDs whose
  deferred cleanup is unfinished.
- Ordinary local children inherit their parent's Realm. Public spawn options
  cannot select another identity.
- A root is its own group leader, rather than implicitly inheriting the host's
  I/O endpoint. This does not implement an I/O server; ordinary synchronous I/O
  can block until a suitable group leader is provided.
- Tenant calls to `erts_internal:spawn_system_process/3` receive `badarg`.
- Identity is unaffected by the root exiting while descendants remain alive.

These interfaces are experimental and may change. Handles are GC-managed magic
references, validated by both resource type and the caller's authority. The host
can manage any non-host Realm. A creation-enabled Realm can manage its strict
descendants, but not itself, its ancestors, siblings, or unrelated Realms. Passing
a handle does not grant those permissions. Ordinary tenants remain denied even
with a valid leaked handle. Diagnostic integers are not management capabilities.
There is no API to join an existing Realm by ID, reopen a closed Realm, or stop
the host Realm. Handles are not canonical identity values: child snapshots can
issue distinct handles to the same object. Use `realm_identity/0,1` to obtain
canonical tokens for equality or map keys. Stable public API naming remains open.

Dropping a handle does not implicitly stop a Realm. The controller is responsible
for retaining or handing off management handles. Child Realms remain attached to
their ancestors independently of their creating processes. If a shutdown caller
dies midway, an authorized ancestor can resume shutdown.

## Creation policy

```erlang
{Manager, Root} = erts_internal:realm_spawn(manager, start, [], #{
    allow_create_realms => true,
    max_realms => 128,
    max_processes => 4096
}).
```

This branch enables Realm support by default; there is no separate configure
switch yet. The example requires a trusted, already-loaded `manager` module:
Realm-local loading of uploaded code is not implemented.

- `allow_create_realms` defaults to `false` for every new Realm, even when its
  creator has permission. It must be explicitly delegated. Ordinary processes
  inherit their Realm's policy; no process flag or policy mutation API exists.
- `restrict_process_access` inherits the parent setting, defaulting to `false` at
  top level while legacy fixtures migrate. Setting it to `true` opts into the
  [initial operation-policy matrix](0001-operation-policy.md). A restricted parent
  cannot create an unrestricted child. This is not a safe untrusted-code profile.
- `max_realms` counts the Realm itself and all retained descendant Realm objects,
  including closed objects pinned by handles or deferred process references. The
  charge is released at final object reclamation, not at `realm_stop/1` completion.
- `max_processes` includes the root, all descendant-Realm processes, pending
  process admissions, and deferred process cleanup. Charges are released when
  process deletion finishes, not just when monitors report `DOWN`.
- Limits accept positive small integers or `infinity`. Top-level defaults are
  `infinity` (internally `MAX_SMALL`). Omitted child limits inherit their parent's
  ceilings. A child cannot request larger ceilings, and its admissions must also
  fit every ancestor's remaining capacity. The host is not charged to these
  subtree limits; an overall node budget remains platform work.
- Unknown keys, malformed policies, unauthorized creation, and attempts to widen
  inherited limits return `badarg`. Exhausted quotas, closed admission, and the
  fixed maximum depth of 64 non-host Realms return `system_limit`.

These are counts for tracked local resources, not CPU or memory isolation. They
cannot yet prevent bypasses through unrestricted distribution or privileged
services. Realm creation permission does not grant system-process creation or
other host-only VM privileges.

## Controlled host byte channels

`realm_endpoint_create/4` grants one direction (`to_host` or `to_realm`) between
the host and exactly one Realm. The native send/receive operations verify the
caller's execution Realm, resource type, and direction. Only the host creates or
revokes endpoints in this initial profile; Realm creation permission alone is
insufficient. Ordinary children in the bound Realm can use the grant, but other
Realms cannot use leaked handles.

Payloads are copied binaries, never decoded Erlang terms. Each queue has explicit
message/byte bounds, with ceilings of 1024 messages and 1 MiB, and at most 64 KiB
per message. A top-level subtree can retain 64 endpoint objects total, including
revoked handles; releasing the last reference frees that reservation. Empty
binaries still consume slots. `send` returns `ok`, `full`, or `closed`; polling
`receive` returns `{ok, Binary}`, `empty`, or `closed`. Wrong callers, resource
types, and malformed arguments raise `badarg`.

The subtree lock serializes queue admission/dequeue with revocation and ancestor
closure. Copying happens outside that lock and is charged reductions. A send
commits only after checking closure and capacity; a dequeue committed before
revocation may finish returning afterwards. Queue nodes contain only owned byte
buffers. Destruction/revocation discards queued data; Realm stop drains at most
64 messages per members pass, keeping otherwise-empty child Realms in the active
tree until drainage completes. Endpoint handles retain Realm metadata but do not
prevent logical shutdown once queues are empty.

This is a pull-based transport for controlled bootstrap/testing, not general RPC.
There is no asynchronous wakeup, endpoint delegation beyond the host grant,
application-level cancellation/deadline protocol, or general host-effects broker.
Already-consumed host work and external effects are not cancelled by transport
revocation. See the [API contract](0001-api-contract.md) for precise bounds and
lifetime semantics. Aggregate CPU/memory quotas remain outstanding.

## Implementation invariants

`ErtsRealm` is an ERTS allocation with an immutable ID and reference count. The
host object is static and immortal. Fresh IDs are allocated atomically, fit in an
Erlang small integer, and saturate rather than wrap; allocation failure returns
`system_limit` to the root-spawn caller. IDs may have gaps.

Each Realm also owns a reference to one immutable, zero-payload magic binary.
All opaque identity queries wrap this same binary, yielding equal reference
values. The token has no back-reference to Realm metadata and carries no
management authority. Reclamation releases the Realm's binary reference outside
the subtree lock (magic-reference table locks rank earlier). Retained tokens can
outlive the Realm without retaining its ancestry or consuming its Realm-count
budget, although the tokens themselves still consume memory. Host identity is
initialized during late process initialization, after binary support is ready.

A root-creation call owns a temporary reference, transferred to the returned
magic handle on success and released on failure. Each allocated real process
owns another reference. Failed process-table insertion releases the process's
reference. A process releases its reference in `erts_free_proc()`, after its own
references and deferred cleanup have drained, not merely when its PID is removed
from the process table.

Each top-level Realm owns a mutex shared by its descendant Realms. Each Realm
has an immutable policy and parent pointer, a pending-spawn counter, subtree
usage counters, and intrusive process/member and active-child lists. The parent
pointer owns a reference, keeping the ancestry and shared lock alive. The host
remains outside this tree and accounting.

Process admission checks closure and quotas along the bounded ancestor chain,
then reserves charges atomically under the shared lock. Process allocation
happens without this lock; failure rolls back every charge. Fully initialized
children replace their pending reservation with membership before releasing
their main/message locks. Child Realm admission similarly reserves all enclosing
Realm counts and links into the tree before attempting root-process admission.
Failed creation releases its temporary reference and accounting.

Members are removed at the end of `delete_process()`, including when native work
requires delayed deletion. This is later than PID-table removal and potentially
later than monitor notification. The Realm lock protects list pointers and PID
snapshots; snapshots copy PIDs before releasing the lock and allocating an Erlang
list. The lock order is registered after process-table/process locks. No Realm
lock holder acquires a process lock, allocates a process, or sends an exit signal.
The host Realm bypasses membership indexing.

Child snapshots acquire references under the shared lock, then build magic
handles after unlocking. Final reference release is serialized with snapshots so
an unowned list pointer cannot be resurrected after its reference count reaches
zero. Final deletion unlinks the child and releases ancestor charges under the
lock, then releases the owning parent reference after unlocking. Closed, drained
children leave the active tree even if handles still pin their metadata; such
objects remain charged against Realm limits until final reclamation.

The Erlang stop loop recursively stops bounded child batches, then kills member
batches before waiting for monitor replies and rechecking completion. It yields
between batches to avoid spinning on pending
admission or deferred native cleanup. Completion does not promise immediate
physical memory reclamation: retained handles and process references can pin
metadata beyond process cleanup.

Membership is installed in `early_init_process_struct()` before process-table
publication. Child creation inherits the pointer. The private spawn override is
initialized explicitly and checked against authorized management ancestry; it is
not a parsed Erlang spawn option.

Pseudo-processes start without Realm authority. The boot caller explicitly
supplies host identity. Dirty-scheduler shadows borrow the real process's identity
for execution; debug assertions check that identity at flush. The existing
parentless distributed-spawn path explicitly remains host execution.

## Initial process-operation enforcement

`restrict_process_access => true` enables checks for ordinary PID/name sends,
normal/monitor aliases, priority sends, links, monitors, exits, inspection,
process flags, group leaders, suspension, and GC/code-check system tasks. Covered
foreign sends are rejected if either side is restricted. Controls permit existing
host/ancestor management scope. Internal alternate entry points and explicit
system-task reply destinations are checked too; VM cleanup is not blanket-filtered.

Restricted callers cannot use unscoped enumeration/registration, timer BIFs,
trace/global-monitoring configuration, global system flags, or the covered outgoing
distribution/control APIs. This is temporary fail-closed behavior, not scoped OTP
service support. See the [operation matrix](0001-operation-policy.md) for precise
semantics, absence behavior, management exceptions, and remaining bypasses.

## Unsupported security and lifecycle features

The following are not implemented:

- Complete process compartment enforcement, default-on restrictions, and full
  legacy fixture migration. Incoming distribution, unrestricted-origin timers,
  and native/debug paths can bypass the initial ordinary-operation checks.
- Scoped names, timers, tracing, ETS, persistent terms, or atoms.
- General endpoint delegation, broker operation rights, replies/deadlines and
  application-level cancellation; restricted native/host effects.
- Code environments or independent versions of the same module.
- Aggregate memory/CPU budgets and admission control for non-process resources
  beyond the initial bounded endpoint queues.
- Realm-wide ownership/cleanup of timers and other work that can outlive their
  initiating process. `realm_stop/1` stops processes and drains endpoint transport
  queues; it does not cancel all external effects or already-consumed host work.
- The non-distributed edge runtime profile; distribution retains ordinary OTP
  behavior and must not be mistaken for a Realm-safe configuration.
- A durable actor platform, request server, or deployment loader.

The legacy profile still allows ordinary communication with privileged services;
the opt-in profile denies covered foreign sends but has other bypass paths.
The root-spawn check prevents direct use by callers without creation permission but is
not a complete privilege-escalation defense. Arbitrary shared VM facilities remain
available. No security or density claim follows from passing identity tests.

## Building

Use [the OTP development guide](../HOWTO/DEVELOPMENT.md). On the current Apple
Silicon development host, configuration uses Homebrew OpenSSL:

```sh
export ERL_TOP="$PWD"
./otp_build configure --with-ssl=/opt/homebrew/opt/openssl@3
make -j8

# Rebuild preloaded modules with this checkout's compiler after source changes.
PATH="$PWD/bin:$PATH" ./otp_build update_preloaded --no-commit
make -j8
make -j8 TYPE=debug
```

The OpenSSL path is host-specific. Optional wx and ODBC applications require
additional system dependencies. No system installation is necessary.

Preloaded updates regenerate tracked BEAM artifacts locally. The contribution
guide asks that generated bootstrap/preloaded binaries not be included in an
upstream PR. Regenerate them when building the source patch rather than using an
older preloaded artifact with the new VM code.

## Tests

`erts/emulator/test/realm_SUITE.erl` is registered in the emulator test Makefile.
It covers host identity, concurrent unique roots, local spawn variants, rejected
root/system-process creation, invalid arguments, immutable membership, descendant
lifetime, and dirty CPU/I/O execution. Lifecycle cases cover malformed and leaked
handles, closed admission, shutdown across multiple batches, unrelated Realms,
concurrent and interrupted shutdown, spawn/close races, handle transfer and GC,
and process-table exhaustion in an isolated VM. A native helper also verifies
that shutdown waits beyond monitor notification for dirty execution cleanup.
Policy cases cover default denial, explicit/transitive delegation, ordinary spawn
inheritance of authority, malformed maps, limit escalation, management scope,
subtree shutdown, aggregate quotas across siblings and spawn variants, retained
handle accounting and reclamation, constructor rollback, concurrent creation,
ancestor closure, maximum depth, and delegated creation racing concurrent stop.
Opaque identity tests cover concurrent queries, ordinary spawn inheritance,
uniqueness, canonical equality across independent management snapshots, GC and
post-shutdown stability, rejection as management handles, and Realm-quota
reclamation while identities remain retained. Endpoint cases cover byte FIFO
exchange, unaligned binaries, size/count/byte bounds, caller/direction checks,
invalid handles/payloads, revocation, subtree drainage after roots exit, retained
endpoint limits, GC reclamation, and sends racing shutdown. Prepared startup
covers endpoint-only bootstrap, empty-Realm lifecycle, root management authority,
and process-admission rollback. Prepared bootstrap now enables process restrictions.

`erts/emulator/test/realm_process_SUITE.erl` adds ten endpoint-only restricted
fixture cases: inherited/attenuated policy, local PID/alias/priority traffic on
both queue modes, controls/cleanup, rejected foreign sends and controls (including
direct internal calls and system-task reply destinations), names/timers,
tracing/distribution denials, resolved-name checks, and exit races. A rejected
send must not consume a monitor's reply-demonitor alias.

```sh
export ERL_TOP="$PWD"
make emulator_test ARGS="-suite realm_SUITE realm_process_SUITE"
make emulator_test TYPE=debug ARGS="-suite realm_SUITE realm_process_SUITE process_SUITE monitor_SUITE register_SUITE timer_bif_SUITE"

# Build the complete interpreter variant before testing it.
make -j8 TYPE=debug FLAVOR=emu
make emulator_test TYPE=debug FLAVOR=emu ARGS="-suite realm_SUITE realm_process_SUITE"
```

Most older tests deliberately use cross-Realm fixture messages and trusted
closures. Prepared bootstrap and the new restricted suite use endpoints for all
cross-Realm fixture data. Remaining fixtures must migrate before restrictions
become the default. These are functional authority tests, not evidence of a sandbox.

## Validation record: ARM64 macOS

| Check | Result |
| --- | --- |
| Full configured optimized JIT build | Passed |
| Full configured debug JIT build | Passed |
| Full configured debug interpreter build | Passed |
| Both Realm suites, optimized JIT | 55 passed (45 lifecycle/endpoint + 10 process-policy) |
| Both Realm suites, debug JIT | 55 passed |
| Both Realm suites, debug interpreter | 55 passed |
| Debug process regressions: binary arguments, async spawn, aliases | 3 passed |
| Debug `monitor_SUITE` | 25 passed |
| Debug `register_SUITE` | 1 passed |
| Debug `timer_bif_SUITE` | 23 passed |
| Debug `dirty_bif_SUITE` | 13 passed |
| Debug signal regressions: ordering, cleanup, priority links/monitors/aliases | 9 passed |
| Debug trace regressions: send/priority receive, suspend/options, delivery barriers | 5 legacy-session + 5 dynamic-session passed |

The 84 focused regressions do not constitute a full emulator regression run.
Batch logs are under `/tmp/beam-realms-compartments/final-*.log`; all final build,
compile, test, and aggregate validation exit records are zero. Source edits add
no declaration-order warnings; earlier full variant builds also reported existing
warnings in unrelated upstream files. The restricted bootstrap example executes
successfully; local documentation links, license headers, and `git diff --check`
also pass. Unrelated regenerated preloaded BEAM artifacts were restored.

The configured builds include crypto/TLS but omit wx and ODBC because their
optional system dependencies are unavailable on the development host.

Historical validation of the identity-only prototype included the full debug
`process_SUITE`: 100 passed, 1 failed, and 3 skipped. W1-03–04 subsequently reproduced
`spawn_against_ei_node/1` failing on both fresh upstream and snapshot builds:
EI canonicalized the host to `macbookpro`, while the fixture requested `MacBookPro`.
The exact node-name handshake check rejected that mismatch (`ei_accept; 5`).
The test-only repair uses the C helper's reported node name; it passes on all
three current variants and on the unchanged upstream runtime. Production handshake
and Realm authority checks are unchanged.

The [checked-in runner and W1 evidence](0001-validation.md) record commands,
fresh-build results, the standalone handshake reproducer, and a test-harness
cross-worktree node-name collision found and addressed during verification.
The subsequent full debug process run reports 101 passed, zero failed, and three
classified skips. Broader code-loading, trace, dirty-NIF, and port results are now
recorded there, alongside upstream-reproducing signal/distribution timetraps,
a reproduced optimized NIF timeslice failure, and the full debug ETS runner timeout.
Optimized process-table cases and isolated ETS update-counter comparisons pass;
these are not full regression clearance. The [native discovery ledger](0001-native-inventory.md)
seeds 570 BIF and 180 NIF C API declarations without policy approval.

The public Kernel `realm` module now supplies version-0 caller-transparent wrappers;
five API cases pass on each of optimized JIT, debug JIT and debug interpreter.
[Contract decisions](0001-contract-decisions.md) and
[code-environment semantics](0001-code-environment-contract.md) are specified,
not generally enforced. The [benchmark harness and initial matched measurements](0001-benchmarks.md)
include repetition dispersion; performance acceptance thresholds remain unagreed.
The code witness confirms shared global resolution on three variants, not private
code environments. The eight-cell runtime CI configuration is unexecuted.

The [next-five batch](0001-next-five-evidence.md) completes W1-05 discovery and
classification with full optimized ETS/monitor/timer/registration runs and
installed OTP 28 interop on optimized/debug JIT. It adds strict CI aggregation,
matched measurements in both source orders, 482 native source records and 32
source-bound design dispositions. These do not close regression/performance or
security gates: restricted spawn p99 remains elevated, and a three-variant witness
confirmed foreign atomics, global persistent-term and host-timer bypasses.

The subsequent [shared-resource enforcement](0001-shared-resource-enforcement.md)
adds retained Realm ownership and use-time checks to atomics/counters, denies
restricted persistent-term access and makes global erase host-only. Nine new cases
cover leakage, local sharing, creator/owner exit, retained quota return and denied
persistent effects. Native byte budgets and revocation/admission on close are not
implemented; the timer bypass and other native/shared-state gaps remain.

Process-table exhaustion covers failed child allocation and failed fresh-root
creation. General allocator/OOM fault injection, x86-64, sanitizers, and security
review remain unvalidated.

## Next implementation steps

The [RFD](0001-beam-realms.md) contains the milestone gap ledger in section 6 and
completion criteria and high-level implementation order in section 8. Use the
[eight-package checklist](0001-work-plan.md) for the concrete execution queue and
completion gates, including the early private-code feasibility investigation.
The remaining areas are:

1. Canonical identity and the experimental version-0 public API are implemented.
   Complete the native operation/error inventory and compatibility/release review.
2. Controlled byte channels, prepared startup, an initial operation matrix, and
   endpoint-only restricted fixtures exist. Finish legacy fixture migration and
   runtime-profile compatibility before enabling restrictions by default.
3. Complete process compartments beyond the opt-in send/control checks: audit
   remaining asynchronous/native paths, implement scoped names/timers/enumeration/
   tracing, and enforce the non-distributed runtime profile including incoming paths.
4. Restrict internal/native host effects and shared state; complete non-process
   resource cancellation and aggregate resource accounting.
5. Implement independent code environments and required OTP services. Design
   loading now, but keep loader restructuring separate from boundary enforcement.
6. Integrate the deployment loader/router and edge lifecycle, then establish
   benchmark, fuzzing, platform, and independent security-review evidence for an
   explicit release go/no-go.

Track performance baselines, allocator-failure injection, reclamation stress,
x86-64 coverage, and security review separately from functional identity tests.
