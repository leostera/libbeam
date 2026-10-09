<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 Leandro Ostera <leandro@ostera.io> -->

# RFD 0002: implement the two-isolate example, one real boundary at a time

## Working method

The driver is [`two_isolates.cpp`](../../libbeam/examples/two_isolates.cpp).
Its expected results remain fixed. We make the program advance by implementing
runtime behavior, not by relaxing assertions or manufacturing successful handles.

The experimental API is now in
[`include/libbeam/engine.hpp`](../../libbeam/include/libbeam/engine.hpp), with a
buildable implementation in [`src/engine.cpp`](../../libbeam/src/engine.cpp).
Unfinished operations return a structured `ErrorCode::not_implemented` naming the
operation. The example turns that into a visible error and nonzero exit status.
`unsupported` remains distinct: a deliberately excluded operation, not unfinished
implementation work.

**Current frontier: `Engine::create`.** The native build can now link ERTS and
prepare native substrate without booting OTP or starting workers in a dedicated
diagnostic, but the public API does not yet initialize an owned Engine or construct
an isolate. See [unbooted preparation](0002-unbooted-preparation.md) for evidence and
the remaining unbooted-state ownership/cleanup gate. No successful handle or
lifecycle completion is simulated.
The opaque implementation structs contain no runtime state and are never allocated
by the failing factories. Their default destructors are safe only for this empty
scaffold; they are not a future ownership strategy.

This intentionally supersedes the earlier declaration-only/no-link approach.
Stubs for the **new API being built** are useful progress markers. Deleted legacy
OTP services stay deleted; we do not restore their compatibility shims.

## Milestones and evidence

Each milestone needs its own focused test in addition to advancing the example.
A frontier test that expects an unfinished operation is a scaffold test, never a
passing two-isolate acceptance result. Update that expectation only with evidence
for the preceding behavior. Keep the complete example's exit-zero requirement.
Every stage introducing owned state also implements its failure/destructor cleanup:
M6 extends that foundation to running worlds, rather than postponing all cleanup
until the end.

### M0 — Buildable, explicitly failing API scaffold (implemented)

- C++17 library target `libbeam::libbeam`, executable `two_isolates`, CTest coverage.
- All declared operations link; every operational method reports `not_implemented`.
- No build-time OTP dependency or hidden child VM at this stage.
- Gate: build/link, structured `Engine::create` error, example exits 1 without its
  success marker. This is not emulator startup or isolate evidence.

Validated under the shared build/test lock with AppleClang 17: CMake
configure/build, 1 CTest, and 23 Python tooling tests pass. Running the executable with the previously compiled A/B fixtures exits
1 with `not implemented: Engine::create`; evidence is
`/tmp/libbeam-api-scaffold-verified-xtdln078/summary.json`, status
`expected_scaffold_failure_not_runtime_acceptance`. No ERTS runtime tests are
claimed for this scaffold-only change.

### M1 — Real engine ownership and safe failure paths

**API:** `Engine::create`, `Engine::shutdown`, move/destructor behavior.

**Partial progress:** native archive integration, retained joinable scheduler-family
handles, and a preparation-only native entry are implemented. Preparation is now
separate from OTP bootstrap and worker launch; its witness observes zero processes,
ports, loaded BEAM code, system-process roots and new OS threads.
[Native ownership migration](0002-native-ownership.md) now introduces `ErtsEngine`,
moves lifecycle/handle-registry state into it, and gives scheduler data an explicit
engine association at construction. Uninitialized control objects can be freed;
initialized objects cannot yet be reclaimed. M1 is not complete: other preparation
allocations remain global. The ownership ledger separates engine infrastructure
from the private namespaces to extract next—not everything moves into Engine.

- Integrate one actual emulator archive. **Do not call whole-world startup from
  Engine creation**, or boot a temporary OTP world inside the Engine. The old
  returning-startup probe is diagnostic evidence only, not the implementation path.
- Separate shared native infrastructure from world-specific tables. The current
  unbooted diagnostic still initializes global tables; move private namespace
  construction to isolate contexts as M2 proceeds.
- Make unbooted initialization owned, typed and recoverable. Do not make destruction
  of the old running OTP world a prerequisite for this work.
- Activate shared execution workers explicitly without implicit `init`, Kernel,
  code-server, logging or application bootstrap. Add readiness/error reporting and
  coordinated stop/wakeup/join ownership for those workers.
- Establish ownership during error unwinding **before** returning a real Engine:
  the example will immediately encounter another unfinished operation and unwind.
  Never retain the scaffold's empty destructor around a live emulator, silently
  abandon running threads, force-free reachable state, or terminate the host.
- Explicit shutdown must report incomplete cleanup honestly. If cleanup cannot
  finish, retain safe ownership; finish the destructor/coordinator policy before
  treating this API as usable. Engine restart/unload is not needed for this proof.

Starting points: `erl_engine.c/.h`, `erl_init.c`, `erl_embed.h`, `erl_process.c`, `erl_async.c`, Unix
`sys.c`, poll and thread-progress machinery. ERTS changes begin **here**, not after
all the C++ methods have been filled in.

Gate: real same-PID execution infrastructure, zero-isolate create/shutdown returns,
thread/resource census, initialization-failure injection, and a host that continues
executing after cleanup. No `_Exit` success path. Expected next frontier:
`Engine::create_isolate`.

### M2 — Fresh owned world contexts

**API:** `Engine::create_isolate` and the ownership beneath Isolate handles.

- Introduce a real `ErtsIsolate`/code-space context in ERTS: engine affinity,
  generation, lifecycle state, private table roots and retained ownership for
  asynchronous users. Start extracting the atom/module/export/code-index state
  with M3; completing every engine-global migration is not a prerequisite.
- Establish an immutable owner on processes before publication; spawn inherits it.
  Scheduler, dirty/async work and deferred callbacks carry the correct context.
- Distinguish shared engine facilities from isolate state. Do not create a new
  scheduler fleet, helper VM, independently loaded emulator, or pooled tenant world.
- Make partial world construction safely reclaimable. A C++ ID/map alone is not
  enough to return a successful `create_isolate` result.

Starting points: process creation/publication in `erl_process.c/.h`, process tables,
allocation ownership and thread progress. Define the private table roots needed
by M3/M5 together rather than duplicating the current global OTP world.

Gate: two fresh contexts coexist with independent owned roots; construction failure
rolls back; handles cannot cross engines or revive retired generations. Expected
next frontier: `Isolate::load_module`.

### M3 — Private BEAM loading, atoms and code resolution

**API:** `Isolate::load_module`.

- Consume/copy host-supplied BEAM bytes, validate module identity and the supported
  profile, stage publication and roll back failures. Reject `on_load` before effects.
- Make atoms, module/export/import/fun lookup, active code indices, literal/code
  ownership and release context-aware. Immutable predefined atoms/core code may be
  shared only where representation and effects are genuinely context-independent.
- Keep both `probe` versions resident. No renaming, loading one over the other, or
  rewriting a single global table between turns. Contextual lookup into private
  resident tables is valid; global table swapping is not.
- Track code references held by closures, processes, collectors and pending work
  now; otherwise later reclamation cannot be made safe.

Starting points: `atom.c`, `module.c`, `export.c`, `code_ix.c/.h`, `erl_fun.c`,
`beam_bif_load.c`, interpreter loader/dispatch. Start with the debug interpreter;
JIT correctness and performance remain separate work, not implied acceptance.

Gate: distinct resident same-name code identities, host byte buffers can be freed
before execution, rejected loads leave no partial publication. Expected next
frontier: `Isolate::start`. **This is a major emulator change, not a wrapper task.**

### M4 — Host-driven execution and correlated binary completions

**API:** `Isolate::start`, `Isolate::call`, `Call::wait_until`.

- Create an ordinary isolate-owned invocation process for the requested MFA/1.
  Bootstrap from supplied code, without requiring a full OTP node per isolate.
- Convert copied host bytes to guest binaries and transfer results to host-owned
  buffers. Do not expose raw terms, PIDs or native pointers as host capabilities.
- Implement admission, correlated completion and error mapping. Enforce 64 KiB
  payload/result, 64 outstanding calls and 1 MiB queued payload bounds per isolate,
  with reserved terminal-status capacity. These are not heap or CPU budgets.
- Implement control-thread waits without holding VM locks or blocking schedulers.
  Timeout is not cancellation. Accepted work gets exactly one terminal outcome;
  abandoned handles must not leave dangling callbacks or unowned native work.
- Returning from `boot/1` does not stop its unlinked server process.

Starting points: initial process construction, BIF/export invocation, process exit,
`erl_message.c`, `erl_proc_sig_queue.c`, binary allocation and host completion queues.

Gate: a simple binary entry point runs in the host PID; two admitted calls complete
independently; exceptions, limits, timeout/retry and discarded handles are tested.
The full fixture will expose M5 if the registry or process services are still global.

### M5 — The example's local services and independent persistent state

- Make `probe_state` registration/lookup and process/message/monitor identities
  isolate-local. Normal spawn/monitor/send/receive must preserve owner boundaries.
- Account for receive-timeout timers and deferred signals during cancellation and
  cleanup. Sharing a physical clock or scheduler is not sharing mutable namespaces.
- Remove legacy bootstrap/services only where they block this execution path.
  Do not spend this milestone implementing every OTP service or preserving a node.

Starting points: `register.c`, process/signal/message tables and `erl_hl_timer.c`.

Gate: the unchanged A/B starts and counter assertions pass simultaneously. A reaches
2 while B remains 1. Both persistent servers remain live after boot invocations
finish. Expected next frontier: `Isolate::stop`.

M2–M5 are coupled: code namespaces, process ownership and local services need a
coherent design. The listed frontiers are useful diagnostics, not a claim that
these subsystems can be implemented in isolation from one another.

### M6 — Stop, physical reclamation and fresh replacement

**API:** `Isolate::stop`, `Reclamation::wait_until`, closed-handle behavior.

- Close admission synchronously; stop every owned process, including the server
  that outlived `boot/1`. Drain/detach messages, monitors, timers, native work,
  completions and code references through safe points.
- Separate logical stop from physical release. Never report success solely because
  a root PID exited, a flag changed, or the host deadline expired.
- Preserve a small closed tombstone while releasing the application world. A stale
  A handle must still reject calls after replacement creation and internal ID reuse.
- Keep B running, with its original state/code, while A drains and after A is gone.

Gate: allocator/resource accounting reaches the isolate release baseline, B's
requests still succeed, replacement starts at zero, and host-owned output survives
reclamation. Add failed-bootstrap/partial-load cleanup and repeated churn tests.
A public success token without independent resource measurements is insufficient.

### M7 — Complete example and harden the witness

- All assertions pass, then all handles are released and real engine shutdown
  returns. Only then may the example print success and exit zero.
- Add an external watchdog, durable provenance, native resource/thread observations,
  repeated runs and the RFD's 1,000-cycle churn gate.
- Expand beyond this first behavioral slice: atom visibility, ETS/persistent terms,
  timers, resource/native boundaries, OOM/failure injection and sanitizers.

No checkpoint above automatically clears untrusted execution, latency, resident
density, budgets or suspend/resume gates. The first workload remains trusted.

## Current next action

Work on ownership and cleanup of M1's unbooted substrate, while moving M2/M3's
world-specific namespace initialization out of shared preparation. Engine execution
workers must be explicitly owned and activated without booting an OTP world. Do not advance the example by
returning a default Engine, empty Isolate, canned reply or successful no-op teardown.
Keep the existing native startup probe as diagnostic evidence until the new API
can meet its actual ownership contract.
