<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 Leandro Ostera <leandro@ostera.io> -->

# Unbooted engine lifecycle: cleanup prerequisites

Target: real shared-infrastructure initialization without an OTP world, followed
by cleanup, repeated creation and failure rollback. **That target is not yet
implemented. `Engine::create()` still returns `not_implemented`.**

The existing `prepare_runtime` is not a suitable completed factory: early startup
initializes allocator, polling, thread-progress, thread-local and scheduler state,
then `erl_init` additionally constructs diagnostic world tables/services. Most
initializers have no matching teardown entrypoint. An empty scheduler handle list
alone does not establish that these resources can be released.

## Implemented prerequisite: cold POSIX pollset cleanup

The real poll backend now provides `erts_poll_discard_unstarted` (including the
fallback implementation). It requires exclusive ownership and refuses without
mutation if a poll wait has ever happened, descriptors remain registered, or lazy
updates remain queued. A started pollset is intentionally outside its scope.

For eligible sets it closes private kernel-poll, wakeup-pipe and timer descriptors,
frees polling/select/status/update storage, destroys its mutex when applicable,
and releases the allocation. Caller-owned descriptors are not closed.

Pollset construction now unwinds on kernel-descriptor, wakeup-pipe/nonblocking
setup and timer-descriptor creation failures, returning NULL with errno preserved.
The initial object allocation uses `erts_alloc_fnf`. This is not general recoverable
ERTS OOM: nested allocations, mutex setup and other runtime initializers can still
be fatal. Existing diagnostic `erts_init_check_io` callers explicitly handle NULL;
that larger initializer still lacks aggregate rollback and remains fatal on error.

There is no mode switch or alternate mock poll backend. The existing runtime uses
the same constructor and storage. The new cleanup API is POSIX-only; Windows does
not advertise it.

## Checks

`libbeam/tests/native_unstarted_poll_test.c` runs against the actual native archive:

- 32 cycles of primary, wakeup-enabled and configured fallback pollset creation
  and release, with peer survival;
- descriptor snapshots over fd numbers 0–8191, plus a functional caller-owned
  pipe that survives cleanup;
- refusal while a live user descriptor is registered;
- real `RLIMIT_NOFILE` exhaustion: failure with zero spare descriptors and rollback
  after the kernel-poll descriptor succeeds but the wakeup pipe cannot be created;
- preservation of errno, descriptor snapshots and restoration of the fd limit;
- construction after those failures, and refusal after a nonblocking poll wait.
  That last started object is deliberately retained until test-process exit.

This is a poll-component test, not Engine shutdown. The supporting prepared runtime
still exits with the test process. No live poller teardown or concurrent destruction
is claimed.

Evidence uses the shared validation lock and external outputs:

- `/tmp/libbeam-unbooted-poll-first/summary.json`: cold cleanup.
- `/tmp/libbeam-unbooted-poll-rollback/summary.json`: real descriptor failure rollback.
- `/tmp/libbeam-unbooted-poll-final/summary.json`: started-object refusal added;
  native components, three raw hosts and four PTY checks pass.
- `/tmp/libbeam-unbooted-poll-package/manifest.json`: frozen native package.
- `/tmp/libbeam-unbooted-poll-verified/summary.json`: six native CTests, one scaffold
  CTest, 29 tooling tests and three CMake-linked hosts pass. Both unchanged example
  builds still fail at `Engine::create`.
- `/tmp/libbeam-unbooted-poll-posix/summary.json`,
  `/tmp/libbeam-unbooted-poll-posix-package/manifest.json` and
  `/tmp/libbeam-unbooted-poll-posix-verified/summary.json`: final validation after
  limiting declaration of the cleanup API to POSIX. All checks above repeated.
- `/tmp/libbeam-unbooted-poll-final-sync.json`: final development/snapshot hashes.

Validated platform/backend: incremental macOS ARM64 debug interpreter, including
its configured fallback polling backend. Other platforms are not validated.

## Remaining before factory success

1. Separate shared initialization from diagnostic namespace/service initialization.
2. Add aggregate I/O coordinator rollback (including cross-pollset registrations),
   plus cleanup of allocator, TLS, thread-progress and scheduler metadata.
3. Connect those real cleanup operations to an owned initialization transaction;
   return recoverable failures without leaving partially initialized global roots.
4. Wire the native lifecycle into C++ creation/shutdown and test complete repeated
   engine creation. Do not report success using only a control allocation or
   process-lifetime retention.

These are implementation gaps, not requests for exhaustive proof before progress.
