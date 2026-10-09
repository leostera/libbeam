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

# RFD 0001: BEAM Realms for a single-node edge runtime

- **Status:** Archived experiment, removed from active `beam/`. Design/public API
  remain draft; never an approved security boundary. This RFD and its companion
  `0001-*` evidence refer to `archive/realm-snapshot` (`eb019d92`). Resolve their
  source links and commands in that historical checkout, not today's clean-upstream
  snapshot. None of these implementations or test counts describe today's emulator.
- **Embedding direction:** [RFD 0002](0002-libbeam-isolates.md) proposes host-managed
  isolates and a C++ library proof. It reframes the next implementation effort;
  it does not complete this RFD's open tasks or acceptance gates. Existing evidence
  predates the repository relocation under `beam/`.
- **Goal:** Complete the full M0–M5 RFD, including validation and the explicit
  security go/no-go—not only the current prototype API.
- **Source baseline:** `cca4e72510a97cfca6427602d3da8a22d5ff7a33`, reporting
  `30.0-rc0` in `OTP_VERSION`.
- **Scope:** Multiple isolated services within one BEAM instance.
- **Implementation:** Identity, delegated creation policy, process lifecycle, and
  subtree Realm/process counts, prepared startup, bounded host byte channels, and
  opt-in ordinary-send/process-control checks are prototyped. Complete isolation
  and independent code environments remain outstanding. See section 6.
- **Validation:** Source inspection and focused prototype tests. See the
  [implementation status](0001-implementation.md) for prototype scope and build/test
  instructions. Performance and security claims require the experiments below.

## 1. Motivation

BEAM offers inexpensive processes, preemptive scheduling of Erlang execution,
per-process garbage collection, message passing, and supervision. These are
attractive building blocks for an edge runtime that keeps the VM warm and
activates services on demand.

The missing primitive is an isolation unit smaller than a whole VM but larger
than an individual Erlang process. Today, independently operated services in a
shared BEAM instance share substantial authority and node-wide state. Separate
process heaps and supervision trees are not security boundaries. A service can
interact with other processes and shared facilities in ways inappropriate for
mutually untrusted deployments.

A **Realm** is proposed as this missing unit: a collection of processes and
resources with an immutable security identity, explicit external capabilities,
and an aggregate resource budget.

The intended platform experience is:

1. Build a versioned Erlang, Elixir, or Gleam deployment outside the serving VM.
2. Have a trusted control-plane program in the warm BEAM create a Realm with
   explicit policy and prepare its private code environment.
3. Load the deployment into that environment and activate its local supervision
   tree, without running user code with control-plane authority.
4. Deliver requests through explicitly granted endpoints.
5. Spawn ordinary BEAM processes within the Realm to perform work.
6. Stop the entire Realm and reclaim its resources when no longer needed.

This borrows the separation between runtime, deployment, invocation, and host
bindings seen in workerd-like systems. It does not attempt to reproduce the
JavaScript execution model or claim equivalent security or density in advance.

### Example workloads

- Request handlers with short-lived child processes and bounded background work.
- Small independently deployed OTP services sharing an edge machine.
- Stateful actor services activated by a platform and backed by external durable
  storage.
- Multiple versions of a service running concurrently during deployment.

A durable actor is not the Realm primitive. Durable identity, storage commits,
recovery, and ownership routing belong to the platform. A Realm can contain many
actors, and a process disappearing does not imply durable data disappearing.

## 2. Goals and non-goals

### Goals

- Preserve ordinary local messaging and supervision within a Realm.
- Prevent unauthorized tenant-to-tenant and tenant-to-host interactions.
- Separate deployment namespaces sufficiently to run independent services.
- Provide host-controlled creation, termination, inspection, and accounting.
- Make unsupported operations unavailable rather than implicitly privileged.
- Retain a cheap same-Realm path, measured against unmodified OTP.
- Keep existing behavior for a VM running only its trusted host environment.

### Non-goals

- **Distributed Realms.** A Realm belongs to exactly one BEAM instance for its
  entire lifetime. It cannot span nodes, be remotely joined, or migrate live.
- Transparent distributed Erlang between tenant processes.
- Checkpointing or restoring arbitrary process heaps.
- Durable storage, placement, replication, HTTP routing, or deployment tooling
  inside ERTS.
- Full compatibility with arbitrary existing applications at the first milestone.
- Arbitrary tenant NIFs, drivers, native libraries, or host OS access.
- Eliminating all timing, cache, scheduler, or resource-contention side channels.
- Protecting tenants from a malicious trusted host or a compromised VM.

Restarting a deployment on another machine creates a new Realm. Platform-level
state recovery may accompany that restart, but no Realm identity or live process
moves between BEAM instances.

## 3. Threat model and release boundary

The eventual adversary can execute tenant BEAM code, call accessible BIFs and
Erlang modules directly, retain or receive foreign PIDs and references, exercise
races, and intentionally exhaust resources. Elixir wrappers, hidden function
names, source-code conventions, and PID obscurity are not enforcement.

The trusted computing base includes ERTS, its loader and JIT, approved native
implementations, and host services that mediate capabilities. Native bugs remain
relevant even when tenant native-code loading is prohibited. Realms do not add
hardware memory protection inside the VM.

Initial prototypes run only trusted fixtures. Successful process-isolation tests
must not be described as permission to host hostile tenants. That requires an
operation inventory, resource enforcement, loader/native API review, adversarial
testing, and independent security review. OS sandboxing remains defense in depth;
separate VMs remain the fallback for workloads requiring a stronger boundary.

## 4. Proposed model

### 4.1 Realm, code environment, and endpoint

Keep three concepts distinct:

| Concept | Responsibility |
| --- | --- |
| Realm | Security identity, owned processes/resources, lifecycle, budgets |
| Code environment | Deployment-specific module resolution and code versions |
| Endpoint capability | Explicitly authorized communication or host operation |

Initially, Realms share trusted fixture code. Independent code environments are
a later milestone. Shared library code executes with the caller's authority;
calling a module associated with the host must not confer host privileges.

One deployment may have multiple Realm instances. A Realm is not a request, a
supervisor, an Erlang application, or a durable object. Request cancellation and
actor lifetimes can be implemented within this larger isolation unit.

### 4.2 Invariants

1. Every real process has a valid, immutable Realm identity before publication.
2. Ordinary local spawning inherits membership, including asynchronous spawning.
3. Tenant code cannot select another Realm through spawn options or process flags.
4. Possession of a PID, alias, ETS identifier, or other raw handle does not bypass
   Realm policy.
5. Same-Realm operations retain their ordinary semantics unless explicitly
   restricted by the runtime profile.
6. Cross-Realm operations require an explicit authorized path.
7. Code execution alone never switches the current authority.
8. Asynchronous operations retain the authorization context needed after their
   initiating process yields or exits.
9. Closing a Realm prevents new admissions and eventually reclaims owned work;
   killing its root supervisor alone is not sufficient.
10. Missing caller context is not equivalent to host authority.

### 4.3 Host authority

The initial VM environment is trusted host execution. Only host-authorized
operations can create Realms, grant endpoints, inspect across boundaries, and
terminate another Realm. Internal VM housekeeping has explicit operation-specific
permission; it is not modeled as unrestricted user messaging.

Prefer separate host-management entry points over silently making every normal
PID operation globally privileged. The host test harness needs a deliberate
management channel to inspect results and clean up fixtures.

All processes in a Realm share its authority in the initial design. The host may
explicitly delegate Realm creation to a deployment-manager Realm; ordinary
Realm creation is denied by default. Nested Realms must remain within ancestor
budgets and lifecycle control. Per-process capability attenuation is deferred.
Creation permission is not permission to access unrelated Realms or other
host-only VM facilities.

Realm support is enabled by default in this experimental branch. A separate
build-time switch is optional packaging work, not a substitute for runtime
policy enforcement.

### 4.4 API sketch, not a committed interface

Conceptually, the host needs operations resembling:

```erlang
{ok, Realm} = realm:create(#{allow_create_realms => false,
                             max_realms => 128,
                             max_processes => 4096}),
ok = realm:load(Realm, ValidatedBundle),
{ok, Root} = realm:spawn_root(Realm, Module, Function, Arguments),
Info = realm:info(Realm),
ok = realm:stop(Realm).
```

Names, return shapes, and public exposure are open. A first implementation may
use experimental BIFs declared in `bif.tab` with preloaded wrappers. An
`erts_internal` export is still callable code, not an authorization boundary.

This is a target public API, not a committed interface. The prototype supports
separate preparation and startup through `erts_internal:realm_create/1` and
`realm_spawn_root/4`, as well as combined creation/spawning through
`realm_spawn/3,4`. The host can install endpoint grants before the first process
starts. There is still no Realm-local loader. A trusted bootstrap inside the target Realm is another
possible loading arrangement, but loader callbacks such as `on_load` must execute
with that Realm's authority, never ambient control-plane authority.

The public Realm identity should be a canonical, opaque, runtime-issued value:
independent lookups of the same Realm must compare equal, different Realms must
compare unequal, and user-constructed terms must not manufacture a valid Realm
identity. Representation details are not a public interface. Identity and
management authority are separate: knowing the identity must not authorize
creation, loading, inspection, or termination. Settle the identity/management
handle relationship before committing the public API.

The prototype now provides canonical opaque tokens through
`erts_internal:realm_identity/0,1`, separate from magic-reference management
handles. Child snapshots can produce unequal management handles for the same
Realm; querying their identities produces equal tokens. Identity tokens carry no
management authority and do not retain Realm metadata after reclamation.
Numeric `realm_id/0` remains diagnostic, not the target public identity API.
The [prototype API contract](0001-api-contract.md) records authority, errors, and
lifetime semantics. Stable public API names and types remain unsettled.
Creation arguments and later endpoint payloads
need a policy for embedded mutable resources and function references; ordinary
term copying does not make every Erlang term an isolated value.

## 5. Source findings and implementation design

Paths in this section are relative to the repository root. Findings describe the
pinned baseline. Identity/lifecycle and initial process-policy changes are
recorded in snapshot `18708c2d4d`; section 6 and the implementation companion
distinguish implemented behavior from remaining design.

### 5.1 Process identity must precede process-table publication

Relevant files:

- `erts/emulator/beam/erl_process.h`: `struct process`, `ErlSpawnOpts`.
- `erts/emulator/beam/erl_process.c`: `erl_create_process()`, `alloc_process()`,
  `ErtsEarlyProcInit`, `early_init_process_struct()`, `erts_init_empty_process()`.
- `erts/emulator/beam/erl_ptab.c`: `erts_ptab_new_element()`.

**Finding:** `alloc_process()` inserts a process into the global process table.
The table invokes an initialization callback before publishing the pointer with
a release operation. Much of `erl_create_process()` initialization happens after
that insertion. Other paths use raw process lookup.

**Proposal:** Resolve the Realm and reserve admission before allocation. Pass the
membership through `ErtsEarlyProcInit` and set it in the early callback before
publication. Do not add an uninitialized field that policy checks can read while
process creation is in progress.

A candidate representation is an immutable pointer to an ERTS-owned `ErtsRealm`,
with a stable diagnostic identity, lifecycle state, and resource accounting.
Process ownership pins the Realm object. Timers, resources, management handles,
and asynchronous work may also pin it. Exact reference ownership and reclamation
must follow ERTS thread-progress and locking rules rather than relying on a raw
pointer surviving a yield.

Keep `Process.common` first and preserve the JIT-oriented hot-field layout as
far as practical. Measure padding and per-process memory impact.

`erl_create_process()` also accepts a null local parent for distributed creation.
The Realm runtime profile rejects tenant remote spawning; internal/bootstrap
creation must select explicit host context. It must not accidentally acquire
host authority merely because the parent pointer is null.

### 5.2 Dirty execution and pseudo-processes

`erts/emulator/beam/erl_nfunc_sched.h` constructs dirty shadow processes by
associating scheduler-owned storage with a real process and copying selected
fields. It does not copy the entire process structure.

Realm-sensitive native operations must resolve the real process or carry a
correctly refreshed identity in the shadow. Reused shadow storage must never
retain the previous tenant's identity. Pseudo-processes initialized by
`erts_init_empty_process()` require an explicit non-tenant classification; they
must not become a general bypass for authorization.

The first membership tests must cover these paths even if tenant native
functionality remains restricted.

### 5.3 Lifecycle and termination

Proposed lifecycle:

```text
creating -> active -> closing -> dead
```

- Creation reserves identity and initializes immutable, host-controlled or
  explicitly delegated policy.
- Activation admits the root and ordinary descendants.
- Closing atomically prevents new spawn/resource admissions and revokes endpoints.
- Teardown terminates member processes, cancels retained work, and releases
  resources through normal ERTS cleanup.
- Logical death and final memory reclamation are separate events.

**Finding:** Process exit in `erl_process.c` cleans registered names before
`erts_ptab_delete_element()`, and continues additional cleanup after marking the
process free. Realm cleanup must preserve existing monitor/link cleanup ordering.

Maintain a membership/admission mechanism that makes concurrent spawn and stop
well-defined. A spawn admitted before closing must be included in teardown;
one admitted after closing must fail. Use a reservation protocol or equivalent
synchronization, with rollback on allocation failure. Do not hold a Realm lock
while recursively driving process exits.

Realm references cannot be released merely when a PID disappears from lookup:
deferred signals, table-enumeration records, timers, or native cleanup may still
need identity. Determine the final-release point from an ownership audit.

Bounded termination is conditional on permitted native work. Arbitrary blocking
NIFs cannot be made safely preemptible by killing their Erlang process.

#### Creation authority and subtree containment

Ordinary spawn always inherits the current Realm. Creating a new Realm requires
an explicit `allow_create_realms` permission, denied by default. The trusted host
has creation authority; it may grant this permission to a deployment-manager
Realm. In this initial policy, granting creation permission also permits further
delegation and management of strict descendants, not the caller's own Realm,
ancestors, siblings, or unrelated Realms. A handle alone grants no authority.
These checks belong in ERTS, including the internal entry points.

Children may narrow inherited resource ceilings, never widen them. Resource
admission must charge every enclosing budget atomically; allocating a child Realm
must not provide fresh capacity outside its ancestor's budget. Closing an
ancestor prevents admission anywhere below it, and ancestor shutdown includes
child Realms even if their creating processes have exited.

The current prototype implements subtree limits for retained Realm objects and
reserved/live processes, using one lock per top-level subtree and a depth limit
of 64. It does not yet account for CPU, memory, or external resources, and its
unrestricted distribution/host effects are not a safe tenant profile. See the
[implementation companion](0001-implementation.md) for exact semantics and
validation. These count limits are not a completed security boundary.

### 5.4 Messaging and signals

Confirmed ordinary path:

```text
JIT ! -> send_2 -> erl_send -> do_send -> erts_send_message
interpreter ! -> erl_send -> do_send -> erts_send_message
```

- `erts/emulator/beam/jit/{arm,x86}/instr_bif.cpp`: `emit_send()`.
- `erts/emulator/beam/emu/bif_instrs.tab`: interpreter send instruction.
- `erts/emulator/beam/bif.c`: `do_send()` and send BIFs.
- `erts/emulator/beam/erl_message.c`: ordinary message copying and enqueueing.

Both JIT backends reach shared C code for ordinary sends. An initial messaging
policy should not require changing emitted assembly.

**Separate path:** aliases and priority sends use
`erts_proc_sig_send_altact_msg()` in `erl_proc_sig_queue.c`. It derives the target
PID from an alias reference and looks up the receiver. Realm authorization must
cover this path before copying/enqueueing a payload. Alias validity still follows
existing receiver-side semantics. A normal alias is not an automatic cross-Realm
capability.

Links, monitors, exits, group-leader changes, inspection requests, suspend/resume,
and other process-control operations also use signal machinery. Authorization
must precede caller-side effects where possible, including installing a link or
monitor. Rejection and races need correct rollback and native Erlang results.

Introduce a common policy vocabulary, but enforce it at operation-specific
boundaries. Do not blindly modify every raw PID lookup or drop every foreign
signal at the lowest queue layer: that can break cleanup, asynchronous replies,
and ordering. Audit all message producers, including `enif_send()` in `erl_nif.c`,
driver paths, and VM-generated messages.

Prefer checks before payload allocation and trace emission where feasible. Trace
output must not disclose interactions that are unauthorized in the first place.

### 5.5 Timers retain context

`erts/emulator/beam/erl_hl_timer.c`, `bif_timer_timeout()`, resolves registered
names at expiration using `erts_whereis_name_to_id(NULL, term)` and queues
messages directly. No live initiating process is required.

Timers therefore need Realm ownership, admission/accounting, and retained name
resolution context. Named timers must resolve within their originating Realm
at delivery time. Direct targets must be authorized at creation, with closing
and cancellation handled at delivery. Timer references must not grant foreign
cancellation or inspection rights.

Teardown must cover timers whose creating process has already exited. Testing
only timers created by a Realm's root misses this case.

### 5.6 Visibility and registration

- `erts/emulator/beam/register.c`: registration hash, lookup, unregister,
  enumeration, and process-exit cleanup.
- `erts/emulator/beam/erl_bif_info.c`: process inspection.
- `erts/emulator/beam/erl_ptab.c`: yielding table enumeration.

The registration hash currently compares names without Realm identity. Extend
its logical key to `(Realm, Name)`, retaining a single global table initially if
that simplifies locking. Registration must validate the target's membership;
unregister and process-exit cleanup must use the same key. Callers without a
process need explicit lookup context, not a default global namespace.

`processes/0`, process inspection, liveness, tracing, and any returned links,
monitors, or group-leader metadata need a consistent view. Setting the Realm
root's group leader requires a deliberate local I/O service or a granted broker;
inheriting the host shell's group leader is not an acceptable implicit bridge.

Process-table enumeration yields and tracks deleted elements to preserve its
semantics. Filtering only a final list by looking up still-live PIDs is not a
complete design. Options include retaining Realm identity in enumeration/deletion
metadata or implementing Realm membership enumeration with specified semantics.
Tests must exercise process death and creation while enumeration yields.

### 5.7 Error semantics

Default proposal: foreign targets behave like unavailable targets under the
corresponding Erlang operation, rather than disclosing another Realm's existence.
This is not one universal exception:

- A send to an inaccessible PID can follow dead-PID send behavior.
- Name lookup can return `undefined`; name-based sends follow missing-name rules.
- Monitoring can report `noproc` without establishing a foreign monitor.
- Process inspection can return `undefined`.
- Explicit host-management operations can report authorization errors.

Finalize a per-operation table before enforcing these rules. Validate it against
the existing BIF contracts, including aliases and asynchronous operations.
Host diagnostics should explain rejected operations without exposing foreign
state to the tenant. No claim of timing indistinguishability is made.

### 5.8 ETS and persistent state

- `erts/emulator/beam/erl_db_util.h`: `DbTableCommon`.
- `erts/emulator/beam/erl_db.c`: `db_get_table_aux()` and table operations.
- `erts/emulator/beam/erl_bif_persistent.c`: global persistent-term storage.

ETS needs Realm ownership independent of its process owner. A public table is
public within that Realm. Scope names, enumeration, information, transfer, heirs,
continuations, and cleanup, not just lookup by name. A leaked table identifier
must remain unusable across the boundary.

`persistent_term` needs Realm-local keys and lifetime, or must remain disabled
for tenants until implemented. Namespace separation alone does not eliminate
VM-wide performance effects of updates; measure and constrain them.

Mutable resources such as atomics, counters, and native resource references need
an explicit sharing policy. The initial endpoint payload profile should reject
unsupported mutable handles and executable closures rather than accidentally
sharing them across Realms.

### 5.9 Host effects and native authority

A BIF/native operation inventory must classify each operation as:

1. Pure or process-local.
2. Realm-local with an ownership check.
3. Capability-mediated.
4. Host-only or unsupported for tenants.

Include global VM flags, shutdown, environment access, tracing, code loading,
ports, drivers, filesystem, networking, and resource decoding/creation.

Blocking `load_nif/2` and `open_port/2` is insufficient. Already-loaded APIs such
as `prim_file` call native functions, and direct internal calls can bypass a
high-level Erlang wrapper. The enforcement point must dominate every supported
entry path, including JIT-specialized calls and dirty execution. There is no
assumption that one universal BIF dispatcher covers all effects.

Host brokers must bind requests to runtime-authenticated grants. A tenant-supplied
Realm ID or claimed sender PID is not authentication. Do not give a Realm
unrestricted access to powerful OTP service mailboxes. Resource handles returned
by brokers need revocation, cleanup, and budget semantics.

### 5.10 Single-node policy, not distributed implementation

The initial edge runtime profile runs without distributed Erlang. Tenant remote
spawn, remote sends, node connections, and distribution setup are unavailable.
No distribution protocol changes or cross-node Realm identities are proposed.

Turning distribution off at startup is not the entire policy: tenant code must
also be unable to enable it through internal APIs or native/port access. If a
future host profile enables distribution for administration, ingress and remote
process creation must be explicitly unable to address tenant Realms. That is a
separate compatibility/security project, not part of this RFD's implementation.

### 5.11 Independent code environments

Relevant files include `module.c`, `export.c`, `code_ix.c`, `code_ix.h`, loader
implementations, and `jit/{arm,x86}/instr_call.cpp`, under
`erts/emulator/beam/`.

**Finding:** Module structures remain node-wide and indexed by the active/staging
code machinery. JIT external calls use loaded export references. Changing only
`code_server` lookup or prefixing a name registry cannot isolate already-bound
calls. Active/staging code indices are publication generations, not available
Realm slots.

A code-environment design must specify:

- Environment-aware module/export resolution at loading and dynamic calls.
- Bound imports, `apply`, local/external funs, literals, and error-handler paths.
- Scope of loading, deletion, purge, upgrades, and code inspection.
- Lifetime of code referenced by running processes and delayed cleanup.
- Safe sharing of standard-library code between environments.
- JIT and interpreter behavior, including cached code-index state.

The first independent-deployment experiment should load two implementations of
the same module name, call both through static imports and dynamic paths, and
upgrade one without changing the other. Until this passes, Realms share fixture
code and are not independent deployment namespaces.

Module-name rewriting may be useful for an experiment but is not the security
design: dynamic names, function references, and OTP conventions complicate it.

### 5.12 OTP services and resource accounting

`lib/kernel/src/application_controller.erl` registers one controller and creates
the named `ac_tab` ETS table. Application lifecycle and configuration therefore
need Realm-local services or mediated equivalents. Code loading, logging,
application environment, and group-leader I/O need a similar review. Compatibility
with an Elixir application is an acceptance experiment, not an initial guarantee.

Realm budgets must aggregate children. Start with process and timer admission
limits, then measure and account for heaps, mailboxes, ETS, binaries, code,
persistent terms, and native resources. Admission reservations must roll back
correctly on every failed allocation.

Reductions provide scheduling/accounting input, not a fixed CPU-time unit.
Per-process fairness does not prevent a tenant from gaining CPU share by spawning
many processes. Evaluate Realm-level scheduling/accounting and dirty work limits.
Per-process `max_heap_size` is not an aggregate tenant memory quota.

Shared immutable binaries and code require a charging policy. Decide whether to
charge full logical retention, attribute physical ownership, or use a documented
hybrid. Limit overshoot and handling of allocator failure must be explicit.

Atoms are globally interned. Quotas on newly created atoms do not automatically
hide atom existence or reclaim atoms after a Realm dies. Initially prohibit or
strictly mediate dynamic tenant atom creation, including indirect creation through
decoding and loading. Long-lived deployment churn needs its own atom-growth
strategy; full atom namespace virtualization is an open design problem.

## 6. Milestones and acceptance criteria

Each milestone is an independently reviewable step. They do not individually
establish a hostile-code sandbox.

### Current implementation versus completion

This ledger describes the prototype snapshot, not completed milestone acceptance.
Keep it synchronized with the
[implementation and validation record](0001-implementation.md) and the
[eight-package execution checklist](0001-work-plan.md). The checklist tracks
remaining tasks, dependencies, acceptance gates, and per-batch evidence.

| Milestone | Implemented evidence | Remaining to finish |
| --- | --- | --- |
| **M0 — Partial** | Pinned upstream baseline; checked-in validation runner; fresh upstream/snapshot optimized/debug JIT builds; incremental interpreter coverage; focused regressions; C-node fixture failure isolated and repaired; adopted core contracts/public API; initial operation inventories and repeated optimized-JIT benchmarks | Complete native audit/profile compatibility, broader performance/fairness data and threshold agreement, regression clearance, and executed platform CI |
| **M1 — Substantially implemented** | Canonical opaque identity tokens distinct from management handles; separate empty-Realm creation and authorized root startup; immutable pre-publication membership; inherited spawning; default-deny creation policy and explicit delegation; ancestor-only management; admission rollback; subtree closure and resumable process shutdown; deferred native cleanup handling | Stable public API; further ownership/failure-path validation and reclamation stress; no claim of isolation |
| **M2 — Partial** | Membership and subtree shutdown; bounded host byte channels; endpoint-only restricted fixtures; opt-in PID/name/alias/priority send and direct process-control/inspection checks; rejection of unsupported global operations and covered outgoing distribution APIs | Complete indirect/asynchronous/native paths; scoped registration, enumeration, timers and tracing; migrate legacy fixtures and make restrictions default; enforce single-node profile including incoming distribution and activation races; broader bypass/concurrency tests |
| **M3 — Mostly outstanding** | Subtree process and retained-Realm count limits; bounded endpoint queues and retained endpoint count; host grant/revocation checks and queue teardown; tenant system-process creation denied | BIF/internal/native authority enforcement, including already-loaded native capabilities; ports and filesystem/network effects; ETS/persistent-term isolation or denial; general endpoint delegation, broker rights, replies/deadlines/cancellation; memory/CPU/dirty-work budgets and complete non-process resource teardown |
| **M4 — Outstanding** | Realms execute shared trusted fixture code only | Realm-aware loading and call resolution; independent module versions, upgrades, funs, purge and code reclamation; application configuration, logging and group-leader I/O services; conflicting Elixir deployments |
| **M5 — Outstanding** | No edge integration or security certification | Deployment loader/router, deadlines, cancellation, activation/eviction, observability, storage-backed actor demonstration; density/fairness/churn benchmarks, fuzzing, supported-platform coverage and independent security review |

The largest functional gap for the deployment platform is **private code
environments**: loading code from a Realm still affects the shared VM code
environment. The largest safety gap is **authority enforcement**: most operations
do not yet respect Realm membership.

The count budgets are not aggregate resource isolation. Remaining accounting
includes heaps, mailboxes, shared binaries, ETS, timers, code, persistent terms,
atoms, native resources, CPU fairness, and dirty work. Each needs explicit
charging, exhaustion, overshoot, and reclamation behavior. Similarly,
`realm_stop/1` currently stops tracked process subtrees and drains endpoint
transport queues. It does not cancel timers, broker work already consumed from
an endpoint, external effects, or all other retained asynchronous work.

The initial host-channel profile is pull-based, accepts only copied binaries,
and binds each direction to the host and exactly one Realm. It has bounded
message/byte capacity, explicit revocation, and ancestor-close handling. It is
not yet a general RPC or host-effects broker. Its exact limits, linearization,
authority, and reclamation semantics are in the
[API contract](0001-api-contract.md). The experimental public `realm` module now
wraps the native primitives without changing caller authority; five public API
cases pass on optimized JIT, debug JIT and debug interpreter. Adopted
[W1 decisions](0001-contract-decisions.md) and
[code-environment semantics](0001-code-environment-contract.md) guide subsequent
implementation but do not establish runtime or security acceptance. The
[next-five evidence](0001-next-five-evidence.md) adds broader regression discovery,
matched performance comparisons and source-bound native design reviews; concrete
shared-state/timer bypasses and elevated spawn tails were recorded as no-go
findings. The subsequent [shared-resource implementation](0001-shared-resource-enforcement.md)
adds actual array ownership checks and persistent-term profile denial, without
claiming timer/native closure, resource budgets or any package acceptance. The
[initial process-operation matrix](0001-operation-policy.md) defines opt-in
ordinary-message/control checks, management exceptions, temporary denials, and
known alternate paths. Legacy Realms default to unrestricted behavior during
fixture migration. Neither profile is a sandbox.

The process-boundary batch adds ten restricted cases to the original 45 Realm
tests: **55 pass on optimized JIT, debug JIT, and debug interpreter on ARM64
macOS**, plus **84 focused debug regressions** covering processes, monitors,
registration, timers, dirty BIFs, signals, and legacy/dynamic tracing.
The earlier `process_SUITE:spawn_against_ei_node/1` failure was reproduced on fresh
upstream and snapshot builds and isolated to a hostname-canonicalization assumption
in the C-node fixture. The fixture repair passes on all three current variants and
on the unchanged upstream runtime; see the [W1 validation record](0001-validation.md).
A subsequent full debug process run reports 101 passed and three classified
skips; broader discovery also found signal/distribution timetraps and test-harness
issues. The signal timetrap reproduces on clean upstream. The
[native inventory seed](0001-native-inventory.md) records 570 declared BIFs, all
still awaiting complete per-operation policy review. These results are not a fully
passing emulator regression suite. Initial repeated optimized-JIT host/restricted
[measurements](0001-benchmarks.md) now exist; accepted thresholds, broader workload/
platform coverage, general allocator/OOM fault injection, sanitizers and independent
security review remain absent. Passing tests do not establish a
hostile-code sandbox.

### M0: Contract and build baseline

- Pin the source revision and document the supported runtime profile.
- Build optimized and debug JIT variants; establish interpreter coverage.
- Run existing process, signal, monitor, timer, and registration tests.
- Record spawn/message throughput, latency, and process memory baseline.
- Create an operation inventory and per-operation error-semantics table.

**Exit:** Reproducible local build/test instructions and measured baseline. This
RFD alone does not satisfy that exit condition.

### M1: Identity, admission, and lifetime

- Add runtime-owned Realm metadata and pre-publication process membership.
- Establish canonical opaque identity, separate management authority, and the
  public API/error contract; retain diagnostic IDs only as diagnostics.
- Add host-authorized creation and inspection operations, including explicit
  delegation with default-deny policy and no widening of inherited ceilings.
- Cover ordinary spawn variants, async spawn, bootstrap, and shadow processes.
- Establish closing/admission synchronization and reference ownership, including
  descendant Realms and ancestor budget/lifecycle containment.
- Keep trusted host-only execution compatible.

**Exit:** Tests prove immutable membership and inheritance, admission rollback,
and no tenant-controlled selection of host identity. No isolation claim yet.

### M2: Process compartments

- Cover ordinary, alias, and priority messaging and process-control operations.
- Scope registered names, enumeration, inspection, timers, and tracing.
- Keep distribution unavailable and test attempted activation/bypass.
- Provide a narrow host test channel and complete Realm stop behavior.

**Exit:** Two fixture Realms run supervision trees; deliberately exchanged foreign
PIDs and aliases cannot authorize interactions. Spawn/exit/timer/stop races pass.
Shared-state and native-authority gaps remain explicitly documented.

### M3: Restricted services and resource isolation

- Enforce the BIF/native operation inventory, including direct internal calls.
- Scope ETS and persistent terms, or deny unsupported facilities.
- Introduce endpoint grants and bounded broker requests.
- Implement aggregate admission/accounting and a defined exhaustion response.
- Exercise complete teardown of resources and pending asynchronous work.

**Exit:** A documented restricted service profile passes authority and
resource-exhaustion tests. All unimplemented operations are rejected or explicitly
listed as trusted-fixture-only limitations. This is not a production security
certification.

### M4: Independent deployment environments

- Specify and implement environment-aware loading and call resolution.
- Isolate application services and configuration needed by the supported profile.
- Test independent module versions, code upgrades, funs, and code reclamation.
- Run a representative small Elixir service twice with conflicting module names
  and configuration.

**Exit:** Two services coexist and upgrade independently without shared mutable
configuration or unintended code replacement.

### M5: Edge host integration and hardening

- Integrate an external request router and deployment loader.
- Add request deadlines, cancellation, observability, and activation/eviction.
- Demonstrate a storage-backed actor whose data survives Realm replacement.
- Benchmark startup, steady-state memory, tenant fairness, and teardown at scale.
- Complete independent security review, fuzzing, and supported-platform testing.

**Exit:** An explicit go/no-go decision for untrusted workloads, with documented
limits, compatibility scope, and residual risks. Durable placement, replication,
and routing remain platform responsibilities.

## 7. Test and measurement plan

Add `erts/emulator/test/realm_SUITE.erl`. Reuse existing process, signal, monitor,
timer, registration, and trace-session suites for regressions. Test both
`FLAVOR=jit` and `FLAVOR=emu`, debug and optimized builds; cover ARM64 locally and
x86-64 in CI when available.

The core matrix includes:

| Area | Positive case | Adversarial case |
| --- | --- | --- |
| Spawn | All local variants inherit | Select host identity; spawn during stop |
| Messaging | Same-Realm PID/name/alias | Foreign PID/alias, priority send, local-node tuple |
| Control | Local links/monitors/exits | Foreign kill, suspend, inspect, trace |
| Timers | Local delayed delivery | Creator exits; name rebinds; Realm closes |
| Visibility | Local enumeration | Yield while foreign processes start/exit |
| ETS | Local public/private behavior | Foreign ID, transfer, heir, continuation |
| Endpoints | Granted operation | Forged sender, revoked grant, mutable handle |
| Host effects | Approved broker | Direct BIF/native/internal call bypass |
| Code | Independent versions | Cross-environment fun/import or purge |
| Budgets | Work within limits | Spawn/mailbox/ETS/binary/atom exhaustion |
| Teardown | All work completes or cancels | Repeated create/stop with pending native work |

Additional requirements:

- Test aliases that deactivate and monitors that race with target exit.
- Exercise on-heap and off-heap queues and tiny reduction budgets where supported.
- Verify both absence of foreign delivery and preservation of required local
  cleanup signals; lack of a reply alone is not a sufficient assertion.
- Test malformed and stale Realm handles and denied operations without side effects.
- Run host-only regression tests to detect accidental changes to normal OTP.
- Track retained resources across repeated deployment churn, not only live counts.
- Bound test waits so a broken authorization path fails rather than hangs.

Measure spawn cost, small/large message throughput, alias/priority overhead,
registration/ETS access, p50/p99 latency under a noisy neighbor, code-load time,
Realm activation/termination, and physical/logical memory. Establish acceptable
regression thresholds from baseline data; this RFD invents no density claims.

## 8. Build and implementation workflow

Follow [DEVELOPMENT.md](../../beam/HOWTO/DEVELOPMENT.md),
[INSTALL.md](../../beam/HOWTO/INSTALL.md), and [TESTING.md](../../beam/HOWTO/TESTING.md).
A starting workflow, subject to configuring local dependencies, is:

```sh
export ERL_TOP="$PWD"
./otp_build configure
make
make TYPE=debug
make emulator_test TYPE=debug ARGS="-suite process_SUITE"

# After adding realm_SUITE:
make emulator_test TYPE=debug ARGS="-suite realm_SUITE"

# Build the entire requested flavor before running its tests:
make TYPE=debug FLAVOR=emu
make emulator_test TYPE=debug FLAVOR=emu ARGS="-suite realm_SUITE"
```

Use binaries from this checkout, not an installed system OTP, when validating VM
changes. Preloaded source changes require the documented preloaded update and a
rebuild; use `./otp_build update_preloaded --no-commit` when appropriate rather
than silently committing generated artifacts. Tests with native helpers may need
the released-test workflow described in the testing guide.

The [execution checklist](0001-work-plan.md) expands the remaining work into eight
packages with stable task IDs and acceptance gates. It is the task-level tracker;
start the private-code feasibility investigation early alongside boundary work.
The high-level implementation sequence remains:

1. **Finish the identity and API contract:** canonical opaque identity and the
   experimental creation/delegation contract are implemented; settle stable public
   names/types and complete per-operation rejection semantics.
   Expand the initial performance baseline and regression clearance
   alongside this work; the initial C-node fixture failure is now resolved.
2. **Finish fixture migration:** bounded byte channels, prepared startup,
   endpoint-only restricted fixtures, and an initial rejection table now exist.
   Migrate remaining legacy cross-Realm fixture traffic and settle public API
   decisions before making process restrictions the default.
3. **Complete process compartments:** extend the opt-in ordinary/alias/priority
   send and process-control checks to remaining alternate paths. Implement scoped
   names, timers, enumeration and tracing rather than temporary blanket denials;
   enforce the single-node profile, including incoming distribution. Preserve
   same-Realm behavior and VM cleanup; test spawn/exit/timer/stop and bypass races.
4. **Restrict host effects and shared state:** mediate internal/native entry
   points, scope or deny shared facilities, complete resource teardown, and add
   aggregate resource accounting with defined exhaustion behavior.
5. **Implement private code environments and OTP services:** design the loader
   now, but implement its restructuring separately from process-boundary patches.
   Validate independent loading, configuration, upgrades, and code reclamation.
6. **Integrate and harden the edge host:** loader/router and activation lifecycle,
   deadlines/cancellation, observability and durable-state demonstration; then
   noisy-neighbor/churn benchmarks, fuzzing, platform coverage, and security review.

Use compileable, testable commits. This sequence does not defer security review
until the end: review authority and ownership at every step. M5 supplies the
independent review and explicit release decision.

**RFD completion means** independently loaded services can coexist and upgrade,
remain within their granted authority and aggregate budgets, and shut down with
all owned work reclaimed or accounted for. This must be supported by measured
behavior, compatibility evidence, and an explicit security go/no-go with residual
risks documented—not merely by the presence of the APIs.

## 9. Alternatives and unresolved decisions

### Alternatives

- **Separate BEAM instances in OS sandboxes:** Stronger existing separation and
  broad application compatibility; higher per-deployment overhead. This remains
  the production fallback, not an obsolete option.
- **OTP applications/supervision only:** Useful organizational boundaries but do
  not restrict arbitrary tenant code or node-wide state.
- **An Erlang-level sandbox wrapper:** Cannot mediate direct BIF/native calls or
  all shared facilities. Useful for ergonomics, insufficient as enforcement.
- **Restricted language or bytecode interpreter:** Potentially smaller exposed
  surface, but changes compatibility and performance assumptions. Remains an
  alternative if modifying ERTS authority proves too broad.

### Decisions still requiring experiments or review

1. Exact Realm object ownership, membership indexing, and lock order.
2. Error behavior for every denied operation and asynchronous reply.
3. Endpoint representation, delegation, revocation, and allowed payload terms.
4. Root I/O/application-service bootstrap without ambient host access.
5. Shared binary/code charging, CPU fairness, and maximum quota overshoot.
6. Atom visibility, dynamic creation, and long-lived deployment churn.
7. Code-environment representation and safe sharing of loaded library code.
8. Teardown guarantees for permitted native operations and external side effects.
9. Compatibility policy for existing OTP libraries and Elixir releases.
10. Whether the measured benefit warrants maintaining the expanded ERTS security
    surface compared with warm, separately sandboxed BEAM instances.

## 10. Further source reading

- [BeamAsm](../../beam/erts/emulator/internal_doc/BeamAsm.md): JIT execution and calling
  conventions.
- [Process and port tables](../../beam/erts/emulator/internal_doc/PTables.md): lookup,
  publication, and lifetime.
- [Thread progress](../../beam/erts/emulator/internal_doc/ThreadProgress.md): deferred
  reclamation and synchronization.
- [Code loading](../../beam/erts/emulator/internal_doc/CodeLoading.md): code publication
  model; corroborate details against the pinned implementation.
- [Tracing](../../beam/erts/emulator/internal_doc/Tracing.md): tracing internals.
- [Contribution guide](../../beam/CONTRIBUTING.md): upstream changes of this scope require
  design discussion and potentially an EEP; this draft is not upstream approval.
