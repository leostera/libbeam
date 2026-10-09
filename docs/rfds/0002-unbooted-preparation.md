<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 Leandro Ostera <leandro@ostera.io> -->

# Native preparation without booting an OTP world

This corrects the earlier M1 direction: the Engine must **not** reuse whole-world
startup or require that old OTP world to become destructible first.

## Actual control flow now

```text
experimental erl_prepare_runtime
    -> parse diagnostic arguments / initialize substrate and global tables
    -> return PREPARED

legacy CLI / process-lifetime erl_start_embedded diagnostic
    -> the same preparation
    -> explicitly load preloaded BEAM code and create init/system processes
    -> explicitly launch message dispatcher, async workers and schedulers
```

Preparation does not call `start_otp_world` or `start_runtime_threads`. The old
`erl_start_common` coupling is removed. There is no embedded/standalone mode flag;
these are initialization phases with different explicit callers. The old boot path
remains a diagnostic/compiler-tool host, not the Engine constructor.

The experimental native interface is in `erl_embed.h`. Its preparation entry still
uses the legacy argument parser, global allocations and fatal initialization-error
behavior. It is **not** a finished typed initializer, private isolate namespace,
recoverable startup, or owned/reclaimable Engine. Global atom/code/registry and
other tables still need ownership separation. Predefined atoms and native BIF
metadata exist; the zero-code assertion means no loaded BEAM, not no metadata.
No application world is booted.

`Engine::create` remains guarded rather than returning an unsafe handle. Its native
error is now:

```text
not implemented: Engine::create (ERTS linked; unbooted initialization cleanup still required)
```

Its preflight checks the native initialization phase, not merely scheduler count:
prepared native state is already claimed even when no threads have started. A
second prepare or attempted whole-world startup is rejected without doing work.

## What the runtime failures exposed

1. `erts_init_async()` was not just metadata initialization: it launched workers.
   It is now `erts_start_async_workers()`, called only in the thread-launch phase.
   Old declarations/call sites are removed, not kept as compatibility stubs.
2. The first prepare-only tests still failed with exit 14. The independent OS
   observer saw **one thread before preparation and two afterwards**, despite zero
   scheduler handles, zero processes and zero loaded BEAM code.
3. `erts_init_trace()` was starting `erts_smsg_disp`. Queue initialization is now
   separate from `erts_start_sys_msg_dispatcher()`. That worker starts explicitly
   during launch, is joinable, and retains its existing native thread ID. This does
   not implement its eventual stop/join protocol.

An enhanced observer was linked against the preserved pre-fix archive and correctly
reproduced the extra-thread failure. We did not weaken the thread-count assertion.

## New native witness

CMake's `engine_prepare_probe` uses the real ERTS library. The native CTest cases
request async pools of 0, 1 and 4, while deliberately specifying a nonexistent
initial module, root and boot script. Those boot settings must never be reached.
Each case checks:

- Native lifecycle progresses from UNCLAIMED to PREPARED.
- Actual native process and port tables both have zero entries.
- Loaded BEAM code size is zero; no init PID or housekeeping process roots exist.
- No scheduler-family handles were created.
- OS thread identities, not just the scheduler inventory, are unchanged.
- The host continues executing, and a second prepare/start is rejected.
- The public C++ factory rejects the already-claimed, threadless native state.

The snapshot checks are not syscall tracing or proof against every transient OS
side effect. Existing native initialization still has globals, environment/argument
handling and other host-effect limitations. No untrusted-code approval is implied.

The executable explicitly ends the process after recording these facts. Native
allocations are not yet reclaimed: **process exit is not Engine destruction**.

## Evidence

Final validation was serialized under the shared user-wide lock; development native
source hashes match the tested configured snapshot. macOS ARM64 debug interpreter,
incremental configured-source build only. The Linux observer is written but untested;
other platform/time-source initialization paths are not certified threadless.

- `/tmp/libbeam-unbooted-native-first/summary.json`: booted diagnostics still pass
  after moving async launch. This archive retains the hidden message worker.
- `/tmp/libbeam-unbooted-cmake-first/`: all three initial preparation tests fail.
- `/tmp/libbeam-unbooted-observer-regression/summary.json`: the preserved first
  archive reproduces `os_threads_before=1 os_threads_after=2`, exit 14.
- `/tmp/libbeam-unbooted-native-final/summary.json`: three legacy same-PID bytecode
  startup trials and four PTY cases pass after deferring both worker groups.
- `/tmp/libbeam-unbooted-package-final/manifest.json`: hash-checked native inputs.
- `/tmp/libbeam-unbooted-checkpoint/summary.json`: all three new unbooted cases pass
  with unchanged OS threads. Native CMake's four CTests, scaffold CMake's one CTest,
  three additional CMake-linked legacy host trials, and 29 tooling tests pass.
  The example still exits 1 at the explicit public-factory frontier.

To reproduce, export a package from a fresh passing native probe, then use:

```sh
cmake -S libbeam -B /tmp/unbooted-api-new \
  -DLIBBEAM_ERTS_PACKAGE=/path/to/runtime-package.cmake -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/unbooted-api-new
ctest --test-dir /tmp/unbooted-api-new -V
```

Use the shared lock for native build/test operations; see the
[native-package instructions](0002-native-api-link.md). Unlike `engine_start_probe`,
the preparation probe needs no private control FD or running Erlang fixture.

## Next

Give the unbooted substrate real ownership and cleanup, with typed configuration
and recoverable initialization. Move world-specific table construction toward
isolate-owned contexts rather than blessing the current globals as isolation.
When shared execution threads are activated, give them explicit wake/stop/join
ownership; do not introduce an implicit OTP bootstrap to make them run.

The two-isolate example remains the driver. Finishing whole-OTP-world destruction
is **not** a prerequisite. No isolates or engine reclamation are claimed yet.
