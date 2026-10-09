<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 Leandro Ostera <leandro@ostera.io> -->

# RFD 0002: one runtime contract, deletion rather than compatibility stubs

## Policy

`beam/` is an editable fork, not an upstream-compatible embedding option. There is
no `erts_is_embedded`, embedding-mode variable, or unrestricted startup branch.
Future upstream fixes are reviewed and selectively ported into this snapshot.

Delete unsupported implementations and their declarations/registrations. Use
compiler/linker errors to find dependencies; remove those callers too. Do not add
empty functions or error-returning native shims merely to satisfy the linker.
A retained operation such as `open_port` must still reject excluded request forms
before effects. That boundary check is not a replacement implementation of the
removed driver.

## Removed in this cut

- Unix spawn/forker drivers, their registrations, executable/environment request
  assembly, fork/exec helper startup, child handshake and exit-status machinery.
- Standalone signal-dispatch thread and notification pipe, signal-to-Erlang
  delivery, normal-startup SIGINT/QUIT/TERM/USR1/CHLD/PIPE/FPE policy, break setup,
  terminal-interrupt replacement, and host alternate-stack initialization.
- Darwin main-thread driver pump, its pipes, exported entry points and declarations.
  wx's dependency on those APIs is unsupported; wx remains outside the build.
- `os:set_signal/2`, including its BIF registration and Erlang export/placeholder.
- `os:cmd/1,2`, command-shell discovery/configuration and their helpers/exports.
- Kernel's signal server/handler startup and node bootstrap: hostname/inet database,
  distribution/RPC/global naming, peer supervision, and optional boot-server,
  disk-log, pg, timer-server and compile-server startup. The extra safe supervisor
  and distribution configuration-change callbacks are gone too.
- Break CLI flags and their automatic insertion by compiler/make launch modes;
  `system_info(break_ignored)` no longer has a handling branch.

Kernel now has one explicit bootstrap child list. There is no `-mode minimal`
versus full-node branch and no configuration switch that restores the deleted
startup constructors. This is not a claim that every remaining OTP module or
native distribution primitive has been removed.

`{spawn,...}`, `{spawn_executable,...}` and `{spawn_driver,...}` requests are rejected
by the retained internal open-port boundary with `notsup` (public wrapper:
`error:notsup`). Deleted OS functions produce `undef`, not compatibility errors.
No native spawn/forker or Darwin callback failure stubs remain.

## Compiler-driven findings

Evidence directories are under `/tmp/libbeam-host-signals/`:

- `first`: the initial signal experiment used a nonexistent C exception constant;
  compilation correctly rejected it. That implementation was subsequently deleted.
- `deleted-hooks-scan`: deleting declarations exposed late-driver and break setup
  callers, plus the remaining mode checks in spawn/forker code.
- `deleted-runtime`: linking exposed `system_info(break_ignored)` and required
  float/text conversions sharing `sys_float.c`. The conversions were retained;
  the signal initialization function was deleted, not replaced with a no-op.
- `required-floats-restored`: removing the helper header exposed a leftover child
  handshake in the shared FD driver. That protocol and its state were removed.
- `spawn-handshake-removed`: native build, archive checks and linking passed. Boot
  failed because `inet_db` tried to open `udp_inet` for hostname discovery. The
  node/network bootstrap constructors were removed instead of restoring drivers.

These failures are retained development evidence, not passing runtime trials.

## Validation

Three fresh native-host trials pass on macOS ARM64, debug interpreter. Report:
`/tmp/libbeam-host-signals/minimal-kernel/summary.json`, status
`started_and_returned_not_shutdown`, SHA-256
`c6ea149470942a653556e9b41409565662259ad264747f4c456b58cb6f82c17a`.
All recorded input hashes remain unchanged, and modified runtime sources match the
tested checkout byte-for-byte. Eighteen Python tooling tests pass. This is an
incremental configured-source validation, not a new clean-bootstrap or platform matrix.

The strengthened witness checks:

- Same-PID bytecode execution, returning host control and second-start rejection.
- No forbidden native definitions **or unresolved references** in the linked archive.
- Nine port-spawn denial calls and two deleted command-API calls; unchanged port
  table and absent helper/command sentinels, across three BINDIR configurations.
- Six deleted signal-API calls; no native BIF or Erlang export for that API.
- Exact retained Kernel child list and absence of the removed registered services.
- Nine seeded host signal dispositions, their flags/masks, the calling thread's
  signal mask and alternate stack preserved at return and acknowledgement;
  deliberate SIGUSR1 delivery still reaches the host handler.
- Retained FD EOF/close behavior, ordinary spawn/monitor cycles, float arithmetic,
  `badarith` on floating division by zero, and float/text conversions.

The runner compiles the interdependent Kernel/OS bootstrap modules in one compiler
VM before rebuilding. Its existing configured compiler is a build tool, not the
native-host witness. Compiler-binary and resulting bootstrap-BEAM hashes are
recorded. No regenerated bootstrap binaries belong in the source commit. The
existing bootstrap/package artifacts have not been certified as a closed profile;
removing startup constructors does not prove absence of every unused OTP module.

The subsequent [terminal-ownership cut](0002-terminal-ownership.md) removes the
remaining native break console and process-exit terminal reset, including a
reproduced host-FD mutation. The evidence above remains the earlier checkpoint.

## What remains, and why

| Surface | Current decision |
| --- | --- |
| Schedulers, GC, thread progress, literal/code reclamation | Retain: required execution/memory machinery, not dispensable node services. Thread ownership and shutdown still need redesign. |
| Runtime-thread alternate signal stacks | Retain on runtime-owned threads; do not replace the host calling thread's stack. |
| Fatal crash-dump suspend pipe/handler | Still present. Normal signal takeover removal does not make fatal paths host-safe. |
| Code/file server, trusted `on_load`, user/stderr and logger | Temporary bootstrap dependencies. Replace with host loading/transport; not tenant permissions. |
| Kernel configuration/reference-count workers | Retained while their users are mapped; not isolate-local yet. |
| FD driver and native file/network/NIF surfaces | Still require ownership/effect auditing or removal. Disabling spawn drivers is not a closed native profile. |
| `erl_start` executable host | Starts the same runtime and parks its calling thread for compiler/bootstrap use. No separate runtime mode or standalone signal pump. |
| Windows and other platforms | Not validated by this cut. Unbuilt upstream platform/library source is not an approved libbeam implementation. |

**Still cannot:** safely destroy/restart the engine, recover all startup failures,
protect the host from `halt`/fatal paths, create isolates, or run untrusted bytecode.
The witness ends with process exit, not physical engine reclamation. Floating-point
validation assumes the ordinary untrapped environment. No latency/density/budget
or sanitizer acceptance is claimed.

**Next:** replace the remaining file/console/node-shaped bootstrap with host-owned
loading and invocation, then establish stoppable thread/resource ownership and
isolate-local state. Do not restore removed services merely to make legacy suites
or optional applications pass.
