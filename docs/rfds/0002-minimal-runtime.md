<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 Leandro Ostera <leandro@ostera.io> -->

# RFD 0002: remove behavior libbeam does not need

## Decision rule

OTP is implementation material, not a compatibility contract. For each failure, ask:
**does libbeam need this behavior?** If yes, fix it. If no, remove it or explicitly
reject it rather than restoring compatibility. Supported BEAM semantics, correct
ownership, host survival and safe lifecycle remain requirements; memory corruption
and resource leaks in supported paths are not ignorable legacy failures.

Standalone `erl` currently helps compile/bootstrap fixtures. Its continued full
behavior is not a product requirement. The baseline driver now defaults to the
startup toolchain smoke profile; `--legacy-diagnostics` opts into the former 84+35
case selections. Requesting those diagnostics still checks strict failures/skips,
but they do not define the embedded runtime's supported surface. The six-housekeeping-
process startup fixture is also temporary bring-up scaffolding, not a mandate to
retain six workers forever.

## Historical first cut: no executable-port helper in embedded startup

The implementation and evidence below describe `f72ac87c`. The mode switch and
native failure stubs have since been deleted, along with node startup services;
see [the current single-runtime contract and evidence](0002-single-runtime.md).

The previous real-runtime witness needed BINDIR because ordinary OTP startup launches
`erl_child_setup` for executable ports. That dependency is not needed for libbeam.
The embedded path now:

1. Selects immutable embedding mode before initialization.
2. Skips forker-port creation in `erl_sys_late_init`, before its environment setup,
   sockets and native child creation. It no longer requires BINDIR.
3. Does not replace the host's SIGCHLD disposition with `SIG_IGN`, since it no longer
   owns a child helper to reap. Other signal ownership remains unresolved.
4. Rejects `{spawn, ...}` and `{spawn_executable, ...}` at the internal open-port BIF
   before option parsing, environment merging, driver lookup or port creation.
5. Also rejects at both Unix effectful native entries, `spawn_start` and `forker_start`,
   before pipes, executable-path checks, environment lookup or helper dispatch.

Error behavior is explicit: `erts_internal:open_port/2` returns `notsup`; the public
`erlang:open_port/2` wrapper raises `error:notsup`. Its dynamic `apply` route does the
same. The existing `os:cmd/1` facade raises `badarg` with `{open_port,notsup}` in its
error-info cause. The native driver entry guards use `ENOTSUP`/`ERL_DRV_ERROR_ERRNO`.
Malformed excluded requests need not retain legacy option-validation precedence.

This removes a runtime dependency, **not every compiled port implementation**. Legacy
compiler/debug startup still has its old path. FD/linked-driver support, filesystem,
networking and native loading are not generally blocked. This is not the complete
positive-list profile, and is not permission to execute untrusted bytecode.

## Evidence

The actual C++ host and Erlang-in-the-same-PID witness pass three fresh-process trials:

| BINDIR input | Result |
| --- | --- |
| absent | Startup and bytecode execution succeed |
| nonexistent directory | Startup and bytecode execution succeed |
| directory containing sentinel-writing `erl_child_setup` canary | Startup succeeds; helper sentinel absent |

Each trial checks:

- OTP bring-up and 20 ordinary spawn/monitor cycles continue to work.
- No forker port appears in the port table.
- Seven executable-port denial routes pass: direct internal calls, public calls and
  `apply` for both spawn forms, plus the `os:cmd` facade.
- The port table is unchanged across denied calls; no command sentinel is written.
- Native startup leaves a host-installed SIGCHLD handler unchanged.
- The C++ host observes `ECHILD` from `waitpid` at its acknowledgement checkpoint.
- Host control returns, and a second engine-start attempt is rejected.

The child check describes that checkpoint, not a syscall trace proving no conceivable
child ever existed. The native entry guards have source-level review here; the test
is not arbitrary-NIF/driver bypass testing or independent security approval.

The first attempt had a fixture syntax error (a stacktrace pattern in a catch clause,
where Erlang requires a variable). It was corrected and retained under
`/tmp/libbeam-no-forker/first`; this was a required-test bug, not a legacy behavior to
ignore. The corrected intermediate and final runs remain in separate directories.

Final report: `/tmp/libbeam-no-forker/final/summary.json`, status
`started_and_returned_not_shutdown`, SHA-256
`5f21fbac8b2eb933aa9a78148c3b14ca2e85242b60665f04d27c967bce05d925`.
All recorded input hashes still match; tested native sources match the development
checkout. Platform: macOS ARM64, debug interpreter. Seventeen tooling tests pass.
The new baseline default selection is unit-tested; a fresh full baseline run with
that new default is not claimed. No new broad OTP compatibility matrix was required
for this cut.

## Commit checkpoint

**Can do:** start the actual linked emulator without its subprocess helper, execute
ordinary Erlang and regain C++ control. Standard executable-port requests fail before
their effects. The host's SIGCHLD disposition is no longer commandeered at startup.

**Cannot do:** destroy the engine safely, create independent isolates, or claim a
closed native profile, memory/CPU budgets or security approval. The test still ends
with explicit process exit, not engine reclamation.

**Next:** reduce the mandatory OTP bootstrap and remaining process-global signal/port
machinery to the minimum needed by the host-driven runtime. Then implement shutdown
for that smaller runtime and real isolate-owned code, atoms and services—not a
compatibility layer around every legacy OTP subsystem.
