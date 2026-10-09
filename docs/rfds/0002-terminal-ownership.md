<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 Leandro Ostera <leandro@ostera.io> -->

# RFD 0002: remove native console ownership

## Observed bug

The previous reduced runtime still called `sys_tty_reset` during process exit.
With a host-owned PTY as stdin, `-noshell -noinput` and an explicit `halt(0)`, it
changed the shared open file description from nonblocking to blocking. The parent
observed `F_GETFL` change from **6 to 2** on macOS, although terminal attributes
remained unchanged. This was an unwanted host-side effect, not a legacy suite
compatibility issue.

Baseline: `/tmp/libbeam-terminal-cut/before.json` and `before.log`, using the
previous native debug executable from the `047525e9` checkpoint. The violation was
also reproduced after the fix by linking a tiny executable host against that
checkpoint's preserved archive, verifying its SHA-256 against the original report:
`/tmp/libbeam-terminal-cut/old-archive-reproduction/summary.json`, status
`expected_ownership_violation`. Thus the new observer also fails against the old
runtime, not only against synthetic unit-test mutations.

## Deletions

- Unix terminal snapshot/reset state and the process-exit reset call.
- Break-request flag, initialization, scheduler polling and platform declarations.
- Interactive break handler, stdin key reader, process-killing console, opcode
  dump and interactive binary-inspection helpers.
- The TTY NIF's write to `using_oldshell`. Removing the global exposed this caller
  at link time; it was removed rather than restoring a compatibility variable.

No replacement no-op functions or failure stubs were added. Noninteractive
process/code/port diagnostics and base64 formatting remain because crash dumping
still calls them. This does **not** make fatal dumping or `halt` host-safe.

## Validation

All three same-PID native C++ trials and four terminal cases pass on macOS ARM64,
debug interpreter. Twenty-two Python tooling tests pass. Final report:
`/tmp/libbeam-terminal-cut/tty-caller-removed/summary.json`, status
`started_and_returned_not_shutdown`, SHA-256
`1f9bc847424129260d478a218260bf2b1689dff75b44d278b466f06db8d5695b`.
Recorded input hashes and modified runtime sources were verified against the tested
checkout. This is incremental validation, not a fresh clean-bootstrap matrix.

The runner tests four terminal cases:
blocking/nonblocking stdin crossed with explicit exit codes 0 and 17. Each uses a
fresh private PTY, noncanonical/no-echo attributes, and a shared parent descriptor
so changes made by the runtime cannot disappear when the child exits. It requires
actual Erlang output before the requested exit, matching exit status, and unchanged
file-status flags and terminal attributes.

Only stdin is a terminal; stdout/stderr are bounded evidence logs. These cases
exercise the intentionally process-exiting executable adapter, **not** engine
destruction, host survival after `halt`, an interactive shell, or an isolate
implementation based on subprocesses. The separate three native C++ trials still
require bytecode and host control in the same PID.

Observer unit tests cover preserved state, a deliberate blocking-mode reset,
terminal-attribute mutation and timeout cleanup. Archive checks reject the removed
native symbols as either definitions or unresolved references.

The first deletion build (`/tmp/libbeam-terminal-cut/final`) failed to link because
`prim_tty_nif.c` still wrote `using_oldshell`; this is retained diagnostic evidence,
not a passing run.

## Still open

The runtime still has one global Erlang world, no destructor and process-fatal
paths. File/code servers, console/logging bootstrap, FD drivers and the TTY NIF
remain. Unix startup still fills missing standard descriptors with `/dev/null`;
FD/terminal ownership is therefore **not fully solved**. Runtime-owned thread and
resource shutdown, host loading/transport, and isolate namespaces remain ahead.
Windows sources are unported/unvalidated; no cross-platform acceptance is claimed.

Next: replace the remaining file/console bootstrap with host-owned loading and
transport rather than treating the host's standard descriptors as runtime resources.
