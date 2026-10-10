<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 Leandro Ostera <leandro@ostera.io> -->

# Engine-owned asynchronous execution

Status: **Internal worker/execution slice; public Isolate/Call APIs still refused.**

This follows [G1 Engine ownership](0003-engine-lifecycle.md), baseline `dffd1206`,
under [RFD 0003](0003-additive-runtime-construction.md). It supplies an execution-led
part of B01/B11, not a completed Erlang scheduler or stateful-Isolate acceptance.
The public contract requires guest execution off the host thread; a host wait loop
must not quietly become the interpreter. The existing C process/loader/GC machinery
now also runs on one lazily created, joinable Engine-owned POSIX worker.

## Actual execution and ownership

`libbeam/core/executor.c/.h` add C coordination around the existing
`lb_process_create_binary` and generated `lb_process_run`. They do not introduce
another bytecode evaluator, helper VM, implicit world, tenant TLS or a C++ runtime.
The new coordination is original ownership code; admitted BEAM algorithms and their
provenance remain in the existing loader/execution/binary manifests.

Engine creation initializes a mutex and condition variable but starts **no worker**.
The first successful task admission copies input into a real process, creates the
shared worker, and publishes the task only after all fallible operations succeed.
Subsequent tasks in any code space of that Engine reuse the same worker. Entry
provenance and arity-one/binary-size boundaries are checked before allocation.

The worker visits pending tasks round-robin and executes at most 1,024 dispatches
and 128 reductions per step. These are existing interpreter safepoints, not CPU
budgets. It drops the Engine mutex and yields between steps, allowing host control
to acquire it. Host polling only reads status; it never executes guest code.
No latency, hard fairness, parallel execution or expensive-BIF preemption claim is
made from these bounds.

The Engine mutex serializes the actual loader/allocation/heap/native consumers.
Raw C code, heap and domain operations on an Engine with a worker require
`lb_engine_enter/leave`; task operations acquire this protection themselves.
These are internal, non-reentrant APIs. Public control remains single-host-thread;
this is not permission to use raw domains concurrently. Internal trusted allocator
hooks may run on the worker under this serialization and must not reenter it.
The public C++ API exposes no arbitrary callback/allocator registration.

Each task owns its process through physical release. A completed process retains
its code, roots and binary payloads; terminal term views are borrowed until task
release. Pending cancellation acquires the Engine mutex after a dispatch safepoint,
destroys the process, and records cancellation. Cancellation cannot replace an
already-terminal result. Releasing a task also safely retires a pending process.
These internal semantics are not yet the public once-only binary-completion API.

A task control itself retains the Engine even after its process is cancelled and
its code space is destroyed. Engine shutdown refuses live spaces, tasks or a host
control borrow. Dropping the Engine owner leaves retained tasks usable. The last
physical child/control release requests worker stop, wakes it, **joins it**, then
destroys the condition variable, mutex, catalog and allocation domain. A host
control borrow defers final disposal until after unlock, avoiding self-deadlock
when the final code space is retired inside a protected raw-C operation. The
worker never destroys or joins itself.

## Fallibility and native surface

- The existing three Engine allocation stages still unwind exactly.
- Mutex initialization failure publishes nothing; condition initialization
  failure destroys the completed mutex stage before allocation cleanup.
- Task/control/heap/binary allocation failure leaves no task or process published.
- `pthread_create` failure destroys the unpublished process/task and permits retry
  on the same Engine. There is no successfully allocated empty task.
- Join/destroy failures on correctly owned primitives are internal invariant
  violations, not recoverable initialization or guest errors. No failure path
  treats an unjoined thread as successful reclamation.
- The explicit admitted POSIX surface is mutex init/destroy/lock/unlock, condition
  init/destroy/signal/wait, thread create/join and `sched_yield`. The runner still
  refuses legacy `erts_`/`ethr_` dependencies and checks the exact pthread symbol set.
  Other platforms need an explicit backend admission; CMake currently requires POSIX.

## Evidence and remaining work

The fresh locked first successful run is
`/tmp/libbeam-executor-native-fixed/summary.json`. It includes ten Debug/Release
CTests, unchanged G1 acceptance, worker fixture execution in Debug/Release/UBSan,
all prior code/binary execution and physical-retirement tests, and reproducible
generation. Final candidate: `/tmp/libbeam-executor-final-reviewed/summary.json`.
Tooling evidence: `/tmp/libbeam-executor-tooling/summary.json` (29 tests).

`core_executor_test.c` interposes only selected POSIX lifecycle functions for
failure injection/counting. Every successful operation delegates to the real
system function through `dlsym`; no fake thread or execution result is used.
Tests cover:

- Mutex and condition initialization failure, exact reverse cleanup and retry.
- Real thread-start failure after copied-process construction and successful retry.
- All four task allocation-failure prefixes, successful retry and exact balance.
- A permanently yielding BEAM loop beside a completing peer in another code space,
  using one worker while the host only polls/sleeps.
- Caller input mutation after admission; real native module-info allocation/GC
  observed on a thread different from the host; actual Erlang exception completion.
- Worker-side allocation failure and retry, wrong-Engine entry refusal, cancellation
  without overwriting a terminal result, actual task retention after entry release,
  Engine-owner drop, and both final-space and retained-cancelled-task teardown orders.
- Physical primitive/thread counts and allocation counts return to zero after join.

The initial build failure at `/tmp/libbeam-executor-first/build.log` exposed the
Darwin feature macro needed by the test's `RTLD_NEXT`; it was fixed in the test,
not worked around with a mock backend. ThreadSanitizer was attempted separately:
`/tmp/libbeam-executor-tsan-control/summary.json` records an empty-main control
exiting with signal 11. There is **no TSan/race-detector pass** for this host.
ASan and Linux/platform evidence remain separate outstanding gates.

No PID directory, spawn/exit/signals, mailbox, timer, registry or closure machinery
was added. Tasks are real selected-profile process contexts, not the full Erlang
process subsystem. There is still no public Isolate, asynchronous binary transport,
64-call/1-MiB capacity protocol, once-only host completion or world reclamation
handle. Neither a worker nor a successful internal task establishes G3. Next,
integrate those C-owned world/admission/completion lifetimes with this worker and
continue the missing BEAM/process families required by the unchanged example.
