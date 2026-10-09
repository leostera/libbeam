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

# BEAM Realms: remaining implementation checklist

This is the execution checklist for completing [RFD 0001](0001-beam-realms.md),
starting from snapshot `18708c2d4d`. It expands the eight agreed work packages
into implementation and acceptance tasks. It does not replace the RFD, the
[API contract](0001-api-contract.md), the
[operation-policy matrix](0001-operation-policy.md), or the
[implementation/validation record](0001-implementation.md).

**Current security decision: no-go for untrusted workloads.** The snapshot is a
functional prototype, not an isolation boundary. The goal remains the full
M0–M5 RFD. Distributed Realms and live migration remain out of scope; deployment
routing and durable placement/replication belong to the external platform.

## How to use this checklist

- Stable IDs identify work across commits, reviews, tests, and follow-up tasks.
  Do not renumber existing IDs when adding discoveries.
- `[ ]` means acceptance is outstanding, including partially implemented work.
  `[x]` means the stated result has evidence. Eighteen tasks are verified,
  including adopted contract/specification tasks; 105 remain open. None of the
  eight package acceptance gates is complete. A completed specification does not
  imply implementation or security acceptance.
- Work in reviewable, buildable increments. A work package is not necessarily one
  commit. Record active task IDs and blockers in the execution log below.
- Before implementing a task, resolve its policy/ownership decisions and identify
  positive, negative, failure, and race tests. Inventory discoveries become new
  tasks; this list is not proof that every runtime path has already been found.
- On completion, check the item and append evidence: implementation commit,
  named tests, exact commands/build variants, results, and remaining limitations.
  Keep durable summaries in the repository or CI; `/tmp` logs alone are not a
  reproducible long-term record. Do not commit credentials or private workloads.
- A denial is an interim solution where the RFD promises a scoped facility. Do
  not mark scoped namespaces, timers, code, or OTP compatibility complete merely
  because their operations return `badarg`. A final scope reduction requires an
  explicit RFD/API decision, rationale, and revised acceptance criteria.
- Do not silently skip a blocked task or turn a failed security gate into a pass.
  Record a no-go or approved scope change. Runtime-enforced denial of deliberately
  unsupported features must itself have tests.

### Run cadence and active batch

- Work in batches of approximately 20 dependency-ordered tasks, rather than
  ending a run after each small tooling or documentation change. Continue through
  the active batch until completed or genuinely blocked; record blockers explicitly
  and proceed with independent tasks where possible.
- Before ending a run, commit all completed source, tooling, tests, and documentation
  changes in reviewable commits. Exclude generated build artifacts, including
  preloaded BEAMs. Do not push without authorization.
- End with a tweet-length summary of completed work, commit IDs, validation results,
  and blockers. This is a status summary, not authorization to publish externally.
- The next 20-task batch is **W1-05–17 and W2-01–07**. Existing partial work counts
  toward these tasks but does not close them without their required evidence.
  External CI, measurements, threshold agreement, and independent review cannot
  be replaced by local tooling or assertions of completion.

### Definition of done for an implementation task

Every implementation task below must satisfy these conditions, as applicable:

1. Public, internal, optimized, interpreter, and asynchronous entry paths have
   been considered; authority comes from execution context or explicit grants.
2. Ownership, admission, rollback, close/revoke races, and final release points
   are specified. No raw PID-lookup or signal-queue blanket filter breaks cleanup.
3. Positive local behavior and adversarial foreign behavior are tested. Assert
   both the error/result and absence of unauthorized effects, not just a timeout.
4. Malformed, stale, leaked, forged, revoked, and wrong-kind handles are tested
   where relevant. Test ancestor, descendant, sibling, unrelated, and host scope.
5. Focused existing OTP regressions and applicable runtime variants pass; failure
   reports distinguish pre-existing failures from new ones with evidence.
6. RFD status, API/operation contracts, validation evidence, and this checklist
   agree. Performance-sensitive changes receive a baseline comparison.

### Existing evidence, not remaining-task acceptance

- [x] BASE-01 — Snapshot identity, immutable pre-publication membership, creation
  policy, ancestor counts, prepared roots, and process lifecycle implementation
  in `18708c2d4d`; see the implementation record for exact supported semantics.
- [x] BASE-02 — Snapshot bounded, caller-bound, binary-only host endpoints and
  opt-in ordinary-message/process-control checks. Restrictions are not default.
- [x] BASE-03 — Record 55 Realm cases passing on ARM64 optimized JIT, debug JIT,
  and debug interpreter, plus 84 focused debug regressions at the snapshot.
  The snapshot's historical C-node failure is resolved by the W1-04 fixture
  repair below; missing broader evidence remains open.

## Dependencies and execution order

| Package | RFD coverage | Start after / coordination |
| --- | --- | --- |
| W1: Baseline and contracts | M0, remaining M1 contract | Start now; maintain throughout |
| W2: Code-environment feasibility | Early M4 investigation | Start alongside W1/W3 once core contracts are recorded |
| W3: Process compartments | M2, M1 hardening | Use W1 operation decisions; coordinate resource ownership with W5 |
| W4: Shared state/native effects | M3, M2 alternate paths | Use W1 inventory; implement alongside W5 ownership and W6 charging |
| W5: Endpoints/resource ownership | M1–M3 lifecycle | Specify ownership early; complete each resource adapter with W3/W4/W7 |
| W6: Budgets/fairness | M3 | Baselines from W1 and ownership hooks from W5; feed limits into W3/W4/W7 |
| W7: Deployment environments/OTP | M4 | W2 feasibility decision; boundaries, ownership, and budgets integrated before acceptance |
| W8: Edge integration/release | M5 | Integration fixtures may start early; release requires all preceding gates |

The numbers are workstream identities, not a requirement to finish every task in
one package before touching another. In particular, investigate code feasibility
now, establish ownership before enabling effects, and review security throughout.
Untrusted deployment is not permitted by any intermediate functional gate.

## W1 — Establish the baseline and close contract decisions

### Reproducibility and regressions

- [x] W1-01 — Add a checked-in runner for Realm suites and focused OTP regressions,
  with exact build variants, timeouts, nonzero failure exits, and result summaries.
- [x] W1-02 — Document clean-checkout configure/build/preloaded regeneration steps,
  toolchain versions, optional dependencies, and separation of source from generated
  artifacts. Verify a fresh build, not only incremental builds.
- [x] W1-03 — Reproduce `process_SUITE:spawn_against_ei_node/1` on the pinned upstream
  baseline and snapshot using matching toolchains, cookies, and network settings.
- [x] W1-04 — Resolve or isolate the C-node failure with a minimized reproducer,
  root-cause evidence, and regression test; document any remaining blocker.
- [x] W1-05 — Run broader process, signal, monitor, timer, registration, trace,
  distribution, code-loading, ETS, and native regressions; classify every failure
  and skip rather than reporting only passing selections.
- [ ] W1-06 — Add CI coverage for ARM64 and x86-64, JIT and interpreter, debug and
  optimized builds as supported; explicitly record any unavailable matrix cells.

### Measurements and contracts

- [x] W1-07 — Add repeatable benchmarks for spawn/exit, small and large messages,
  aliases/priority traffic, registration/ETS, Realm activation/stop, and baseline
  process/VM memory. Record machine, configuration, repetitions, and dispersion.
- [ ] W1-08 — Measure host-only and restricted workloads against unmodified OTP;
  agree acceptable overhead and p50/p99 latency thresholds from actual data.
- [ ] W1-09 — Inventory BIFs, internal exports, JIT/interpreter special paths,
  preloaded wrappers, drivers/NIFs, system services, and deferred message producers.
  Record source anchor, caller context, target/resource, effect, and cleanup path.
- [ ] W1-10 — Classify each inventory entry as Realm-local, management-only,
  explicitly brokered, or denied; specify use-time checks, asynchronous replies,
  errors, absence semantics, and information exposure. Keep unknowns visible.
- [x] W1-11 — Finalize identity versus management handle versus endpoint authority,
  policy attenuation, nested lifecycle, depth limits, and stale-handle contracts.
- [x] W1-12 — Decide whether ordinary process-control APIs retain manager exceptions
  or management requires exclusively explicit scoped APIs; specify host diagnostics.
- [ ] W1-13 — Define the first supported tenant profile: OTP/language versions,
  required services, unsupported facilities, native allowlist policy, and how a
  production profile excludes legacy unrestricted tenant creation.
- [x] W1-14 — Specify global metadata/time visibility, existence side channels,
  timing-channel limitations, diagnostic redaction, and threats outside scope.
- [x] W1-15 — Define the resource ownership/charging vocabulary used by W4–W7:
  reservations, live usage, retained objects, shared charges, in-flight work,
  logical completion, and physical reclamation.
- [x] W1-16 — Choose public API/module names, types, return/error conventions, and
  compatibility/versioning policy; remove dependence on undocumented internal API
  names for consumers before release, without treating naming as authorization.
- [x] W1-17 — Establish a security review checklist for each authority/lifetime
  change, with recorded findings and owners before the final independent review.

**Acceptance**

- [ ] W1-G — Reproducible baseline and benchmark reports exist; regression failures
  are resolved or explicitly block release; the supported profile and complete
  operation dispositions are documented and traceable to implementation/tests.

## W2 — Investigate private code environments early

### Architecture and feasibility

- [x] W2-01 — Specify code-environment identity, ownership, sharing, and lifetime
  separately from Realm identity; define the Realm-to-environment relationship.
- [ ] W2-02 — Map global module/export tables, code indices, imports, JIT direct
  calls/caches, interpreter dispatch, fun tables, loader state, and purge machinery.
- [x] W2-03 — Define resolution for local/external calls, `apply`, dynamic module
  calls, BIFs, imports, undefined-function handling, and stack/exception metadata.
- [x] W2-04 — Define immutable shared platform code and deployment-owned code;
  specify how shared OTP code resolves tenant callbacks without acquiring authority.
- [x] W2-05 — Specify fun environment binding, equality, capture, invocation,
  serialization, and cross-environment rejection/sharing rules, including root
  startup arguments and closures that currently come from trusted host fixtures.
- [x] W2-06 — Specify loading transactions, concurrent loads, initialization failure,
  `on_load` authority, failed activation cleanup, and prohibition of global tenant
  code replacement through direct loader/internal calls.
- [x] W2-07 — Specify current/old code versions, upgrades, soft/hard purge, live
  stacks/funs, literal ownership, and environment destruction/reclamation.
- [ ] W2-08 — Build a minimal environment-aware prototype loading conflicting module
  names simultaneously; do not substitute module-name rewriting for resolution.
- [ ] W2-09 — Exercise imports, dynamic calls, funs, and callback resolution in both
  JIT and interpreter; identify architecture-specific work for ARM64/x86-64.
- [ ] W2-10 — Estimate lookup/call overhead, per-environment metadata, shared-code
  savings, and churn/reclamation costs from prototype measurements.
- [ ] W2-11 — Record a reviewed feasibility decision, unresolved risks, and concrete
  W7 implementation slices. If infeasible, explicitly revisit the RFD rather than
  quietly reverting to one global code namespace.

**Acceptance**

- [ ] W2-G — Two environments concurrently execute different implementations of
  the same module/function through genuine environment-aware dispatch. Evidence
  and a reviewed path to upgrades, reclamation, and OTP compatibility are recorded.
  This prototype alone does not complete W7.

## W3 — Finish process compartments

### Defaults, admission, and process operations

- [ ] W3-01 — Inventory remaining trusted closure and ordinary cross-Realm fixture
  traffic; migrate reporting/bootstrap to controlled channels. Specify nested
  manager fixture transport without adding an implicit messaging exemption.
- [ ] W3-02 — Enable restrictions by default for new non-host Realms after migration;
  keep child attenuation enforced and prevent tenant selection of a weaker profile.
  Decide removal or host-only test containment of the legacy profile.
- [ ] W3-03 — Re-audit ordinary/async spawn, root creation, bootstrap/system processes,
  dirty shadows, allocation rollback, and all process-publication paths. Test that
  missing execution context never becomes implicit host authority.
- [ ] W3-04 — Complete send-source coverage, including aliases, monitor aliases,
  priority, named/tuple destinations, native/driver producers, and deferred replies;
  assign W4 ownership to alternate native paths and prove no unchecked producer.
- [ ] W3-05 — Complete link, monitor, demonitor, exit, suspension, flag, group-leader,
  liveness/inspection, GC, code-check, and system-task entry-path audits. Validate
  explicit reply destinations and preserve authenticated VM cleanup semantics.
- [ ] W3-06 — Test stale/missing PIDs and references, deactivated aliases, target exit,
  denied priority operations, both queue modes, and tiny reduction budgets where
  supported; reject before unauthorized effects or alias consumption.

### Scoped services

- [ ] W3-07 — Implement Realm-local register/unregister/whereis/registered namespaces,
  name ownership, atomic collision behavior, owner exit cleanup, and teardown.
- [ ] W3-08 — Route named sends and monitors through the correct namespace; test name
  reuse/rebinding and local-node tuple forms without foreign target substitution.
- [ ] W3-09 — Implement scoped process/port enumeration and continuations, including
  deletion tracking and yields. Filter during construction, not only the final list.
- [ ] W3-10 — Define and enforce process/port/node/global diagnostic visibility;
  keep host management observability explicit and separate from tenant enumeration.
- [ ] W3-11 — Give timers retained Realm ownership independent of creator lifetime;
  specify target/name binding, admission, queueing, expiry, and memory charges.
- [ ] W3-12 — Enforce timer authorization at creation, read/cancel, asynchronous reply,
  and delivery; close unrestricted-origin and name-rebinding bypasses as well as
  restricted-caller paths. Release retained context on every terminal path.
- [ ] W3-13 — Integrate timer cancellation/drainage with Realm closure; test expiry
  versus close/cancel, creator death, target replacement, and repeated teardown.
- [ ] W3-14 — Implement the agreed Realm-local tracing/session model and protect
  tracer destinations, patterns, sequential traces, profiling, and delivery barriers.
  Explicitly deny unsupported modes and test that host tracing cannot leak to tenants.

### Single-node profile and integration

- [ ] W3-15 — Enforce the non-distributed deployment profile at startup and runtime:
  activation, node naming, controllers, remote spawn/send/monitor, and internal APIs.
- [ ] W3-16 — Reject incompatible pre-existing distribution state; close incoming
  traffic paths and race activation against Realm creation. Document supported host
  management transports that do not re-enable BEAM distribution.
- [ ] W3-17 — Run two restricted supervision trees with deliberately exchanged PIDs,
  aliases, names, and references; test sibling/ancestor/host control decisions.
- [ ] W3-18 — Stress concurrent spawn/exit/close/stop, timer expiry, monitor replies,
  interrupted/resumed stop, and deferred dirty cleanup; measure retained metadata
  and quota return, not just monitor notifications.

**Acceptance**

- [ ] W3-G — Default-restricted fixture Realms preserve local supervision and scoped
  services, reject foreign process interactions across the audited paths, retain
  required cleanup behavior, and cannot activate or receive distributed operations.

## W4 — Close shared-state and native-effect bypasses

### Shared state

- [ ] W4-01 — Implement ETS Realm ownership and scoped named tables; check every
  operation using table IDs, names, enumeration, and info, including internal paths.
- [ ] W4-02 — Mediate ETS transfer/give-away, heirs, fixation, match/select continuations,
  deletion races, and creator death; make public/protected visibility Realm-local.
- [ ] W4-03 — Integrate ETS reservations, growth, delayed deletion, and teardown with
  W5/W6; test foreign handles and memory retention after owner/Realm exit.
- [ ] W4-04 — Implement the agreed persistent-term namespace or enforce explicit
  profile denial. Cover get/list/update/erase, global scans, retained values,
  reclamation work, and cross-Realm resource-bearing terms.
- [ ] W4-05 — Audit other mutable VM resources, including atomics, counters, references
  backed by mutable resources, global caches, and shared service state; mediate
  possession/use/transfer rather than assuming immutable term copying is isolation.

### Native and host effects

- [ ] W4-06 — Define approved native exports and effect classes; enforce policy on
  already-loaded code and direct/internal calls as well as load/open operations.
- [ ] W4-07 — Bind native resources, scheduled jobs, threads, callbacks, and senders to
  retained authority. Reject absent/invalid context; never treat missing callers or
  a trusted module's name as host privilege.
- [ ] W4-08 — Mediate `enif_send`, driver sends, native monitors, process inspection,
  port messages, callbacks, and resource sharing; preserve legitimate VM-owned work.
- [ ] W4-09 — Mediate all port/driver lifecycle and command/control/call/info paths,
  including inherited or leaked handles and dynamically loaded drivers.
- [ ] W4-10 — Gate direct filesystem, sockets, DNS, subprocess execution, environment,
  working-directory, OS inspection, and other host effects; route approved effects
  through least-privilege brokers with validated arguments and results.
- [ ] W4-11 — Deny tenant halt/restart, debugger/internal-state mutation, global system
  settings, unsafe tracing, and other VM-wide controls; audit privileged services
  for confused-deputy requests and claimed-sender spoofing.
- [ ] W4-12 — Close direct global loader/purge/NIF-loading bypasses while private code
  environments are incomplete; replace temporary denials only with scoped paths.
- [ ] W4-13 — Review binary/term decoding, fun/resource reconstitution, atom creation,
  parsers, compression, crypto, and other ostensibly pure native functions for
  authority, allocation, scheduling, and cancellation effects.
- [ ] W4-14 — Define bounded-execution requirements for allowed native work. Move
  unbounded/uninterruptible or unsafe effects out of process, deny them, or record
  a release blocker; arbitrary native execution is not a safe tenant capability.
- [ ] W4-15 — Add adversarial tests for preloaded native exports, stale resources,
  callback-after-close, forged sender metadata, privileged-service requests, and
  alternate internal APIs. Pair each allowlist entry with ownership/budget tests.
- [ ] W4-16 — Reconcile the implemented inventory against actual exported operations
  and code paths; add a maintenance check so new upstream exports require review.

**Acceptance**

- [ ] W4-G — All inventoried tenant-visible operations have enforced dispositions.
  The supported profile has no known unmediated shared-state/native authority path;
  deliberately unsupported facilities are denied and tested. Independent release
  review remains required even after this gate.

## W5 — Complete endpoints and resource ownership

### Grants and broker protocol

- [ ] W5-01 — Define endpoint principals, operation rights, direction, peer binding,
  payload types, resource-transfer rules, and generation/stale-handle semantics.
- [ ] W5-02 — Implement reviewed grant delegation/attenuation for nested managers and
  supported peers; prevent widening rights or bypassing ancestor limits/lifecycle.
- [ ] W5-03 — Define and implement request IDs, response correlation, caller binding,
  duplicate/replay handling, error responses, and exactly which delivery guarantees
  are provided; do not promise exactly-once effects without a supporting protocol.
- [ ] W5-04 — Add bounded wakeups/notifications and fair servicing without lost wakeups,
  notification storms, polling dependence, or generic cross-Realm mailbox authority.
- [ ] W5-05 — Add count/byte/in-flight limits and backpressure for requests and replies;
  charge transient copies, queued work, and retained grant objects through W6.
- [ ] W5-06 — Implement deadlines and cancellation with defined races among admission,
  execution, completion, timeout, revoke, peer death, and Realm close.
- [ ] W5-07 — Separate transport revocation from already-consumed host work; define
  broker cancellation acknowledgement, late replies, abandoned results, and partial
  external effects. Validate all returned resources and prevent capability laundering.

### Lifecycle infrastructure and adapters

- [ ] W5-08 — Establish an ownership registry/protocol for every resource class with
  reserve/commit/rollback, close, cancel/drain, retained references, and final release.
  Specify lock ordering and lifetime rules for managed and unmanaged threads.
- [ ] W5-09 — Implement adapters for timers, ETS, persistent state, ports/drivers,
  native jobs/resources, broker requests, endpoint grants, and code environments
  alongside their feature packages; document all resources that can outlive a PID.
- [ ] W5-10 — Define deterministic subtree closure ordering and prevent new admissions
  or callbacks from resurrecting closed work; handle resource transfer during close.
- [ ] W5-11 — Extend resumable/idempotent Realm stop to all owned work with bounded
  batches and scheduler yielding, including repeated and concurrent stop callers.
- [ ] W5-12 — Expose logical completion, outstanding deferred cleanup, retained usage,
  and stop failure/timeout states without leaking foreign state to tenants.
- [ ] W5-13 — Define bounded native completion or out-of-process termination behavior;
  preserve ownership/charges for work that cannot yet be cancelled. Do not report
  successful full teardown while unaccounted native work remains.
- [ ] W5-14 — Test handle/identity retention, lost managers, retained closed descendants,
  interrupted stop, grant GC, peer death, and callbacks racing physical reclamation.
- [ ] W5-15 — Run long create/work/revoke/stop churn with delayed native/broker work;
  demonstrate usage returning to an explained steady state and quotas becoming
  reusable only at the documented release points.

**Acceptance**

- [ ] W5-G — Every supported resource has tested ownership and teardown. Queued,
  executing, cancelled, completed, and retained work is accounted for; stop and
  cancellation guarantees match observable behavior under races and failure.

## W6 — Implement aggregate budgets and fairness

### Charging and admission

- [ ] W6-01 — Define budget units, hierarchy, reservations, hard/soft limits, permitted
  overshoot, exhaustion results, and recovery policy for every resource class.
- [ ] W6-02 — Define node-wide admission/headroom and protected host capacity in
  addition to subtree limits; prevent many top-level Realms exhausting the host.
- [ ] W6-03 — Implement atomic ancestor charging and rollback for failed admission,
  partial construction, allocation failure, cancellation, and resource transfer.
- [ ] W6-04 — Charge process heaps/stacks, fragments, GC/transient peaks, mailboxes,
  signal queues, monitor/link state, and timer/registry/trace metadata.
- [ ] W6-05 — Specify and implement charging for shared/refcounted binaries, sub-binaries,
  literals, message copies, and shared code; avoid free retention and unjustified
  double charges when ownership/reference patterns change.
- [ ] W6-06 — Charge ETS, persistent values, code/JIT metadata, endpoint buffers,
  in-flight broker work, ports, native allocations, and external resources that the
  host must bound; deny unaccountable resource classes in the supported profile.
- [ ] W6-07 — Resolve global atom exhaustion: loading and dynamic atom creation,
  non-reclamation, per-Realm and node limits, repeated deployment churn, and any
  required rejection/restart strategy. Document the residual node-lifetime limit.
- [ ] W6-08 — Implement exhaustion handling without corrupting GC, signal cleanup,
  accounting, or host control; reserve enough capacity to reject and tear down.

### Scheduling and validation

- [ ] W6-09 — Define CPU budgets and replenishment, ancestor sharing, priorities,
  throttling, and fairness targets; prevent tenant priority/scheduler settings from
  bypassing the policy or starving the management plane.
- [ ] W6-10 — Implement scheduling/reduction accounting that preserves cheap local
  processes and messaging; charge VM/BIF work performed on a tenant's behalf.
- [ ] W6-11 — Bound and account dirty CPU/I/O execution, native continuations, queued
  dirty work, and asynchronous callbacks, including work remaining after closure.
- [ ] W6-12 — Add allocation/admission fault injection and test each rollback/release
  point, retained-object charges, concurrent ancestor exhaustion, and recovery.
- [ ] W6-13 — Stress spawn floods, mailbox growth, large binaries, ETS/persistent data,
  atoms, timers, code churn, endpoints, and native work independently and together.
- [ ] W6-14 — Measure p50/p99 latency and throughput for a victim tenant and host under
  noisy neighbors; verify policy limits, bounded overshoot, and post-throttle recovery.
- [ ] W6-15 — Reconcile logical accounting with allocator/physical memory measurements
  and fragmentation over long churn; document any non-reclaimable/shared overhead.

**Acceptance**

- [ ] W6-G — Resource limits and ancestor/node budgets hold within documented bounds;
  exhaustion is recoverable as specified, cleanup remains possible, and measured
  fairness/overhead meet W1 thresholds. Unaccounted native/atom risks block release.

## W7 — Complete deployment environments and OTP compatibility

### Code implementation

- [ ] W7-01 — Turn the W2 design into production-quality environment metadata,
  ownership, admission, immutable sharing, and Realm attachment APIs.
- [ ] W7-02 — Implement environment-aware module/export lookup, imports, JIT caches
  and direct calls, interpreter dispatch, dynamic calls, BIF resolution, and funs.
- [ ] W7-03 — Implement scoped load/prepare/finish/delete/purge and code queries,
  including direct internal APIs; remove all tenant global-code mutation paths.
- [ ] W7-04 — Implement transactional multi-module loading and initialization with
  correct callback authority, failed-load rollback, concurrent activation behavior,
  and limits on code/atom/native allocation.
- [ ] W7-05 — Implement current/old code handling, hot upgrades, live-stack/fun checks,
  soft/hard purge, safe literal reclamation, and complete environment teardown.
- [ ] W7-06 — Test captured/exported funs, dynamic callbacks, shared OTP libraries,
  serialized terms, old versions, and malformed code against cross-environment
  execution or replacement; preserve Realm authority through every call path.
- [ ] W7-07 — Audit tracing, stack traces, coverage/debug info, code inspection, and
  tooling for environment identity and foreign-code exposure.

### Realm-local OTP and languages

- [ ] W7-08 — Provide environment-local loading/autoload and code-server behavior;
  restricted startup must not depend on sending to the global host code server.
- [ ] W7-09 — Provide Realm-local application controller/master, dependency startup,
  configuration/environment, shutdown ordering, and supervision behavior.
- [ ] W7-10 — Provide compatible local registration/discovery and supported OTP
  service adapters; explicitly reject unsupported global/distributed services.
- [ ] W7-11 — Provide a real group-leader I/O service and scoped logging/handlers,
  with broker rights, output limits, metadata separation, and failure behavior.
- [ ] W7-12 — Define immutable platform libraries, deployment dependency packaging,
  version selection, trusted native adapters, and code/configuration boundaries.
- [ ] W7-13 — Finalize the public Realm/code-environment/endpoint API and migration
  guidance from experimental `erts_internal` calls; add examples and API tests.
- [ ] W7-14 — Add representative Erlang, Elixir, and Gleam services using normal
  supervision, configuration, messaging, timers, and the supported state facilities.
- [ ] W7-15 — Run conflicting module/application names and dependency versions
  concurrently; independently upgrade, fail initialization, restart, and stop them.
- [ ] W7-16 — Measure activation, code sharing, call overhead, repeated upgrades,
  retained old funs/literals, and environment reclamation against W1/W6 criteria.

**Acceptance**

- [ ] W7-G — Independently packaged supported services coexist and upgrade without
  unintended code/configuration replacement; startup, callbacks, OTP services,
  budgets, and teardown work without trusted-fixture or global-service bypasses.

## W8 — Integrate the edge host and make the release decision

### Platform integration

- [ ] W8-01 — Specify the deployment/controller protocol and trust boundary: artifact
  authorization, validation, policy assignment, code loading, endpoint grants, and
  activation only after preparation succeeds. Reject malformed/oversized artifacts.
- [ ] W8-02 — Implement the loader and router lifecycle with deployment generations,
  readiness/failure states, concurrent activation deduplication, routing changes,
  draining, rollback, and eviction without stale-generation delivery.
- [ ] W8-03 — Implement bounded request/response transport with payload validation,
  streaming/backpressure as supported, request identity, deadlines, disconnect
  cancellation, and overload behavior through reviewed endpoints/brokers.
- [ ] W8-04 — Implement resource-aware activation/eviction and host-wide admission;
  test failed initialization, capacity exhaustion, cleanup stalls, and routing races.
- [ ] W8-05 — Add authorized lifecycle/resource/latency observability and diagnostics
  with tenant separation, bounded cardinality/output, and no capability leakage.
- [ ] W8-06 — Implement a storage-backed actor demonstration with explicit identity,
  storage rights, consistency, retry/idempotency, and partial-effect semantics;
  data must survive Realm replacement and VM restart via external durable storage.
- [ ] W8-07 — Test controller/broker/storage failures, late and duplicate replies,
  request cancellation during writes, host restart, and deployment rollback;
  distinguish durable state from ephemeral Realm identity and live migration.

### Hardening and release

- [ ] W8-08 — Run end-to-end mixed-language/multi-deployment compatibility and
  adversarial tests on all supported architectures and runtime variants.
- [ ] W8-09 — Add fuzzing/property tests for policies, handles, terms, loaders,
  endpoint protocols, resource ownership, and concurrent lifecycle state machines;
  retain minimized reproductions and fixed regression cases.
- [ ] W8-10 — Run applicable sanitizers, lock/order diagnostics, allocator-failure
  injection, process-table exhaustion, and extended native/callback race stress.
- [ ] W8-11 — Benchmark cold/warm activation, density, memory, steady-state throughput,
  p50/p99 latency, noisy-neighbor fairness, cancellation, and teardown under churn;
  compare against unmodified OTP and separate-VM deployment baselines.
- [ ] W8-12 — Complete broad OTP regression runs and supported-platform CI; resolve
  every release-blocking failure and review all skips/exceptions explicitly.
- [ ] W8-13 — Obtain independent review of authority, native effects, code loading,
  lifetime/accounting, and broker boundaries; track findings to fixes and retests.
- [ ] W8-14 — Publish the supported profile, operational limits, threat model,
  residual risks, benchmark methodology, reproducible build/tests, and deployment
  guidance. Include crash containment and OS-isolated fallback guidance.
- [ ] W8-15 — Conduct an explicit release go/no-go review against W1–W7 and M0–M5.
  A completed review can conclude no-go; functional completeness is not security
  approval. Any rollout must be separately authorized and have rollback criteria.

**Acceptance**

- [ ] W8-G — Edge deployment, routing, durable actor replacement, and measured
  lifecycle/fairness behavior are demonstrated. Independent review and an explicit
  release decision are recorded, with no unsupported security or density claims.

## Gate tracker

This table mirrors the gates above; do not maintain a separate percentage-based
completion claim. A package remains open until its required tasks and gate pass.

| Gate | Status | Evidence / blocker |
| --- | --- | --- |
| W1-G | Open | Broader regressions discovered/classified, public contracts/API, measured comparisons, 570 BIF/180 NIF-API declarations plus 482 native records and 32 initial dispositions; full audit/profile pins, threshold agreement, regression clearance and executed CI remain |
| W2-G | Open | Semantics specified; shared-code witness confirms missing private resolution; full map, environment-aware prototype and feasibility review remain |
| W3-G | Open | Opt-in checks; legacy fixtures, scoped services, and alternate paths remain |
| W4-G | Open | Shared-state/native/debug/host-effect bypasses remain |
| W5-G | Open | Process and endpoint transport lifecycle only; no complete resource teardown |
| W6-G | Open | Count limits and endpoint bounds only; aggregate memory/CPU absent |
| W7-G | Open | No independent code environments or Realm-local OTP stack |
| W8-G | Open | No edge integration, independent review, or security approval |

## Execution log and immediate queue

### Planning baseline

- Starting implementation commit: `18708c2d4d`.
- No additional runtime implementation is claimed by this planning document.
- Existing evidence: BASE-01–03 and the linked implementation record.
- W1-01–04 are now verified; see the execution entry below.
- Next work batch: W1-05 (broader regression classification) and W1-09–15
  (inventory and boundary/profile/ownership decisions).
- Early parallel investigation: W2-01–11; keep experimental loader changes separate
  from boundary patches and record the feasibility gate before committing to W7.
- Next process batch: W3-01–03 (fixture migration, defaults, admission audit), then
  scoped names/enumeration/timers. Start W5 ownership contracts before introducing
  retained timer/native work; update the queue as dependencies become concrete.

### W1-01–04 — Reproducible validation and C-node fixture repair

- **Status at this historical checkpoint:** Four tasks verified; 119 actionable tasks and all eight package
  acceptance gates remain open. This batch does not add runtime isolation.
- **Implementation:** `scripts/realm-validation.py`, its ten Python unit tests,
  `scripts/realm_ei_probe.erl`, and the two-file `process_SUITE`/`fwd_node.c` repair.
  Recorded in the follow-up validation/tooling commit after the runtime snapshot.
- **Durable evidence and commands:** [validation guide](0001-validation.md).
  55 Realm cases pass on all three current runtime variants; 84 focused debug
  regressions pass. Independent fresh upstream/snapshot optimized/debug JIT builds
  pass after bootstrap and preloaded regeneration; fresh snapshot Realm tests pass
  on optimized and debug JIT.
- **C-node comparison:** Original upstream and snapshot fixtures both fail with
  `ei_accept; 5`. EI canonicalizes `MacBookPro` to `macbookpro`; the fixture guessed
  the wrong exact node name. Reporting EI's actual name repairs the case on all
  three current variants and on the unchanged upstream runtime. The standalone
  echo probe reproduces guessed-name failure versus actual-name success.
- **Harness finding:** Concurrent worktrees collide on OTP's fixed `test` node
  during native test-data compilation. The first fresh debug attempt failed before
  tests ran. Added user-wide runner serialization and a regression unit test;
  serialized retry passes. Arbitrary external test jobs still need coordination.
- **Residual gaps:** No fresh interpreter build, x86-64 CI, full regression clearance,
  performance baseline, or security certification is claimed. Keep the clean
  worktrees for upcoming baseline comparisons; logs are supplementary to this record.
- **Next IDs at this checkpoint:** W1-05, W1-09–15; early W2-01–11 investigation.

### W1-05 / W1-09 — Broader discovery and declared-BIF inventory (partial)

- **Status at this historical checkpoint:** Both tasks open; four verified tasks, 119 open tasks,
  and eight open gates. No runtime isolation or security approval added.
- **Implementation:** Broad discovery and single-case diagnosis in
  `scripts/realm-validation.py`, explicit NIF cross-suite helper compilation,
  released-runtime flavor selection in `make/test_target_script.sh`,
  `scripts/realm-inventory.py`, generated `0001-bif-inventory.tsv`,
  [native discovery ledger](0001-native-inventory.md), and a lightweight inventory
  workflow (not run on hosted CI), recorded in the follow-up validation/tooling commit.
- **Evidence:** 20 tooling unit tests pass; inventory drift check passes and stale
  input is rejected. Full debug process: 101 passed, zero failed, three classified
  skips; code/multi-load/parallel-load: 29/3/2 passed; trace: 76 passed, two explicit
  stress skips; dirty NIF/port BIF: 35/9 passed; repaired NIF run: 85 passed, two
  platform/build skips. Full counts and commands are in the
  [validation record](0001-validation.md).
- **Failures are retained:** Signal: 38 passed, one 120-second timetrap, reproduced
  as an isolated case on clean upstream. Distribution: 51 passed, one 240-second
  timetrap (`async_dist_proc_dctrlr`), four memory-guard skips; baseline comparison
  pending. Initial ETS did not start CT because a released JIT binary is named
  `smp`. Initial NIF reported 87 cleanup failures because `driver_SUITE` was not
  compiled; its repaired retry now passes all non-skipped cases. ETS's first retry
  was invalidated by editing the test script during release preparation; the
  stable-source retry remains active. No failures/skips are silently converted
  into passes.
- **Inventory scope:** All 570 declared BIFs have candidate source anchors and
  dispatch facts; all remain unreviewed. Five unmatched dirty-test annotations
  are visible. Body/helper changes, NIF/driver callbacks, deferred and optimized
  paths still require full enumeration and semantic review. W4-16 remains open.
- **Next:** Finish W1-05 comparisons/retries and classify every remaining result;
  W1-09–15 substantive per-operation and profile/ownership decisions remain next.

### Follow-up checkpoint — commits and early W2 investigation

- W1 validation, fixtures, inventory, and checklist committed as `787f012906`.
  Generated `erts_internal.beam` is deliberately excluded; nothing pushed.
- The stable-source ETS run remains active. A serialized upstream comparison of
  `distribution_SUITE:async_dist_proc_dctrlr` is queued after it, retaining the
  original testcase timetrap. Results belong in the W1 validation record once run.
- Began W2-02 source mapping of global module/export keys, code indices, both JIT
  external-call paths and fun dispatch. Added the
  [code-environment investigation](0001-code-environment-spike.md) and
  `scripts/realm_code_probe.erl`. The probe compiles; execution across all three
  runtime variants is queued after the regression comparison.
- The probe is deliberately a **shared-code witness**, not a private loader: two
  restricted Realms observe the same global replacement, while local funs retain
  their old version. Its expected success explicitly reports private-environment
  acceptance as `not_met`. No W2 task or gate is marked complete.

### Twenty-task batch — W1-05–17 / W2-01–07

**13 of these 20 tasks now have their stated evidence; seven remain open.**
Overall: 17 verified tasks, 106 open tasks, eight open package gates. Contract-only
completions below are not claimed as enforcement or a successful loader spike.

| IDs | Status / evidence |
| --- | --- |
| W1-05 | Partial: public API adds five passes on each of three variants; optimized process-table exclusions now pass. Distribution and optimized NIF timeslice failures reproduce upstream. Full debug ETS timed out; isolated optimized `update_counter` passes both sources. All known failures/skips are classified, but full regression clearance is absent |
| W1-06 | Prepared, not accepted: manual eight-cell Linux ARM64/x86-64 optimized/debug JIT/interpreter workflow, strict result checks and summary artifacts; hosted jobs unexecuted |
| W1-07 | Verified: repeatable fresh-VM harness covers required categories, metadata, warmup, per-operation quantiles and repetition dispersion; three-repeat upstream host/current host/restricted runs completed; [benchmark evidence](0001-benchmarks.md) |
| W1-08 | Partial: matched initial measurements recorded; noisy tails, broader platforms/workloads and explicit threshold agreement remain |
| W1-09–10 | Partial: 570 BIF and 180 NIF C API declarations now drift-checked. No per-entry policy approval inferred; native exports/drivers/services/deferred paths, effects/cleanup and dispositions remain |
| W1-11–12 | Contract decisions complete: [identity/lifetime and retained manager exceptions](0001-contract-decisions.md), existing native coverage plus public-wrapper non-escalation/stale-handle tests |
| W1-13 | Open: candidate invariants specified, but exact supported Elixir/Gleam pins and scoped OTP compatibility/service matrix unvalidated; neither prototype profile is safe |
| W1-14–15 | Contract decisions complete: [visibility/threat exclusions and ownership/charging vocabulary](0001-contract-decisions.md); runtime redaction, accounting and resource adapters remain W3–W6 work |
| W1-16 | Verified initial public contract: Kernel `realm` module with separately opaque types and experimental version 0; native authority unchanged; all 13 exports exercised by five cases on each of optimized JIT/debug JIT/debug interpreter |
| W1-17 | Review framework established: [authority/lifetime checklist and seven open findings with accountable workstreams](0001-contract-decisions.md); independent reviewer remains unassigned and release-blocking |
| W2-01 / W2-03–07 | Semantic specifications complete: [environment ownership, resolution, platform code, fun binding, transactions/on_load and version/purge lifetime](0001-code-environment-contract.md). No environment-aware implementation is claimed |
| W2-02 | Partial source map and compiled-call disassembly; globals/imports/funs/indices identified, but complete loader/callback/purge/cache mapping remains |

The shared-code witness runs on three variants and reports `shared_code_only` /
`private_environment_acceptance => not_met`. It is intentionally negative evidence
for private namespace availability. Source/kernel build, 28 tooling tests, inventory checks, contract links/licenses
and diff checks pass. Final reruns pass 55 Realm cases on three variants plus 84
focused debug regressions; the five public API cases also pass on three variants.
Source/API contracts are committed as `d18ad88df9`; benchmarks, NIF inventory and
runtime CI as `6c4c4de526`. Generated preloaded BEAMs remain excluded from commits.

Next continue the seven unfinished batch tasks, with W2-08–11/W3 implementation
following the adopted semantics. External CI execution, compatibility pins,
threshold agreement and independent review must remain explicit blockers; the
remaining source audit/mapping is unfinished engineering work, not an invented
external blocker. Nothing in this batch changes the untrusted-workload no-go.

### Next-five batch — W1-05, W1-06, W1-08, W1-09, W1-10

**One completed; four remain open.** Overall: 18 verified tasks, 105 open tasks,
eight open package gates. [Evidence and limitations](0001-next-five-evidence.md).

- **W1-05 verified as discovery/classification, not regression clearance:** complete
  optimized ETS 162/0/0, monitor 25/0/0, timers 23/0/0 and registration 1/0/0;
  installed OTP 28.4.1 interop passes on optimized/debug JIT through a reproducible
  adapter for CT's inherited emulator flags. Prior broad failures/skips are all
  classified; signal/distribution/NIF timing failures, full debug ETS timeout,
  disabled stress and Linux-only coverage remain release blockers.
- **W1-06 partial:** strict eight-cell summary aggregation checks revision, runtime,
  architecture, source dirtiness, runner digest and exact counts; missing or failed
  artifacts fail. Hosted cells remain unexecuted; no push or workflow dispatch.
- **W1-08 partial:** 5000 samples × ten repetitions per mode in both source orders;
  strict comparison and durable dispersion. Restricted spawn p99 is about 2×
  upstream in both orders; no passing budget or threshold agreement is invented.
- **W1-09 partial:** 482 native source records add NIF exports/lifecycle slots,
  driver APIs/globals/callbacks and an explicit unresolved dummy table. Source-file
  hashes catch body changes; unenumerated wrappers/services/optimized/deferred
  paths and transitive helper coverage remain unfinished engineering work.
- **W1-10 partial:** ten design records cover 32 declared entries with desired
  dispositions, source dependencies, cleanup/accounting, gaps and workstream
  owners. Three-variant negative witnesses prove foreign atomics/global persistent
  state/host timer bypasses. None is marked enforced or independently approved.
- **Validation:** 51 Python tests; all inventory/review checks; 213 passing CT case
  executions in the new broad/interop runs; effects witness on three variants.
  The negative witness is not counted as successful security validation.

Native discovery/reviews/witnesses are committed as `4a27fa0787`; CI aggregation,
measurement comparison and the old-release adapter as `ba2b54c6c4`.
CI execution and budget agreement require external evidence/decisions; the full
native inventory/classification still needs implementation-team work. This batch
does not excuse those engineering tasks as externally blocked. No untrusted
profile is safe, and no M0–M5 package gate is closed.

### Runtime enforcement follow-up — W4-04/05, W5/W6 lifetime evidence

[Implementation/evidence](0001-shared-resource-enforcement.md) closes two specific
previously witnessed entry-path bypasses without marking the broader tasks done:

- Atomics and both counter backends bind immutable retained Realm ownership.
  All native use/info paths reject foreign access, including host/ancestor and
  legacy-profile callers. Direct internal counter BIFs are checked too.
- Restricted persistent-term public access is denied before effects; internal
  global erase is host-only. This is not a private persistent-term namespace.
- Nine new cases cover local sharing, creator exit, foreign-handle use, nested
  management, stopped owners, retained child-quota return and persistent denial.
- All four local optimized/debug JIT/interpreter variants pass the 44-case resource,
  148-case default regression and five-case public API profiles, no skips/failures.
  The formerly missing local optimized interpreter is now exercised. Hosted CI
  remains unexecuted; these are incremental same-checkout builds.
- Updated witness confirms both denials and still demonstrates host-timer ingress
  on all four variants. That remaining bypass is not called a successful boundary.
- 52 Python tests pass. Re-reviewed source records now cover 37 declared entries;
  full source/native classification and independent review remain open.
- Matched upstream/current resource-use measurements are preserved, but 42-ns
  p50 quantization precludes claiming zero overhead. Admission/allocation, contention,
  final reclamation costs and threshold agreement remain performance work.

No additional whole task or package gate is checked off: W4-04/05 still require
alternate/native/scoped-service review and other mutable-resource coverage. New
resource admission/revocation on close and byte/CPU charging remain W5/W6 work.
Overall remains 18 verified tasks, 105 open tasks and eight open gates. No hostile
workload or full OTP/language profile is approved. Generated BEAMs stay uncommitted.

### Entry template for subsequent implementation batches

- **Task IDs / status:**
- **Contract or design decisions:**
- **Implementation commit(s):**
- **Tests and exact commands / platform / results:**
- **Performance or reclamation evidence:**
- **Review findings / residual gaps / blockers:**
- **Next task IDs:**
