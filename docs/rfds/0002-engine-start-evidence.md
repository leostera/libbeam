<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 Leandro Ostera <leandro@ostera.io> -->

# RFD 0002: real emulator startup returning to a C++ host

**Historical first-start checkpoint (`5a6f348b`).** The forker/BINDIR dependency
and SIGCHLD takeover described here have since been removed from embedded startup;
see [the runtime-reduction record](0002-minimal-runtime.md). The original evidence
is retained, not a description of every current requirement.

## Implemented boundary

The POSIX-only experimental native entry
[`erl_start_embedded`](../../beam/erts/emulator/beam/erl_embed.h) initializes the
**actual linked OTP emulator**, creates its global OTP world, launches its runtime
threads, publishes signal initialization and returns to C++. It is not an object
that merely records configuration, a helper Erlang VM, or a toy bytecode interpreter.

The conventional `erl_start` frontend and experimental entry share `erl_start_common`.
Standalone startup still enters its main-thread wait loop. On Unix, the initialization
previously at the front of that loop is extracted into `prepare_main_thread`; the
returning entry invokes it without entering the loop. Omitting the entire old call
would have omitted required signal publication, not just a blocking wait.

Only one startup attempt is supported, on one host main/control thread. A sequential
second call is rejected before reinitialization. This is **not a thread-safe concurrent
creation API**. The caller must retain mutable CLI argument storage for process life.
The return acknowledges native startup/thread launch, **not completion of asynchronous
OTP boot or application work**.

## Actual witness

[`engine_start_probe.cpp`](../../libbeam/examples/engine_start_probe.cpp) directly calls
the new native entry, verifies second-start rejection, reports return to host code,
and waits on a private host control FD. The trusted
[Erlang fixture](../../libbeam/tests/fixtures/engine_start_probe.erl) exercises OTP init,
six housekeeping processes and 20 spawn/monitor cycles, then reports `os:getpid()`.

The bounded [driver](../../libbeam/tools/run_engine_start_probe.py) waits for exactly
one host marker and one bytecode marker naming the **same host PID** before releasing
the host control FD. No guessed startup sleep, fake ready response or `erl` subprocess
is used to execute this host workload. Compiler/build helpers run separately before
the native host experiment.

Three fresh native-host process trials pass on ARM64 macOS with the debug interpreter:

```text
HOST_STARTUP_RETURNED pid=<host> second_start=rejected
BEAM_STARTUP_OK pid=<same host>
HOST_CONTROL_OK engine_shutdown=false isolates_created=0 process_exit=true
```

The first two lines may arrive in either order because schedulers execute independently
of the host control thread. These are **three process-lifetime bring-up trials**, not
three engine create/destroy cycles or three isolates. The C++ host explicitly uses
`_Exit(0)` to end the experiment; that is not engine teardown or reclamation evidence.

## Important limitations and retained failure

The first trial exited during native startup: `Environment variable BINDIR is not set`.
Direct linking bypasses `erlexec`, which normally supplies that value. The corrected
driver provides the configured absolute emulator directory. The failed output remains
at `/tmp/libbeam-engine-start/first` rather than being discarded.

This exposed an existing dependency in `sys/unix/sys_drivers.c:forker_start`:
ordinary OTP startup creates a native `erl_child_setup` helper for port support.
That support is retained here. **No helper BEAM VM** does not mean **no native child
processes**, nor does it establish a reduced bytecode-only profile.

Other restrictions remain explicit in the native header:

- Process-wide signals and alternate signal stack are claimed, not restored.
- Startup/CLI errors, boot failure, `halt`, system-process termination and other fatal
  paths can terminate the C++ host. There is no recoverable creation/unwind contract.
- Runtime threads remain detached/live. There is no engine shutdown, restart, unload,
  destructor or safe physical reclamation.
- Darwin wx/Cocoa main-thread callbacks are unsupported without the standalone loop.
- The entry is POSIX-only; native-host execution is tested only on macOS ARM64 debug
  interpreter. Windows and hosted cross-platform acceptance are not claimed.
- Atoms, modules, exports and services still describe one global world. No isolate
  creation API, private namespaces, budgets or closed native-effect profile exists.

This is trusted-fixture bring-up, not a supported public C++ `Engine`/`Isolate` API,
not P0-03 completion, and not authorization for untrusted workloads.

## Evidence and checks

The configured snapshot worktree was based on `87d63923`; the new native changes
were applied only after its baseline and archive-link runs finished. Reports record
the tracked diff plus individual hashes for the new header, emulator sources, helper,
C++ host, fixtures and linked archive. No source input was changed during its build.

| Summary | Result | SHA-256 |
| --- | --- | --- |
| `/tmp/libbeam-engine-start/bindir-fixed/summary.json` | `started_and_returned_not_shutdown` | `85c03f9e52ee6459c427304a6abd920218f54e671d6a04588825f68315d0c8b2` |
| `/tmp/libbeam-engine-start/regressions/summary.json` | `passed` | `76b8f05ab4f69e318c8f9723612d96caac29ce74fc0bfce7c6784a64887042d9` |

Sixteen Python tooling tests pass, including exact/missing/duplicate/foreign-PID marker
checks, private FD handshake, early-exit rejection and timeout cleanup. Standalone
optimized JIT and debug interpreter each pass all 84 focused cases, with zero failures
or skips (168 executions, not 168 distinct cases). All recorded bring-up input hashes
still match, and the tested native sources match the development checkout.

## Commit checkpoint

**Can do:** start the real linked emulator, return to C++, run ordinary Erlang in that
same process and continue host control flow. The clean snapshot baseline also passes.

**Cannot do:** shut the engine down while preserving the host, recover from arbitrary
startup errors, create independent isolates, or claim profile/security/performance
acceptance. There is deliberately no dummy isolate API to imply otherwise.

**Next:** establish coordinated shutdown, thread ownership/joins and host-surviving
engine destruction. Then introduce actual isolate-owned code/atom/service worlds and
prove conflicting same-name modules. Keep the P0-03 and isolate gates open until those
behaviors exist and pass their own witnesses.
