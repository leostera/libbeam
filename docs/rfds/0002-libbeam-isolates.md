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

# RFD 0002: libbeam — an embeddable BEAM engine with fresh isolates

- **Status:** Draft architecture and executable-proof specification. Not implemented.
- **Architectural direction:** Separate process-wide engine initialization from
  creation of independently owned, reduced-profile Erlang execution worlds. Expose
  lifecycle to a C++ embedder, not to tenant Erlang programs. The direction is adopted
  for the proof; API details and implementation remain draft.
- **Runtime scope:** Strip the emulator to the minimum needed for host-managed
  engines and isolates. Preserving `erl`, full OTP-node behavior, legacy startup,
  native drivers or process-global administration is not a requirement. Keep legacy
  machinery only where needed for development/bootstrap, outside the guest contract.
  Existing OTP regression tests are diagnostic aids, not a mandate for compatibility.
- **Tenant contract:** BEAM bytecode only, a closed approved native runtime surface,
  and no ambient OS authority. This is a deliberately supported subset, not an
  attempt to instantiate a complete conventional Erlang/OTP node per isolate.
- **First deliverable:** A native executable linked against libbeam that creates two
  fresh isolates, runs conflicting same-name modules concurrently in lifetime,
  destroys one without disturbing the other, then creates a fresh replacement.
- **Repository:** `beam/` is a tracked upstream OTP snapshot with direct emulator
  changes. It is not a submodule and requires no separately applied patch series.
  `libbeam/` contains embedding tools and will contain the C++ API and isolate tests;
  `docs/` contains project documentation.
- **Provenance:** clean upstream OTP `cca4e72510a97cfca6427602d3da8a22d5ff7a33`
  (`30.0-rc0`). Realm changes are excluded. The previous implementation remains at
  `archive/realm-snapshot` (`eb019d92`); see the [transition record](0002-clean-upstream.md).
- **Security:** The first proof is for trusted fixtures only. Passing it does not
  authorize hostile tenants, arbitrary native extensions or production deployment.

## 1. Motivation and relation to Realms

Today, initializing BEAM largely initializes one global Erlang world. Its module
resolution, atom table, process services and many native subsystems assume that
world is the VM. Simply exporting the existing entry point from a library would
not make multiple independent worlds possible.

RFD 0001 approached coexistence by giving processes immutable Realm membership
and mediating access inside that world. It produced useful ownership, lifetime,
policy and negative-test evidence, but code and several services remain global.

This RFD changes the center of the design:

> Initialize one engine. Instantiate a fresh, reduced-profile Erlang world on
> demand. Preserve ordinary semantics for supported operations, without exposing
> a filesystem, native extension loader or the full conventional node environment.

We call that world an **isolate**. The embedder, not a tenant program, creates,
configures, loads and destroys it. There is no mandatory tenant `isolate:create`
API, nor an isolate argument on ordinary Erlang operations.

The active implementation now starts from clean upstream OTP, not the Realm fork.
The public Realm API and all historical emulator enforcement have been removed
from the active source. This is a deliberate architectural reset, not a claim that
upstream provides equivalent isolation: it does not. Historical membership, retained-
owner, teardown and private-code work remains reference material only. Reintroduce
mechanisms only when justified by the host-managed design, with fresh ownership
contracts and tests. There is no existing `ErtsRealm` owner to inherit or migrate.
Nested Realms are not required by this proof.

## 2. Goals and exclusions

For every failure, ask whether the behavior is needed for libbeam. Fix failures in
required engine/isolate lifecycle and supported BEAM semantics. If the behavior is
not needed, remove it or classify it unsupported rather than restoring legacy OTP
compatibility. A rejected unsupported operation must still fail safely before its
excluded effects; memory corruption, host termination or leaks in supported paths
are not ignorable compatibility failures.

### Goals

1. A C++ host links the engine as a library and retains control of its process.
2. Engine initialization returns; creating an isolate does not restart the engine.
3. Isolates are created fresh on demand, without prebooted tenant-instance pools.
4. Simultaneously live isolates can load different implementations of the exact
   same module/function/arity without module renaming or global code swapping.
5. Ordinary spawn, messaging, monitoring and local resource operations execute in
   an implicit, immutable isolate context.
6. Stopping and reclaiming an isolate does not halt the host or another isolate.
7. An orchestrator can deliver bounded events and observe results without owning
   or retaining raw BEAM heap pointers.
8. Target single-digit-millisecond time to **first application execution**, not
   completion, with local precompiled artifacts and an initialized engine. Measure
   creation, load/bootstrap, scheduling delay, execution and destruction separately.
9. Support thousands of simultaneously resident small isolates as a scale objective,
   not an inference from a sequential creation loop. No V8-equivalent density is
   assumed; memory and CPU contention must be measured.

### Not part of the first proof

- A complete workerd integration, HTTP server, deployment system or cloud service.
- A public, stable ABI, several independent engines in one process, or engine
  reinitialization after its final shutdown.
- Distributed Erlang, live migration, cross-isolate PIDs or transparent links.
- Full OTP application compatibility or Elixir/Gleam ecosystem acceptance.
- A shell, debugger, ambient filesystem, raw networking, subprocesses, arbitrary
  ports, native extension loading or VM-wide administration. These are excluded
  from the tenant model, not merely postponed until after P0.
- Production CPU/memory fairness, security certification or recovery from every
  engine bug, fatal allocator failure and unsafe native operation.
- Snapshots of running applications, fork-based cloning, a separate OS process per
  isolate, or `dlmopen`/multiple copies of the entire emulator as a substitute.

An immutable bootstrap image or reusable compilation artifact may be shared.
That is different from assigning an already-running instance to a tenant. A first
proof must not rely on a pool to conceal instance creation cost.

### Supported execution profile: positive list, not an OTP node

The host supplies validated BEAM module bytes and an explicit bootstrap. It does
not supply an implicit OS user, working directory, filesystem, environment-variable
namespace, shell, node cookie or unrestricted network. The host itself may use
these facilities; that does not grant them to tenant code.

| Area | P0 support contract |
| --- | --- |
| Language execution | Selected supported BEAM instructions and pure built-ins needed by the proof: ordinary terms, arithmetic, pattern matching, binaries, exceptions and GC; unsupported artifact/instruction versions fail validation |
| Processes | Local spawn, messaging, links, monitors, exits, receive and reduction-based scheduling; immutable isolate membership |
| Code | Host-provisioned modules, isolate-local resolution, direct/dynamic calls and local/external funs; no tenant code-loading authority |
| Local state | Isolate-local registration, named/private ETS, persistent terms, atomics/counters and the bounded local timer behavior exercised by P0 |
| Libraries/services | Only an enumerated bootstrap and selected bytecode dependencies; no automatic full Kernel/stdlib/application-controller boot |
| Host integration | Bounded binary invocation/results initially; later effects require explicit capabilities, separate ownership and cancellation contracts |
| Native implementation | Only built-ins and statically provisioned runtime facilities explicitly approved for this profile; these remain trusted engine code |

A versioned profile manifest must enumerate actual modules/MFAs, instructions,
internal helpers, native registrations, bootstrap processes, limits and denial
behavior before implementation claims profile acceptance. This table defines
requirements, not a completed allowlist. New native facilities are opt-in changes
to the engine's trusted surface, never tenant-supplied extensions. Pure BEAM code
is not inherently harmless: it can call effectful built-ins unless the runtime
mediates them.

**Excluded capabilities:** tenant NIF/shared-library loading (including loading from
an in-memory blob), native drivers, executable ports and subprocesses, direct file
access, raw sockets/DNS, distribution activation, OS environment/process control,
debug/tracing escapes and tenant access to engine/isolate management. A tenant may
ship its own bytecode module named `file`; the name does not grant native authority.
Likewise, platform-module names and internal BIF exports are not authority.

Enforcement belongs at effectful native/dispatch entry points, including special
instructions and already-linked helpers. Hiding an Erlang wrapper is insufficient.
Trusted bootstrap exceptions require explicit execution provenance, not permission
based on a module name or a temporary process-global bypass flag.

**Failure contract:** malformed or unsupported bytecode is rejected by the host load
operation with a structured error before publication. Calls to absent library
modules have ordinary `undef` behavior; attempts to use excluded engine-native
operations raise `notsup` before side effects, translated to an exception completion
at the host boundary. Existing ordinary argument errors still need per-entry
specification. No accidental host file access, process exit, indefinite wait or
silent success is an acceptable unsupported-operation result. If a compatibility
facade is later shipped (for example returning `{error, enotsup}` for a file API),
its exact MFA/error contract must be listed and tested in the profile manifest.

BEAM bytecode compatibility is not application compatibility. Erlang/Elixir/Gleam
applications that depend on excluded services may fail, intentionally. Supporting
more libraries later means adding audited bytecode dependencies or explicit host
capabilities, not promising the entire conventional BEAM environment. Preserve
normal semantics for the supported subset rather than silently redefining them.

## 3. Architecture and ownership

```text
C++ host / future workerd-style orchestration
    | create, load, invoke, receive completion, stop, release
    v
libbeam public embedding interface
    |
BEAM engine — process-wide infrastructure, initialized once
    +-- shared scheduler infrastructure / host integration
    +-- isolate A: independently owned Erlang world
    +-- isolate B: independently owned Erlang world
    +-- isolate C: created later, without restarting the engine
```

The wrapper is not the isolation implementation. Core changes belong in `beam/`;
`libbeam/` exposes an intentionally small ownership-safe interface above them.

| State or mechanism | Proposed owner / requirement |
| --- | --- |
| Runtime executable code, immutable built-in metadata | Engine, where actually immutable and context-independent |
| Scheduler/async worker threads and physical clock source | Engine; no new thread fleet per isolate |
| Erlang processes, mailboxes, monitors and links | Isolate; ordinary spawn inherits context before publication |
| Process lookup/storage infrastructure | May use an engine-wide physical table, but lookup, enumeration, identity lifetime and accounting must preserve isolate boundaries |
| Module/export/import/fun resolution and code publication state | Isolate; global publication indices are not code namespaces |
| Runtime-created atoms | Isolate-local namespace/lifetime; immutable predefined atoms may use a common representation if proven safe |
| Registration, ETS, persistent terms, atomics/counters | Isolate-owned state, not global state hidden by result filtering |
| Logical timers and deferred callbacks | Retain originating isolate, validate destination, cancel/drain during teardown; physical timer infrastructure may be shared |
| Minimal bootstrap and selected service processes | Per-isolate where state belongs to that world; no obligation to instantiate the full OTP boot tree |
| Host I/O and service access | Explicit host-granted capability; no ambient authority merely because native code can reach the OS |
| Allocator arenas, caches, JIT artifacts and thread progress | Ownership/lifetime audit required; physical sharing must not imply shared mutable application state |

These are contracts to implement, not statements that the current source already
has these boundaries. A source ownership inventory must identify initialization,
all use sites, asynchronous producers, charges and destruction for each subsystem.
Moving declarations into an `Isolate` struct without changing their users is not
sufficient. Avoid a mutable process-global "current isolate" selector. Execution
context must remain correct across scheduler migration, dirty execution, yields,
GC, callbacks and teardown. Thread-local access is only an implementation aid,
never the sole durable provenance of deferred work.

The first proof may use one shared scheduler thread, provided A and B remain live
and scheduled together. Simultaneous execution on several scheduler threads is a
later explicit gate, not inferred from interleaving on one thread.

## 4. Minimal host-facing contract

The concrete desired API is now written in
[`two_isolates.cpp`](../../libbeam/examples/two_isolates.cpp), with
[experimental API types](../../libbeam/include/libbeam/engine.hpp) and
[same-module fixtures and implementation priorities](../../libbeam/examples/two_isolates.md).
It now builds and runs against an explicit-error scaffold, stopping at
`not implemented: Engine::create`. No actual engine or isolate is created by that
scaffold. The [implementation plan](0002-example-implementation-plan.md) defines
incremental runtime gates. Names may evolve before this becomes a stable SDK.

The example submits both starts before waiting, exercises private code and named
process state, reclaims A while B remains live, creates a fresh replacement, and
requires explicit engine shutdown. It proposes correlated wait handles instead
of exposing the completion-poll loop directly. Physical-release instrumentation
and the rest of the acceptance assertions below remain separate requirements.

**Implementation priority:** make this example real. Remove or reshape legacy
machinery when it blocks that path, rather than completing a broad cleanup first.

Required semantics:

- **Ownership:** One engine per host process for this proof. The host owns engine
  and isolate handles; no C++ exception crosses a future C ABI. Public errors are
  structured statuses, not host termination or log-only failures.
- **Threading:** Public management/poll operations are initially restricted to one
  host control thread, documented and checked. Scheduler threads run independently.
  No arbitrary host callback is invoked while VM locks are held. Completions are
  queued for the host to poll; reentrant invocation is not silently supported.
- **Load:** Host-supplied module bytes are consumed/copied with documented lifetime.
  The host, not the tenant, supplies the bootstrap bundle. Admission/load failures
  roll back unpublished state. Unsupported post-start replacement returns an error;
  hot upgrades are a separate milestone.
- **Call:** The proof uses exported arity-one functions accepting a binary and
  returning a binary. Invocation executes in a normal isolate-owned Erlang process,
  not on the host thread. Erlang exceptions become correlated failure completions.
  A returned invocation does not implicitly destroy unrelated isolate processes.
- **Transport:** Input bytes are copied on admission; output buffers have explicit
  host ownership/release. No `Eterm`, PID, fun, magic reference or native pointer
  crosses as authority. No generic unsafe ETF decoder is required by the proof.
- **Bounds:** Initial proof limits: 64 KiB per payload, 64 outstanding calls per
  isolate, and 1 MiB of queued payload bytes per isolate, including queued output.
  Reserve capacity for terminal statuses so saturation cannot silently lose accepted
  requests. Over-limit admission returns `full`/`limit` before side effects.
  These transport bounds are not aggregate heap or CPU budgets.
- **Completion:** Accepted requests receive exactly one terminal result, exception,
  cancellation or stopped status. No callbacks target a released host handle.
  A host-side deadline does not by itself prove VM cancellation/reclamation.
- **Identity:** Handles are scoped to their engine and generation. Operations on a
  live handle whose isolate is stopped return `closed`; reusing freed C++ storage is
  not supported. A surviving handle may retain a small tombstone/control block, not
  the reclaimed application world. Account for this separately from isolate-owned
  resources. Internal identifier reuse must not revive stale queued work.
- **Host process:** Isolate failure/normal shutdown must not call `exit()` on the
  host. In the initial profile, tenant `halt` and VM-wide controls are denied before
  effects. Engine invariant failure or fatal OOM may still be process-fatal; the
  trusted proof is not crash containment for arbitrary native code.

### Execution control: stop/evict first, suspend/resume separately

The host controls lifetime and eventual budgets; tenant bytecode does not configure
or widen its own allowances. Runtime controls and orchestration policy are separate.

| Control | Contract and milestone |
| --- | --- |
| Stop / evict | Mandatory P0 operation: close admission, terminate the world and reclaim it through safe cleanup. Eviction is a host policy reason for stop, not a resumable state |
| CPU allowance | Subsequent gate: aggregate actual execution across all isolate processes/schedulers and approved native work; reductions are scheduling checkpoints, not exact elapsed CPU time |
| Wall-clock deadline | Subsequent integrated policy gate: count elapsed time for a named invocation or the isolate lifetime, with the scope explicit; waiting for I/O consumes elapsed time but not equivalent CPU |
| Memory allowance | Subsequent aggregate-accounting gate; P0 transport bounds alone do not bound heaps, atoms, native allocations or code |
| Suspend / resume | Subsequent lifecycle gate, not required to demonstrate P0. Suspend stops guest execution but retains state/memory; resume makes it runnable again |
| Idle eviction | Host policy using activity/lifetime signals; running background Erlang processes are not automatically safe to evict just because no host call is pending |

A deadline is not the same as its enforcement latency. P0's runner can request stop
and detect failure with an external watchdog; that is not proof of an implemented
CPU limiter or an integrated deadline service. Budget exhaustion must eventually
cause an explicit terminal status or suspension policy, not disappear into host
queueing. Killing the whole host is a test harness failure mechanism, not isolate
eviction.

Bytecode-only tenants eliminate arbitrary uploaded native code, but approved
built-ins may still do expensive work. Audit them for bounded execution, yielding,
cancellation or controlled async execution. Do not promise prompt stop/suspension
until the longest non-preemptible paths have evidence and documented limits.

The suspend/resume gate must define: a safe-point acknowledgement (not merely setting
an unscheduled flag), handling of already-running/async native work, continuing
wall-clock deadlines, timer expiry and bounded pending I/O completions, admission
while suspended, memory retention, and stop of a suspended isolate. No guest callback
may run while suspension is acknowledged. Unbounded accumulation is not permitted;
there is no implication that physical timers or external I/O stop with the guest.
This is not a snapshot/persistence mechanism or a replacement for eviction under
memory pressure. P0 must not expose a fake working suspend API; unsupported optional
controls report that status explicitly.

### Lifecycle and reclamation

```text
created -> loaded -> running -> stopping -> stopped -> reclaimed
                \-> failed -> cleanup -----------------^
```

`stop` closes admission before cancellation. Running processes must reach the
appropriate exit/safe-point paths; queued timers, signals, code references and
native work retain the isolate until detached or drained. Logical stop and physical
reclamation are distinct observable events. Physical reclamation must not free
state still referenced by code, a scheduler, a host completion or a resource.

A deadline can return `pending`/`timeout`; it must not force unsafe freeing. The
host can continue polling and serving B while A drains. Completion payloads already
copied into host-owned buffers may outlive A without retaining its mutable world.
Engine shutdown requires all isolates reclaimed and handles released, otherwise
returns `busy`. The proof must include partial-bootstrap failure cleanup too.

## 5. The first executable proof: P0

### Deliverable layout (API scaffold and initial fixtures present; runtime planned)

```text
libbeam/
  CMakeLists.txt                         # builds scaffold and example
  include/libbeam/engine.hpp             # experimental API
  src/engine.cpp                        # explicit not_implemented errors
  examples/two_isolates.cpp              # runnable progress driver
  tests/fixtures/two_isolates/probe_impl.hrl
  tests/fixtures/two_isolates/a/probe.erl
  tests/fixtures/two_isolates/b/probe.erl
  tests/run_isolate_proof.py             # planned full acceptance runner
```

The current example links the C++ scaffold and can now optionally import a
[hash-checked native ERTS package](0002-native-api-link.md). The public factory still
refuses startup pending cooperative stop/join and cleanup. The eventual working
example must link against a **single** libbeam engine library. It must not exec `erl`, use Erlang distribution, call out to helper VMs or
boot several renamed copies of the runtime. Static linking is enough for P0; shared
library packaging, symbol visibility and a stable ABI follow later.

### P0 runtime profile

Start on one documented development platform with a debug interpreter build and
one shared scheduler. Implement the positive-list profile in section 2 and enumerate
its exact preloaded modules, internal processes, BIFs and native facilities. Provision
only the bootstrap needed for the listed operations, from host-supplied bytes; no
filesystem-backed code server or complete OTP boot is assumed. For P0, reject modules
with `on_load` callbacks before publication to keep startup/rollback and the first-
execution boundary explicit. Supporting `on_load` later is a separate profile change.

Tenant-visible isolate lifecycle APIs, distribution activation, dynamic native
loading, arbitrary ports/OS I/O, tracing/debug escape hatches and host-global controls
are unsupported. They must fail before effects, rather than merely being absent
from the fixture. Any trusted bootstrap-only exception must be explicit and not
reachable through ordinary tenant invocation. Already-loaded native paths require
review; denying `load_nif` alone is not a native-effect boundary.

### Fixture and assertions

A, B and C contain different implementations of **`probe` with identical exported
MFAs**. A separate common caller module ensures external imports are exercised.
Each uses ordinary Erlang operations; none calls Realm/isolate management APIs.

The host harness must assert:

1. **Embedding:** Engine initialization returns to host C++; the host continues
   executing. OS-process inspection confirms no child BEAM runtime. Thread counts
   show no per-isolate scheduler fleet; record any shared lazy thread creation.
2. **Fresh creation:** A and B are independently created after engine initialization,
   with no pre-existing tenant instance assigned to either. Both stay live throughout
   the conflicting-code tests. Do not test A and B only in separate host runs.
3. **Private same-MFA execution:** A returns `A` and B returns `B` for direct external
   calls from the common module, runtime-computed `apply`, external fun invocation
   and retained local fun invocation. Interleave repeated requests. Load/start B
   after A is already running and prove A's results do not change. No renaming,
   rewriting one shared code table between turns, or host-side substitution of
   answers. Selecting an already resident private table from actual execution context
   is legitimate; replacing global code to simulate separate worlds is not.
4. **Ordinary concurrency:** Spawn multiple local processes; exchange messages and
   exercise monitor/link exit behavior and reduction-based preemption. Keep a busy
   Erlang loop in A while B must complete requests within the harness deadline.
5. **Local services/state:** Both register `worker`, create a named ETS table `cache`,
   and use the same persistent-term key, but read back different values. Their own
   lookups/enumerations do not reveal the other's fixtures. Local atomics/counters
   work and creator exit does not incorrectly revoke same-isolate resources.
6. **Atom scope:** Create a unique runtime atom in A; B cannot resolve it through
   `binary_to_existing_atom`. It must not accidentally occur in B's loaded literals
   or bootstrap. Predefined atoms may be shared; dynamic namespace leakage is not
   acceptable. Do not count shared immutable predefinitions as a private-atom proof.
7. **Teardown with work outstanding:** Leave local timers, processes, monitor/link
   state and an accepted host invocation pending in A. Stop A. Admission closes,
   accepted calls terminate exactly once, and late work cannot target B or a later
   replacement. B's version, registry/ETS/persistent state and progress remain intact.
8. **Fresh replacement:** After A's physical reclamation, create C. Its same-name
   module returns `C`; before initializing its fixture state, registration, ETS and
   persistent-term lookups show A's data is absent. Recheck A's unique dynamic atom.
9. **Error cleanup:** Bad module bytes, failed startup, unknown MFA, thrown Erlang
   exception, rejected oversized requests and shutdown with live handles produce
   explicit statuses. Failure of A does not invalidate B or poison later creation.
10. **Repeatability:** Repeat fresh create/start/use/stop/reclaim for at least 1,000
    small isolates in one engine, keeping B alive as a sentinel. Report iteration
    count and every failed/timed-out assertion; do not replace failures with retries.
11. **Physical release:** Isolate-owned process/resource/code/timer/native-work
    counters return to zero after reclamation. Record retained engine caches
    separately with a documented lifetime/bound; RSS alone is neither a leak proof
    nor proof of prompt freeing. Allocator/sanitizer checks supplement these counters.
12. **Host survival:** Stop and reclaim all isolates, release buffers and handles,
    shut down the engine, then execute a host-side assertion before returning normally
    from `main`. Isolate shutdown must never stand in for process exit.
13. **Unsupported effects fail closed:** Exercise absent file/socket library calls
    (ordinary `undef`) and excluded engine-native operations (`notsup`), including
    dynamic NIF loading, executable ports, `halt`, internal/indirect entry points and
    attempts to access a host-owned temporary sentinel file. Confirm the host remains
    alive and the sentinel is unchanged; use a sandboxed proof host. Include
    instrumented deny-before-effect assertions for read paths: an unchanged file
    alone does not prove it was not read. Test negative paths identified in the
    profile manifest, not merely modules absent from the bundle. A missing required
    denial case fails the proof; it is not a skip.
14. **First-execution instrumentation:** Correlate host admission through isolate
    construction/load/bootstrap to the scheduler reaching the first application
    instruction. Timestamp before executing it. Report the first result separately;
    a fixture that deliberately performs a slow bytecode loop after entry must not
    make entry latency equal to completion latency. No user code runs during P0
    loading (`on_load` is rejected). Report instrumentation overhead and clock
    resolution; this debug functional test does not satisfy the optimized latency gate.

P0 proves these operations only. Full Erlang language/OTP compatibility, complete
cross-boundary adversarial coverage and multi-scheduler execution remain separate
acceptance gates even when every P0 assertion passes.

### Intended build/run contract

These are target commands for full runtime acceptance, **not currently working
commands**. The CMake scaffold now builds without OTP, but does not implement these
OTP integration options or the acceptance runner. See the
[current scaffold commands](../../libbeam/examples/two_isolates.md).

```sh
cmake -S libbeam -B build/libbeam-proof \
  -DLIBBEAM_OTP_SOURCE_DIR="$PWD/beam" \
  -DLIBBEAM_EMULATOR=interpreter -DCMAKE_BUILD_TYPE=Debug
cmake --build build/libbeam-proof --target two_isolates
python3 libbeam/tests/run_isolate_proof.py \
  --host build/libbeam-proof/two_isolates \
  --iterations 1000 --output /tmp/libbeam-proof-new
```

The build integration must drive the supported OTP/bootstrap tooling; do not hand
copy a selection of emulator objects into an archive and assume it is a valid build.
The integration must document compiler/linker flags, dependencies, PIC requirements
where applicable, entrypoint separation and the exact bootstrap artifact source.
Use the checkout compiler, not an arbitrary installed OTP compiler, for fixtures.

The runner uses a fresh output directory and a serialized build/test lock. It records
revision/dirty state, compiler/runtime variants, fixture/library/executable hashes,
actual process/thread observations, assertions, deadlines, reclamation counters and
raw timing samples. Missing assertions, skips, fewer iterations, unexpected runtime
variants and child-runtime shortcuts fail the run. A timeout is failure or pending
reclamation, never a successful destroy. Hard external timeout kills only the proof
host process group, with evidence preserved. No hosted result is inferred locally.

## 6. Implementation sequence and source investigation

Checkmarks cover only the stated evidence, not the complete isolate runtime. Each
implementation slice needs tests and a reviewable commit; a design document does
not check off an implementation step. See the [baseline/link evidence](0002-baseline-evidence.md)
and [initial engine-seam findings](0002-engine-seams.md).

- [x] **P0-01 — Reproducible clean-upstream baseline.** Build the pinned `beam/` source
  in an external worktree
  from a clean checkout; repair tooling paths deliberately. Inventory scripts and
  workflows inherited from the old root layout are not assumed runnable unchanged.
  Standalone `erl` may remain as temporary build/debug tooling, but preserving its
  behavior is not a product requirement or an ongoing compatibility gate.
  **Reset:** previous 148 + 44 + 5 results belong to the archived Realm fork.
  **Fresh evidence:** snapshot `87d63923` configure/build/preload rebuild, 84 focused
  + 35 resource cases, three startup probes and archive link all pass on ARM64 macOS
  debug interpreter. No old Realm result carries over; see the transition record.
- [ ] **P0-02 — Engine/instance source map.** Classify globals, locks, caches, startup
  order, OS registrations and asynchronous ownership. Start with `erl_init.c`
  (`erl_start`, `erl_init`, bootstrap/system processes), `erl_process.c/.h`,
  `erl_sched*`, `erl_alloc*`, `atom.c`, `module.c`, `export.c`, `code_ix.c/.h`,
  `erl_fun.c`, `register.c`, `erl_db*`, `erl_bif_persistent.c`, `erl_hl_timer.c`,
  `erl_proc_sig_queue.c`, `erl_nif.c`, driver/port infrastructure and emulator dispatch.
  Paths here are relative to `beam/erts/emulator/beam/`; wildcard names denote source
  families. Explicitly locate process-fatal paths and startup-only assumptions.
  Produce the versioned positive-list profile manifest and trace its minimal bootstrap
  dependencies, including deny-before-effect coverage for excluded native operations.
  **Partial:** concrete startup, process-exit and world-global anchors are mapped in
  the engine-seam note; the exhaustive ownership map/profile manifest is still open.
- [ ] **P0-03 — Library entry/exit seam.** Split executable setup/CLI behavior from
  engine construction, execution and shutdown. Link a minimal host, return control
  to it, and destroy an engine normally with zero isolates. One engine only initially.
  **Partial:** the prior archive-link witness did not call `erl_start`. The startup
  phase split is retained directly in `beam/erts/emulator/beam/erl_init.c` on the clean
  upstream base. A new experimental POSIX entry genuinely starts the global emulator
  and returns to its C++ caller; bytecode runs in that same PID. It is process-lifetime
  only and retains fatal startup/exit behavior. The mode switch, standalone signal
  administration, spawn/forker drivers and node bootstrap services have since been
  deleted: [current evidence](0002-single-runtime.md). No engine shutdown or isolates;
  [historical first-return evidence](0002-engine-start-evidence.md).
  P0-03 remains open until creation and destruction both return safely.
- [ ] **P0-04 — Isolate context and fresh bootstrap.** Introduce explicit owned state,
  staged initialization/unwind and pre-publication membership. Reuse engine scheduler
  infrastructure; test two live contexts and failed creation cleanup before claiming
  independent code or services.
- [ ] **P0-05 — True code/atom environments.** Make loader, exports/imports, funs,
  literal ownership and interpreter dispatch isolate-aware. Establish dynamic atom
  scope and bootstrap representation. Pass the simultaneous conflicting-MFA witness.
  Module renaming is not an intermediate result that satisfies this item.
- [ ] **P0-06 — Local services and native ownership.** Implement the P0 registry,
  ETS, persistent-term, timer and resource behavior with provenance retained through
  deferred work. A profile denial cannot satisfy a P0 operation promised as local.
- [ ] **P0-07 — Embedding transport and lifecycle.** Implement bounded binary calls,
  completion ownership, cancellation, stale-work rejection and stop/reclaim semantics.
  Scope/reject host-fatal and excluded tenant effects; ensure B survives A's eviction.
  Report pending physical reclamation honestly. Do not claim CPU-budget enforcement
  or suspension from successful process preemption or a host watchdog.
- [ ] **P0-08 — Full executable witness.** Deliver the C++ host, independently compiled
  bundles, strict runner and 1,000-cycle sentinel test above. Record supported platform,
  build flavor, first-execution instrumentation and all profile denial cases.
  This includes all 14 P0 assertions; no security or density approval is implied.
- [ ] **P0-09 — Measurements and review.** Record phase-separated results and review
  leaks, host-global assumptions and blockers. Decide go/no-go for the next stage
  based on evidence. Keep functional proof, latency and simultaneous-density gates
  distinct. A successful P0 does not automatically satisfy the target below.

Particularly difficult seams are atom IDs embedded in terms/code, export and fun
entry lifetime, code purger/literal collector ownership, scheduler-safe teardown,
NIF resources outliving creators and global `halt`/signal handlers. These may require
structural changes, not mechanical parameter additions. If the cost of an ownership
choice is prohibitive, amend this RFD explicitly rather than silently weakening P0.

## 7. Measurements and subsequent gates

Measure independently:

- Cold host/engine startup (including process initialization).
- Fresh empty-isolate construction in an already initialized engine.
- Bootstrap, module load/link, scheduling delay and **first application instruction**.
- Application initialization, first response and subsequent request execution.
- Logical stop and eventual physical reclamation, including tail latency.
- Incremental physical memory and isolate-accounted memory at increasing live counts.
- Host responsiveness and B latency while A creates, runs and stops.

Use repeated fresh creations, keep raw data and report p50/p99 with sample counts,
clock resolution, dispersion and caching conditions. Separate first-ever code-cache
use from later reuse of immutable artifacts. Record debug results as debug results;
optimized measurements are necessary before performance decisions.

### First-execution target, not a completion deadline

The adopted performance objective is **less than 10 ms p99 from accepted creation
request to first application instruction**, with an already initialized engine and
precompiled module bytes available locally. This includes private state construction,
loading/linking (and JIT work where used), required bootstrap and scheduling delay.
It must not stop at handle allocation or mean merely that the isolate is runnable.
Admission queue time and end-to-end request-to-first-execution are also reported;
rejected requests are counted, not silently omitted from a success claim.

```text
creation requested -> admitted -> construct -> load/bootstrap -> scheduled -> first instruction
       | admission wait |<--------------- target measurement ---------------->|
       |<-------------------- end-to-end report ------------------------------>|
                                                                              | application work
                                                                              | may take seconds
```

Code acquisition before the request and one-time engine boot are separate metrics.
Do not fetch/compile code or execute tenant initialization before the timed interval
and call that fresh startup. Shared immutable caches are allowed, but cold-artifact
and reused-artifact conditions must be distinguished. In P0 the measured entry is
the first application instruction because load-time execution is prohibited. Future
profiles permitting `on_load` or other tenant initialization must report both the
first tenant instruction and requested entrypoint, with initialization in the latter's
elapsed time; they must not silently preserve incomparable benchmark labels.

This is an objective, **not an achieved result or an unconditional real-time guarantee**.
Hardware, artifact size/profile, scheduler count, resident population, creation rate,
CPU/memory load and sample count must be pinned before a latency acceptance run.
The initial acceptance scenario should include 1,000 resident small isolates, not
only an empty engine. An empty-instance sub-millisecond result is useful but does
not substitute for first-execution latency. No per-isolate memory budget is agreed
yet; measure before setting it. Application completion has an independent policy.

### Simultaneous density and controls: follow-on tracking

The 1,000-cycle P0 test is sequential churn, **not** 1,000 live isolates. Keep these
additional gates open rather than expanding P0 into an entire production platform:

- [ ] **S1 — Optimized first-execution latency.** Implement the timestamped benchmark
  above, pin its workload/hardware envelope and evaluate the <10 ms p99 objective.
  Preserve all samples, errors and admission statistics, including cold-cache runs.
- [ ] **S2 — Concurrent residency and churn.** Measure 2, 100 and at least 1,000
  simultaneously live small isolates, each with distinct mutable state. Report
  incremental memory, shared caches, threads, first-execution/response tails and
  reclamation. Create/destroy additional isolates while residents remain responsive.
  Thousands of resident instances is not a claim of thousands of CPU cores.
- [ ] **S3 — Execution budgets and eviction.** Implement scoped wall-clock deadlines,
  aggregate CPU/memory accounting and host-configured exhaustion policy. Validate
  preemption/cancellation of approved native paths, time-to-stop and physical release
  while other isolates make progress. Bound admitted work and test saturation.
- [ ] **S4 — Suspend/resume.** Implement and test the safe-point, timer, I/O, queue,
  budget and memory-retention semantics in section 4. Test stop while suspended and
  resume after expiry; do not call suspension memory reclamation or persistence.

Additional gates include optimized interpreter/JIT across supported architectures,
multi-scheduler/dirty execution, selected compatible supervision/application libraries
and independently versioned Erlang/Elixir/Gleam bundles **within the reduced profile**,
explicit host-capability mediation, fairness, OOM/fuzz/sanitizer coverage, independent
security review, portable packaging and orchestration integration. Full conventional
OTP/OS compatibility and tenant native extensions are not default future requirements.
Each profile expansion needs its own compatibility and security decision.

The host can eventually provide a worker-style service interface and route events
between isolates. A separate HTTP/orchestration project should consume libbeam's
interface, not require ordinary tenant code to manage its own isolation machinery.

## 8. Workerd lessons and evidence limits

The architectural reference is the inspected workerd revision
`f4ebbae6562718e53afbc3bba0f882266bd89529`, not an assertion about all production
Cloudflare implementation details:

- [Engine initialization and isolate construction/destruction](https://github.com/cloudflare/workerd/blob/f4ebbae6562718e53afbc3bba0f882266bd89529/src/workerd/jsg/setup.c++): process-level initialization is distinct from `v8::Isolate::New()`/`Dispose()`.
- [Dynamic loader and server construction](https://github.com/cloudflare/workerd/blob/f4ebbae6562718e53afbc3bba0f882266bd89529/src/workerd/server/server.c++): named loads can reuse instances; unnamed loads create fresh instances; startup promises gate requests. The server uses a null isolate limit enforcer, not the full production limiter.
- [Worker/Script/Isolate contracts](https://github.com/cloudflare/workerd/blob/f4ebbae6562718e53afbc3bba0f882266bd89529/src/workerd/io/worker.h): these are distinct objects, and ordinary Worker instances can serve multiple requests.
- [I/O context ownership](https://github.com/cloudflare/workerd/blob/f4ebbae6562718e53afbc3bba0f882266bd89529/src/workerd/io/io-context.h): context destruction cancels associated I/O; wrong-context resource use is rejected.
- [Built-in compilation cache](https://github.com/cloudflare/workerd/blob/f4ebbae6562718e53afbc3bba0f882266bd89529/src/workerd/jsg/compile-cache.h): immutable reusable compilation data is different from reused mutable application state.
- [Security warning](https://github.com/cloudflare/workerd/blob/f4ebbae6562718e53afbc3bba0f882266bd89529/README.md): workerd alone is not the full hardened production sandbox.

Cloudflare's public [Workers architecture documentation](https://developers.cloudflare.com/workers/reference/how-workers-works/#isolates)
says a single runtime can run hundreds or thousands of isolates. That is a useful
density reference, not a hardware-independent per-process capacity guarantee.
The [128 MB memory limit](https://developers.cloudflare.com/workers/platform/limits/#memory)
is an allowance, not a reservation or measured startup footprint; the
[one-second global-scope startup limit](https://developers.cloudflare.com/workers/platform/limits/#worker-startup-time)
is not isolate-creation latency. None of these establishes libbeam performance.

No workerd/V8 creation benchmark was performed in that source study. Nor does this
RFD assume V8's single-executor isolate scheduling should replace BEAM's process
scheduler and preemption model.

Related design/evidence:
[Realms RFD](0001-beam-realms.md),
[private-code contract](0001-code-environment-contract.md),
[shared-code witness](0001-code-environment-spike.md),
[resource ownership and remaining bypasses](0001-shared-resource-enforcement.md).
Existing evidence remains historical; it does not validate the embedding proof.
