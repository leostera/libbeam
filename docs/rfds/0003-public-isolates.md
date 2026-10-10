<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 Leandro Ostera <leandro@ostera.io> -->

# Public Isolates and copied calls over the selected BEAM profile

Status: **M2 selected-profile public execution implemented; stateful M3/M4 remain outstanding.**

This follows [owned worker execution](0003-owned-executor.md), baseline `4e206202`,
under [RFD 0003](0003-additive-runtime-construction.md). The public C++ API now
creates real private worlds, loads ordinary BEAM, admits binary calls, waits for
worker execution, and reclaims code/atoms/heaps while preserving closed handles.
Neither acceptance example was changed. `engine_lifecycle` continues to pass;
`two_isolates` now reaches loading and refuses its unsupported stateful profile.
No PID/spawn/mailbox/monitor/registry/timer machinery is manufactured by this slice.

## One ownership and execution path

`core/world.c/.h`, `world_internal.h` supply original C coordination around the
existing code space, atom namespace, process constructor, generated interpreter,
collector and joinable worker. They are not another VM or a C++ ownership graph.
Existing transplants remain covered by the loader/execution/binary manifests;
this slice adds no opcode, BIF implementation, term representation or language
translation. The worker's new completion path is an explicit owned C runtime
operation, not an arbitrary host callback.

Worker-side orphan completion and code-space retirement now mutate the parent
ownership graph. Unlike the earlier raw-task-only checkpoint, shutdown and final-
detachment checks must hold the Engine mutex; owner drop participates in the same
serialization. The final host leave decides detachment before unlock, then joins
and disposes outside the lock. A worker never decides final Engine disposal.

The Engine retains a list of world controls. Each world owns a private code space
and its accepted calls. Calls and reclamation handles retain the world control;
worlds/tasks retain the Engine. The C++ adapter owns only C handles and host-thread
affinity metadata. Host methods reject another control thread; handle destruction,
like other management, remains a control-thread requirement.

World phases are loading, running, stopping and reclaimed. `start` requires loading
and changes phase only after the copied process/task and terminal control are
fully constructed. `call` requires running. Loading after start, repeated start,
and admission after stop refuse explicitly. Unknown arity-one MFAs fail admission
without interning new names. Names resolve to owned namespace identities; no caller
string or input buffer is retained. Existing all-function/eager-import admission
still applies to whole modules, including compiler-generated module-info functions.

Guest code runs only on the Engine worker. Polling and the C++ bounded sleep loop
never execute BEAM on the host. All loader, scheduler, completion and GC accesses
share Engine serialization. The worker completes a call, copies its byte result,
and destroys its invocation process before exposing the terminal status. Thus a
ready host completion does not retain executable frames, literals or guest heaps.
The internal raw-task API continues retaining its process for its borrowed-term
consumers; both forms use the same process constructor and interpreter.

## Transport and completion

- Input and successful result are limited to **64 KiB** each. A normal guest return
  that is not a byte-length bitstring is an unsupported transport result, not a
  fabricated Erlang exception. Oversized results produce a terminal limit error,
  never truncation. Byte-length results with a bit offset are copied correctly.
- At most **64 unconsumed/outstanding calls** and **1 MiB queued payload capacity**
  per world. Terminal controls are separately allocated before admission.
- Admission conservatively reserves `input size + 64 KiB` of that capacity. This
  covers copied input and maximum output simultaneously, so completion cannot
  discover an unreserved full output queue. Consequently the queue can refuse
  before 64 calls: at most 16 zero-input or eight maximum-input calls can be
  simultaneously pending under this policy. These are upper bounds, not promised
  throughput or heap quotas.
- On completion, the reservation becomes actual result bytes. Empty completions
  consume no payload capacity but still occupy a call slot until consumed/dropped;
  all 64 slots are exercised by tests. VM heaps and finite handle bookkeeping are
  not mislabeled as transport payload quotas.
- Output allocation failure publishes a reserved terminal error. Exceptions and
  cancellation also need no payload allocation. Every accepted call has one
  terminal status even on these paths.
- A deadline timeout does not cancel or consume the call. Waiting again is valid.
  Terminal success/error is consumed once; subsequent waits return closed.
- C++ success copies into a host-owned `Bytes` vector before consuming the C
  completion. If that host allocation fails, the typed limit error leaves the
  ready C completion untouched for retry. Error diagnostics are also constructed
  before consumption; diagnostic OOM falls back to an empty typed error.
- Dropping a pending call handle does not cancel accepted guest work. Its runtime
  control remains owned until completion or world stop, when an orphan completion
  is discarded without allocating a host result. Dropping a completed handle
  releases its payload and slot directly.

## Stop, reclamation and exceptional destruction

`stop` allocates its reclamation control before changing admission. Allocation
failure leaves the running world unchanged. Successful stop closes admission
under the Engine mutex, obtains an interpreter safepoint, and cancels pending
invocations without overwriting already-terminal results. The worker then retires
one module per stopping world per round, interleaving peer execution. With no
worker ever admitted, a never-started world can reclaim immediately.

The selected eager loader imports only already-published modules or self; newest-
first retirement therefore reverses the dependency order. Self imports do not
retain the module. This is not a solution for future mutually recursive batches,
hot loading, closures, signals, timers or unrelated child processes: those need
additional retaining users and explicit drain paths before admission.

Reclamation success follows actual module/literal/atom/code-space destruction.
Only closed world control and independently owned host completions/handles may
remain. The memory-accounting test keeps a peer loaded, stops a world and observes
**exactly one world-control allocation** above the peer baseline; all of that
world's VM allocations are already gone before its Isolate handle is dropped.
The final handle drop returns exactly to the peer baseline.

An Isolate destructor closes/cancels and drains this admitted world synchronously,
under the same safepoint protection, rather than stranding an orphan cleanup job.
It does not run guest code on the host. Surviving call/reclamation handles remain
valid closed/terminal controls. Engine-owner drop does not invalidate retained
worlds; they can finish supported work. Engine shutdown remains busy until worlds,
tasks and handles have released their ownership. Final disposal happens on host
control after unlock, then joins the worker; the worker never joins itself.

These cleanup guarantees cover the resources actually admitted here. They are
not evidence that future spawned processes, mailbox fragments, monitor signals,
timers or native continuations already have a world-wide retirement protocol.

## Validation

Initial C world validation: `/tmp/libbeam-world-first/summary.json`.
First public integration: `/tmp/libbeam-public-world-first/summary.json`.
Expanded physical-release/capacity coverage:
`/tmp/libbeam-public-world-expanded/summary.json`.
Final candidate: `/tmp/libbeam-public-world-commit-reviewed/summary.json`.
Tooling: `/tmp/libbeam-public-world-tooling/summary.json`.
The retained `/tmp/libbeam-public-world-ownership-reviewed/build.log` records a
missing internal executor-header declaration during the ownership-lock audit;
that include was corrected before the final full rerun.

- Eleven Debug/Release CTests plus actual C and C++ fixture execution in each build.
- **1,160 world-construction allocation-failure prefixes**, each followed by retry;
  **six host call-admission allocation failures**, exact rollback to loading and
  successful retry. The earlier native worker/init failure tests remain active.
- Worker output-copy OOM, stop-control allocation failure, C++ adapter allocation
  failures and host-vector copy failure with retry.
- Real native module-info/GC and exception paths, inherited forced-GC/code/binary
  regressions, 64 outstanding completions, the 1-MiB reservation boundary, and
  copied input/results. A separately generated ordinary BEAM fixture returns
  65,537 bytes to test result-limit refusal; its stock-OTP result is a reference,
  not libbeam acceptance.
- Public calls progress beside an infinite BEAM loop; repeated timeout does not
  cancel it. Stop returns cancellation, reclamation physically retires its world,
  and peers/replacements continue. The public test repeats 32 fresh worlds.
- Closed/moved handles, wrong-control-thread refusal, unconsumed results after
  reclamation, out-of-order parent destruction, and host-owned replies surviving
  Engine shutdown. No test uses host answer substitution or a helper VM.
- C world and C++ API execution run under UBSan, with locked fresh builds, source
  hashes and retained logs. Generated fixture BEAMs remain outside source control.
- Unchanged stateful acceptance still exits 1: `unsupported BEAM profile or import`.
  M3/M4 are not complete. No ASan/TSan, Linux, security, quota, performance, full
  Erlang/Elixir compatibility or pinned-OTP-30-compiler claim is made here.

Next: admit the missing process/language families required by the unchanged stateful
fixture, preserving this public ownership/transport boundary and its failure tests.
The Midgard pure-Elixir application experiment is scheduled after M0–M4, not used
as a substitute for them.
