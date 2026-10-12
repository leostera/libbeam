<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 Leandro Ostera <leandro@ostera.io> -->

# RFD 0004: Isolate memory and invocation execution policy

Status: **Policy selected before enforcement. Accounted memory is implemented;
[invocation wiring](0004-invocation-work-wiring.md) now has actual worker charging
and causal cancellation, with native coverage/failure audit still unfinished.**

## Decision

Use Engine-owned default policy, copied/resolved into each new Isolate with optional
per-Isolate overrides. Default accounted Isolate memory is **256 MiB**. Default
invocation execution credit is **10,000,000 work units**. These are initial policy
values, not measured deployment recommendations. Both are configurable finite
positive limits, validated before Isolate publication; invalid or overflowing values
are refused without silently clamping or interpreting zero as unlimited.

Memory is shared by the world, not reset per call. Execution credit is fresh for
each separately admitted invocation, not reset by waits, retries, spawning or
scheduling. Credits follow causal work, including child actors and messages.
Preserve a loaded world after safely contained private invocation cancellation;
terminate/reclaim a world when an overrun cannot be contained without asserting
unproved persistent-actor invariants. No guest-state rollback is promised.

This extends [RFD 0003](0003-additive-runtime-construction.md), preserving the
[public lifetime/transport contract](0003-public-isolates.md) and the completed
stateful acceptance. It does not add multicore, NIFs, ambient host capabilities,
hot reload, lazy loading, snapshots or a serving OTP VM.

## Evidence and motivation

The current Engine owns one allocator domain and one shared worker. Worlds own code
spaces and calls; persistent spawned actors share the world. The public transport
limits bound copied input/output reservations, not heaps, code, offheap binary
capacity or CPU work. Existing cooperative instruction/reduction slices schedule
work but do not establish a per-request resource allowance.

Public-source research is retained in the private ai-research repository at
`personal/libbeam/request-budgets-and-code-reuse/`, with pinned sources and no copied
proprietary material. Relevant primary sources are:

- [V8 Isolate APIs](https://github.com/v8/v8/blob/7d259d7c2090bb38af1fd9f6ca67c6dc8b6f88d8/include/v8-isolate.h): termination/restored execution capability, not rollback.
- [workerd limiter interfaces](https://github.com/cloudflare/workerd/blob/f4ebbae6562718e53afbc3bba0f882266bd89529/src/workerd/io/limit-enforcer.h): separate Isolate/startup and IoContext/request policy.
- [workerd standalone implementation](https://github.com/cloudflare/workerd/blob/f4ebbae6562718e53afbc3bba0f882266bd89529/src/workerd/server/server.c++): inspected general limiter methods are no-ops, not production-limit evidence.
- [Managed Workers limits](https://developers.cloudflare.com/workers/platform/limits/): documented per-request CPU and per-Isolate memory; not a guarantee of instance reuse after every overrun.

Accessed 2026-10-12. The research distinguishes product documentation, public-source
facts and our BEAM design inferences. It also covers code-cache/source reuse and
bindings, whose implementation remains outside this goal.

## Host model

The host constructs an Engine with default limits, then creates Isolates with
resolved limits. Changing a host configuration for a future Isolate cannot mutate
the policy of an already published world. Overrides may raise or lower either
limit; an override is host authority, never a guest term or capability.

Every accepted `start`/`call` creates a distinct execution-budget owner before its
process becomes runnable. Pending calls retain their budget even when the host
handle is dropped. A wait deadline expiring only stops waiting; it neither cancels
execution nor grants fresh credits. Successful copied output is consumed once.

A healthy pure-function world can serve another request after one private
invocation exhausts its execution allowance. If request A sends work to an existing
persistent actor, the actor may already have changed its state when A overruns.
Canceling A cannot reverse that change. The conservative first policy therefore
retires that world when cancellation cannot safely contain A's effects; peer worlds
and the Engine survive. A replacement is a fresh world, not revival of a tombstone.

## Memory ownership and accounting

Account actual runtime allocation requests plus owned allocation headers/metadata,
not only logical live term sizes. The ledger has current, peak and finite limit;
checked arithmetic precedes allocation. Policy rejection is a distinct
infrastructure limit status, never system OOM or a guest `badarg`.

Include all Isolate-owned backing through physical release:

- private code spaces, atoms, module/export/import tables and literals;
- loader/verifier/decompression/preparation scratch, including staged graph loads;
- process heaps/stacks/register storage, task bookkeeping and budget metadata;
- binary payloads and retained writable capacity, offheap owners and temporary copies;
- mailbox fragments, reference/monitor/registry/receive-marker state and timer records;
- internally copied request bytes and ready response backing until transferred or released.

Old and replacement heaps count simultaneously during copying GC. Spare binary
capacity counts; soft vheap pressure is not the memory quota. Partially prepared
modules/processes/messages unwind their allocation prefix. An allocator callback
failing is still OOM, even when a policy limit also exists.

Shared Engine infrastructure and host-owned control tombstones/independently copied
host vectors are separately documented exclusions. Runtime memory must not become
uncharged by moving its allocation onto the Engine domain: task/message allocation
sites that are currently there need explicit world ownership. A ready internal
response is not a detached host vector. Pending request metadata/transport backing
must remain bounded and accounted independently of how the caller drops handles.

This ledger is not RSS, malloc carrier overhead, stack usage of native host threads
or total process memory. No process-wide RSS ceiling or adversarial containment
claim follows. Shared prepared-code caching is not implemented; any later design
must explicitly settle shared backing and charging rather than silently charging it
once or repeatedly.

## Execution work model, version 1

The default unit is a documented estimate of admitted runtime work, not elapsed
milliseconds or a promise that all units cost equal CPU time. The initial allowance
is chosen to permit meaningful small applications while remaining finite; startup
benchmarks may justify explicitly versioned future defaults.

Charge at least:

- one unit for each dispatched native interpreter instruction;
- explicit visits/iterations in native helper loops (term/map/descriptor scans,
  comparisons, integer/regex processing and mailbox examination);
- `ceil(bytes / 16)` for bulk copying/processing and
  `ceil(words / 16)` for heap/binary allocation initialization and GC scanning/copying;
- preparation of guest effects such as child/message/monitor publication.

An empty operation may still have fixed dispatch/helper costs. Waiting actors,
host waits and Engine idle polling consume no guest execution credit. Work performed
for another Isolate must never debit this one's invocation. Charges are checked
before effects, or at bounded native continuation points whose unpublished state
can be safely discarded. Existing `last_native_cost`/GC/reduction estimates are
inputs to audit, not an automatic complete coverage proof. Do not double-charge
those same operations merely because both scheduling and accounting use a cost.

All admitted native loops need a bounded/charged path. A monolithic opaque helper
without defensible work accounting must be refused or adapted before its effect is
admitted; an output size bound alone does not establish CPU work. Native helpers
remain native algorithms with owned roots, not successful substitutes.

## Causal ownership and persistent actors

Budget records are explicitly owned C metadata under Engine serialization. They
are not atoms, PID words, guest reference payloads, tenant TLS or moving heap aliases.

1. A root invocation owns fresh credit; child tasks inherit/retain that owner.
2. Prepared messages retain the sending work's budget before queue publication.
3. Selective-receive candidate examination/import and its matching instructions
   charge the candidate's causal budget. Skipping restores the prior execution
   owner; accepting a message continues its causal owner through handling/reply.
4. An actor waiting with no candidate does not consume idle credit. Timed wakeup
   and other ensuing work remain attributable to retained prior causal credit.
5. A completed host call may leave background work; that work retains the original
   unused credit, not unlimited permission. Spawning or sending cannot create credit.
6. A later unrelated request gets its own fresh owner; it cannot reset older
   queued/running work's credit. Keep current same-world scheduling rather than
   silently serializing all public invocations to simplify accounting.
7. Sender death, message unlink, skipped/stale markers, cancellation, queue drain
   and world retirement release every corresponding budget lease exactly once.

The implementation must audit receive scanning before removing a message, not only
charge after consumption: otherwise command matching/import can bypass the caller's
allowance or repeatedly drain an old startup budget. Monitor/error delivery and
native continuations likewise need explicit attribution. Cross-family interactions
set an irrevocable escape/unsafe-containment fact before effect publication.

## Exhaustion and cancellation

Resource exhaustion is terminal infrastructure status for the affected invocation,
not a catchable guest error and not host process termination. Cleanup does not
require new guest allocations or guest finally/catch execution.

For a cancellable private family, stop its runnable processes at safepoints; drain
its queued effects, native continuations, timers and monitor ownership; retire its
roots/code leases before advertising containment complete. Already terminal copied
results are not overwritten. Keep the world loaded and available only after these
invariants hold. Cleanly rejected construction/allocation prefixes may similarly
leave a healthy world reusable.

If effects escaped into a pre-existing actor, a persistent registration/background
family, or another invocation in a way that cannot be contained, synchronously close
world admission and cancel/drain the world. Preserve an explicit originating limit
outcome; other pending calls receive documented cancellation. Resolve self-send and
same-family communication as owned private cases, not indiscriminate world poisoning.
Repeated wait timeouts and mere handle drop do not trigger either protocol.

This distinguishes memory-limit denial, invocation work exhaustion, unsafe world
retirement, guest exceptions and explicit host stop. It does not promise rollback
of state or exactly-once guest side effects after interruption. A wider actor-reuse
policy requires additional interruption/recovery invariants and evidence.

## Alternatives and drawbacks

A lifetime CPU/work quota eventually kills a healthy reused server. Resetting one
world counter per request allows previous background work to borrow unrelated
credits. Always destroying the world on any refusal wastes pure-function reuse.
Universal persistent-actor preservation after arbitrary interruption asserts guest
invariants the runtime cannot prove. Silently serializing requests would change
existing scheduling and still leave background attribution unresolved.

Our policy has real costs: allocation-site ownership work, retained budget leases
in messages/tasks, charge coverage throughout native loops, and conservative world
retirement for escaped effects. Work units are approximate and versioned, not a
security or CPU-time certification. Shared actors can reach limits through combined
causal work; tests and diagnostics must make that attribution understandable.

## Verification and non-goals

Before claiming enforcement, test Engine defaults/overrides, exact threshold and
peak behavior, overflow, loader scratch, GC old-plus-new backing, native bulk/scan
work, child inheritance, selective receive budget transfer, skipped messages,
background work after response, concurrent calls without credit reset, pure reuse
and unsafe actor retirement. Fault every relevant preparation prefix; retain exact
resource balance, peer progress, closed handles, detached replies and physical join.

Run fresh full Debug/Release/UBSan validation under the shared lock and preserve
unchanged M0–M4 acceptance. Execute all whole-application routes and malformed
protocol cases through real Isolates; ordinary OTP is only a reference/compiler.
No general OTP service, capability broker, NIF, multicore, suspend/resume, Linux,
JIT, quota-completeness, security or performance claim is added by this policy.

No blocker remains in the policy choice itself. Implementation mechanics and unit
calibration are test-driven work; a consequential change to containment semantics
or accounting scope requires explicit review rather than a silent relaxation.
