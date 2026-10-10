<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 Leandro Ostera <leandro@ostera.io> -->

# RFD 0003: Build libbeam additively from BEAM components

Status: **Accepted direction; G1 lifetime and M2 selected-profile public Isolate execution demonstrated.**

The [first-execution record](0003-first-execution.md) connects the earlier
[A03–A07 checkpoint](0003-loader-program.md) to real generated transformations,
emission, publication, interpreter execution and copying GC for a limited profile.
Full loader/literal/instruction breadth and stateful process machinery remain
outstanding. The subsequent [C Engine lifecycle integration](0003-engine-lifecycle.md)
passes the unchanged create/shutdown/recreate target over this actual substrate;
the unchanged stateful two-Isolate target still refuses unsupported module admission. The subsequent
[ordinary-binary slice](0003-owned-binaries.md) connects native payload lifetime to
literal loading, metadata copies, collection and copied-byte invocation. The
[owned executor](0003-owned-executor.md) now runs that actual machinery on one lazy
shared worker, with construction rollback, cancellation and physical join.
[Public Isolates and calls](0003-public-isolates.md) now connect it to copied transport,
capacity, once-only results and physical selected-world reclamation. M3/M4 remain
outstanding; real persistent Erlang processes are not implemented by call handles.

See the [A01/A02 term-and-atom admission record](0003-terms-and-atoms.md) for the
selected BEAM representation, namespace identities, transactional image bindings
and remaining scope.

The [additive runtime inventory](0003-additive-runtime-inventory.md) records the
remaining dependency clusters, fixture-derived requirements and implementation
order. Stateful Isolate acceptance remains outstanding; G1 is not RFD completion.

## Decision

Stop using whole-OTP reduction as the primary implementation strategy. Build the
runtime under `libbeam/`, admitting components from `beam/` into an explicit,
destructible ownership structure. Keep the ownership layer and transplanted
implementation in **C initially**. The host-facing interface remains C++.

This replaces the construction strategy in [RFD 0002](0002-libbeam-isolates.md),
not its Engine/Isolate contract or its unchanged executable acceptance example.
It is not a clean-room implementation of Erlang, a new bytecode format, or a
simultaneous translation to C++, Rust, or another language.

## Why change direction?

The subtractive approach established valuable facts:

- Native startup can return to a surviving host, and preparation can omit OTP
  bootstrap and runtime thread launch.
- Actual atom/module/export/code metadata, registrations and persistent-term
  storage can follow namespace ownership rather than tenant-selecting globals.
- Processes, code, literals and deferred contexts need physical lifetime leases.
- Poll groups, allocator backing, thread-progress storage, TLS-key bookkeeping
  and constant arenas have identifiable owners and some guarded cold cleanup.
- A compilable C++ API is not evidence of a working, destructible runtime.

See the [ownership inventory](0002-current-ownership-inventory.md) for the
committed checkpoint and its limitations.

However, changing storage ownership has repeatedly exposed more implicit
consumers. Startup initializes interconnected scheduler, allocator, thread-library,
clock, native-helper and diagnostic-world state. The traditional runtime expects
process exit to finish reclamation. Moving a field does not supply a stop protocol,
retire callbacks, destroy TLS values, release mappings, or undo once-only init.

At that transition the public factory could not honestly succeed. Neither repeated
Engine lifetime nor simultaneous executable Isolates had passed. We should not keep expanding the
initialization graph and only afterward discover what its destructor requires.

### Transition checkpoint

At this decision, `main` is at `a2d905b5`; additional lifecycle work is uncommitted.
That work splits a shared preparation prefix from diagnostic world construction,
copies startup arguments into Engine-owned storage, and adds narrow pre-world
constant and early-scheduler-storage retirement. It does **not** supply aggregate
shutdown, recoverable native initialization, or reset of the runtime claim.

The shared-prefix validation artifacts include
`/tmp/libbeam-lifecycle-shared-prefix-reviewed/summary.json` and
`/tmp/libbeam-lifecycle-shared-prefix-reviewed-verified/summary.json`. They are
local historical evidence, not reproducible repository inputs or acceptance for
this new runtime. Later uncommitted release-phase/provenance edits are not covered
by that run. Preserve the work without describing it as a finished lifecycle.

## Architecture and source boundaries

- `beam/` remains the tracked OTP source/reference and the previous experiment.
  It is **modified**, not a pristine upstream checkout. Do not erase the experiment
  or silently treat its results as results for the new implementation.
- `libbeam/core/` contains the new C implementation and internal C interfaces.
  Sources enter its build through an explicit list, not recursive inclusion of OTP.
- `libbeam/src/` and `libbeam/include/` contain the C++ host adapter/public API.
  The adapter owns C handles; it does not provide a second ownership architecture.
- Tests and the existing examples stay under `libbeam/`. Historical archive/package
  probes can remain runnable, but are diagnostic/reference artifacts, not a runtime
  backend that the new implementation falls back to.

There will be one production implementation, not selectable legacy/new execution
modes. During construction, a standalone component target may exist before it can
back the public API. Do not wire a control-only object into `Engine::create()` and
call that a successful Engine.

### Ownership

The Engine owns shared execution infrastructure. Each Isolate owns its private
mutable world: heaps/processes, atoms, code, registrations, persistent data and
service state as those features are admitted. Shared immutable implementation data
need not be copied merely to remove a global declaration.

Use explicit owner/context parameters, not current-isolate TLS, mutable root
swapping, or namespace selection through a global singleton. Host-managed objects
must not become process-lifetime singletons. Raw pointers are borrowed references
with defined lifetimes; queued work and escaped runtime objects need retention
through their physical destruction.

Initialization is staged and fallible. Each completed stage has a reverse operation;
partial construction unwinds its completed prefix. Quiescence, admission closure,
retirement and physical release are distinct operations. A destructor must reject
live users rather than force-free them. Do not hold a lock or lose an allocation
base on an initialization error path.

Initially, management operations are serialized on the documented control thread.
Concurrency enters with explicit scheduling, queue and lifetime protocols—not with
an unsupported assumption that a mutex around creation makes the runtime safe.

## Language and fidelity policy

**C ownership layer; C transplants; C++ host interface.**

Preserve proven term representation, execution algorithms and relevant source
structure where practical. Make ownership changes in C, including substantial
refactoring where necessary. This is not a requirement to keep unsafe globals or
an unusable startup dependency just to minimize a diff.

Do not combine an ownership transplant with a discretionary language translation,
algorithm replacement, performance rewrite or new term representation. Keeping
those changes separable makes behavioral failures and upstream comparisons much
more tractable.

Later, a component may justify a C++ or Rust implementation. Decide then against
its tested boundary and actual safety benefit. Moving-GC pointers, tagged terms,
custom allocation and generated-code interfaces would still require carefully
controlled unsafe operations in Rust. Neither C++ wrappers nor a language change
alone establish memory safety.

### Transplant record and admission checklist

For every transplanted source cluster, record:

1. Exact upstream commit and paths, plus local snapshot revision/content hashes
   if importing adaptations from this repository. Distinguish upstream code from
   earlier libbeam modifications. Keep license headers and third-party notices.
2. Required semantics and the executable test that first needs this cluster.
3. Dependencies: classify each as immutable shared data, Engine-owned state,
   Isolate-owned state, borrowed reference, or excluded effect.
4. Actual allocation, lookup, callback and execution paths changed for ownership.
5. Construction failure, admission closure, quiescence and destruction behavior.
6. Semantic tests, failure injection, lifetime tests, and known unsupported cases.

Do not admit an entire subsystem because one helper is convenient. Conversely,
do not pretend that a tightly coupled interpreter/GC/process/loader cluster can
always be ported one file at a time. Bring the smallest honest dependency cluster,
keeping observable behavior and lifetime evidence together.

Unsupported operations fail explicitly before effects. No success-shaped stubs,
mock term executor, native helper VM, module renaming, or per-Isolate emulator
process may stand in for the acceptance contract.

## Execution-led milestones

These are vertical slices, not a mandate to finish each subsystem in isolation.

### 0. Establish the construction boundary

Create the C core build boundary and ownership/failure primitives without linking
whole ERTS. Add deterministic allocator failure injection and exact resource
balance tests. Identify the first term/heap/interpreter dependency cluster and
write its transplant record before copying it.

A bootstrap allocation domain is supporting infrastructure, not a replacement GC
or proof that an Engine exists. Avoid recreating every previous ownership container
before executing a BEAM instruction.

### 1. Real Engine lifetime

Construct the actual shared infrastructure needed by the selected execution slice,
without implicit Isolates, OTP services or unsolicited workers. Drive
`examples/engine_lifecycle.cpp`: create, shutdown, destroy, create again.

Inject failures at each admitted initialization stage; the same host must then
create successfully. Require resource balance and host survival. No retained
runtime singleton, `_Exit`, process restart or empty successful handle counts.
An Engine that passes this gate may still explicitly reject unsupported Isolate
operations; report the implemented execution substrate precisely.

### 2. First real BEAM execution

Transplant the minimum coherent term/heap/GC, code-loading, process-context and
interpreter cluster to load and execute a tiny compiler-produced BEAM function.
Prefer the interpreter first; defer JIT, distribution and broad native services.
Preserve binary-format and supported Erlang semantics rather than introducing a
special bytecode subset format to simplify the witness.

Destroy the Isolate and repeat. Add allocation failures and exception paths as the
cluster is admitted. A stock OTP reference can establish expected behavior; it
cannot establish our ownership or reclamation.

### 3. Independent executable worlds

Load conflicting same-name modules in simultaneously live Isolates. Add the
process/mailbox/state behavior needed by the existing fixtures. Retire one world
while its peer continues; create a replacement without restarting the Engine.

### 4. Unchanged acceptance and growth

Pass `examples/two_isolates.cpp` **without modifying it to weaken the contract**.
Add churn, queued-work retirement and executable-code/literal lifetime coverage.
Then expand the positive-listed BIF/native profile and deliberately selected OTP
services. Security, performance, quotas, density and suspend/resume remain separate
gates; passing the example proves none of those automatically.

## Validation and transition rules

- Keep existing refusal behavior in the public API until a real core can back it.
- Run native validation under the existing user-wide lock, in fresh external build
  outputs; record source hashes and preserve failures. Never commit generated BEAMs.
- Test components independently, but label component tests as such. Whole-lifecycle
  acceptance requires the actual host examples plus failure recovery/resource tests.
- Compare supported semantics against the pinned OTP reference. Supplement with
  sanitizers and platform-specific resource observations; record tool limitations.
- Freeze further broad subtractive migration work. Fix the reference experiment
  only when needed for evidence or a specifically documented transplant.
- Do not delete `beam/` or its evidence during this pivot. Remove obsolete production
  linkage only when the new core replaces it; never use that linkage as a shortcut
  around missing implementation.

## First implementation in this RFD

The initial additive component is a C bootstrap allocation domain with explicit
allocator callbacks, exact-base ownership, fallible creation/allocation, wrong-owner
release refusal, and busy destruction refusal. Its standalone test checks failure
prefixes, successful retry, independent domains, alignment and resource balance.
It is intentionally **not wired into the public Engine factory**. At that initial
checkpoint no BEAM machinery had been transplanted. The next
[owned BEAM-image slice](0003-first-loader-slice.md) now adapts the real file-parser
boundary; it still does not execute bytecode or complete Engine lifecycle.

Initial validation: a fresh standalone CMake build passed both CTests (allocation
component and honest API-scaffold refusal). The allocation component also passed
UBSan with nonrecovering diagnostics and strict compiler warnings. The lifecycle
example was run separately and still exited 1 at `Engine::create`; it is not an
expected-failure acceptance test. The two-Isolate example was not changed.

Source hashes, command results and logs are recorded locally under
`/var/folders/v0/6x4x9vzn10gbdxpzdnsfyxwh0000gn/T/libbeam-additive-foundation-qey77qkm/`.
These results cover the new component, not the previous uncommitted ERTS changes,
JIT, other platforms, or a working Engine.

The [first loader admission record](0003-first-loader-slice.md) identifies the
execution dependency cluster and its first image-preparation boundary. Next,
continue through the actual code-reader/term/process/interpreter dependencies and
integrate their shared infrastructure with reversible initialization. The progress measure
is the executable milestone, not the number of transplanted files.
