<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 Leandro Ostera <leandro@ostera.io> -->

# Native API linking and retained scheduler thread handles

Historical checkpoint at `4141efd3`. Its proposal to tackle running-world stop/join
first is superseded by [unbooted preparation](0002-unbooted-preparation.md): Engine
initialization must not reuse OTP-world startup, nor wait for that world's teardown.
The evidence and factory diagnostic below describe this earlier checkpoint.

This is a **partial M1** implementation from the
[example-driven plan](0002-example-implementation-plan.md), not a working Engine
factory, engine destruction, or an isolate milestone.

## What changed

- CMake can link the experimental C++ library and `two_isolates` against one actual
  debug-interpreter ERTS archive. The optional `LIBBEAM_ERTS_PACKAGE` selects a build
  artifact, not an embedded/standalone runtime mode. Without it, the old pure C++
  scaffold remains available for quick build tests; neither build simulates success.
- `export_runtime_package.py` consumes a passing native startup probe. It validates
  the header, archive, recorded link settings and static dependencies, copies those
  inputs, and generates a CMake import with configure-time SHA-256 checks. Changing
  a packaged archive fails configuration. Unsupported/unresolved dependencies fail
  export instead of being guessed. System libraries/frameworks and the toolchain
  remain external. This is an absolute-path local snapshot, not a relocatable or
  hermetic SDK distribution.
- `erts_start_schedulers` now creates its normal/dirty schedulers, auxiliary/poll
  threads and optional run-queue supervisor **joinable**, retaining every successful
  creation's ID, role and detached setting. Auxiliary/poll IDs were previously
  discarded. The registry has a dedicated `RUNTIME_THREAD_HANDLES` allocator type.
- `erl_scheduler_thread_inventory` exposes a copied diagnostic census, before or
  after startup on the single host control thread. It excludes async workers,
  which already retain their own joinable handles in `erl_async.c`. It is not a
  census of every native thread or a physical-join measurement.
- The native C++ factory queries that real census. It rejects an already-started
  raw runtime rather than adopting it. Otherwise it reports:

  ```text
  not implemented: Engine::create (ERTS linked; cooperative stop/join still required)
  ```

  **It does not call `erl_start_embedded`, return an empty successful Engine, or
  unwind live threads through the scaffold's default destructor.**

## Errors that drove the slice

The first native compile failed with:

```text
beam/erl_process.c:8962:41: error: use of undeclared identifier 'ERTS_ALC_T_SCHED_DATA'
```

Instead of borrowing an unrelated allocation category, the thread-handle registry
received its own generated allocator type. That build then passed.

The initial package unit test exposed macOS `/var` versus `/private/var` fixture
paths. The fixture was fixed to use canonical paths, matching the native runner's
provenance. Hash validation was not weakened.

The runtime frontier remains `Engine::create`; linking is not startup, retaining
joinable handles is not stopping threads, and an expected-error test is not the
full two-isolate proof passing.

## Evidence

All final native builds/tests/package exports were serialized with the shared
user-wide OTP validation lock. Platform: macOS ARM64, AppleClang 17, debug interpreter,
incrementally configured source snapshot. No clean-bootstrap/JIT/platform matrix
or latency/security acceptance is claimed.

- `/tmp/libbeam-thread-ownership-first/`: preserved failing native compile.
- `/tmp/libbeam-thread-ownership-final/summary.json`: three native C++ host trials
  execute actual bytecode in their host PID; each reports six retained handles
  created joinable. Existing no-child, deleted-service, signal and four PTY cases
  continue to pass. Status remains `started_and_returned_not_shutdown`.
- `/tmp/libbeam-thread-ownership-final-package/manifest.json`: frozen native link
  inputs and their hashes, generated from that passing probe.
- `/tmp/libbeam-thread-ownership-checkpoint/summary.json`: both scaffold and native
  CMake builds and their CTests pass. The actual example reads A/B fixture bytes
  and exits 1 at the expected factory error. Three additional CMake-linked native
  host trials check C++ factory rejection both before startup (`not_implemented`)
  and after raw startup (`invalid_state`), then actual same-PID execution. **29
  tooling tests pass.** Development native source hashes match the tested archive's
  recorded inputs. The earlier non-final CMake run's failed unit test is
  preserved separately in `/tmp/libbeam-thread-ownership-cmake/unit.log`.
- `/tmp/libbeam-thread-ownership-extra/summary.json`: an additional native trial
  enables the run-queue supervisor and two poll threads (`-sfwi 1 -IOt 2`), observes
  eight retained joinable handles, and continues executing bytecode. A separately
  exported, deliberately modified package is rejected by CMake's hash check.

The raw startup diagnostics still end with an explicit process exit. No scheduler
thread has been demonstrated stopping/joining in these tests, and no application
world or engine state is physically reclaimed.

## Repeatable build entry points

Use a newly produced startup report; older reports without native dependency/link
setting hashes are deliberately rejected. The startup runner and exporter acquire
the shared lock themselves. Serialize subsequent native CMake builds/tests with it
as well; do not edit active inputs. Choose fresh external output directories.

```sh
python3 -B libbeam/tools/run_engine_start_probe.py \
  --root /path/to/configured/snapshot/beam --output /tmp/native-probe-new
python3 -B libbeam/tools/export_runtime_package.py \
  --probe /tmp/native-probe-new/summary.json --output /tmp/native-package-new
cmake -S libbeam -B /tmp/native-api-build-new \
  -DLIBBEAM_ERTS_PACKAGE=/tmp/native-package-new/runtime-package.cmake \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/native-api-build-new
ctest --test-dir /tmp/native-api-build-new --output-on-failure
/tmp/native-api-build-new/two_isolates /path/to/a/probe.beam /path/to/b/probe.beam
# Expected exit 1: ERTS linked; cooperative stop/join still required.
```

The CMake `engine_start_probe` target is a separate, process-lifetime diagnostic;
it requires the private control-FD harness in `run_engine_start_probe.py`, not a
normal direct invocation. The full example's success assertions remain unchanged.

## Next actual runtime work

`erl_process.c` now holds the IDs needed for joins, but still deliberately treats
normal/dirty scheduler return as a fatal error. Auxiliary/poll loops have no stop
protocol. Managed thread-progress registration has no ordinary unregister path
(the unmanaged side does). These are the next lifecycle dependencies to resolve:

1. Define coordinated admission close, quiescence, wakeup and termination
   acknowledgement for scheduler/auxiliary/poll/supervision loops.
2. Preserve thread-progress, allocator/TLS and callback ownership until all users
   are quiescent. Include existing async workers; do not confuse the new registry
   with complete engine ownership.
3. Join before releasing stacks, queues and runtime state. Retain safe ownership
   on timeout; add partial-initialization failure cleanup.
4. Only then let the public factory return a real Engine and advance the example
   to `create_isolate`. Continue the private-world/code namespace slice from the
   plan, without turning this into a general OTP compatibility cleanup.

Untrusted workloads remain no-go.
