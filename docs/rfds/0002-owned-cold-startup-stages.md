<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 Leandro Ostera <leandro@ostera.io> -->

# Owned cold startup stages

**Partial lifecycle implementation, not a completed unbooted Engine.** The public
factory still returns `not_implemented`; the unchanged two-isolate example still
stops there. Repeated complete Engine creation/shutdown and complete initialization
failure rollback are not implemented.

This continues [cold pollset cleanup](0002-unbooted-engine-cleanup.md) through real
coordinator and thread-progress resources. It does not introduce an embedding mode,
an alternate backend, a current-isolate selector or a retained-engine factory.

## Poll groups and the I/O coordinator

`ErtsEngine` now owns an `ErtsIoPollGroup`. The existing early initializer receives
the engine explicitly and uses the same group constructor exercised by the tests.
The group owns:

- primary pollsets, configured scheduler/fallback sets, and their vector;
- the original thread-record allocation, without losing its base when indexing
  past scheduler/fallback records;
- every thread's result buffer, including multiple threads sharing a primary set.

Construction uses failure-returning allocations for these containers/buffers and
unwinds already-created children on descriptor failure, preserving errno. Nested
backend allocation/lock failures can still be fatal. This is not generally
recoverable ERTS OOM, and the diagnostic startup wrapper still exits on failure.

`erts_poll_can_discard_unstarted` supplies non-mutating child preflight. Group
release preflights every child before releasing any storage. Bound groups are
refused by the unpublished-group API. Handing a thread record to a worker
permanently closes cold-cleanup eligibility, independently of whether it has
already waited.

`erts_discard_check_io_unstarted` releases the actual prepared coordinator on the
validated continuous-fd POSIX path. It requires a prepared engine, its fixed group
binding, no thread-record borrowers and no pollset registrations/queued updates.
It also checks the driver-event table for remaining state, flags, events and
resource pointers before destroying its locks and storage. Those mutexes are now
initialized as destructible resources rather than permanent/static locks.

Success clears the I/O binding and the engine's group pointer and enters
`ERL_RUNTIME_RELEASING`. It does **not** reset the runtime claim. That phase cannot
resume preparation/bootstrap, and the control-only discard API still refuses it.
Other initialized resources remain owned/retained pending their cleanup.

Driver-event storage still uses the fixed shared coordinator binding; this is not
per-isolate driver-state ownership. Started/cross-registered pollsets are refused,
not drained or forcibly dismantled. Windows does not provide this cold release.

I/O size reporting includes the group's containers and result buffers and returns
zero after release. Its fallback query now calls the fallback backend's information
function rather than interpreting fallback storage through the primary backend.

## Thread-progress backing and TLS

`ErtsEngine` also owns an `ErtsThreadProgressDomain`. Pre-initialization owns the
real TSD key; initialization retains the base of its cache-aligned backing for
callback arrays, managed-thread data and wakeup/reference-count state. The normal
runtime uses this backing through a fixed engine binding.

The backing uses system allocation rather than permanent ERTS allocator storage:
allocators depend on thread progress during their own cleanup. This breaks that
allocation dependency and permits thread progress to be released last. Both the C
memory report and `allocated_areas` external-allocation accounting include these
bytes. The owner exposes its requested allocation size for inspection.

Pre-initialization and backing initialization return errors; their existing
startup callers still handle those errors fatally. They are not a completed
returning initialization transaction.

The terminal `erts_thr_progress_discard_unstarted` stage requires exclusive
ownership, `RELEASING`, released coordinator state, no current TLS borrower,
registrations, blockers, delayed references, wakeup requests or callback roots.
It deletes the key, clears the binding/owner and frees the backing/control storage.
This is not managed-thread unregister, running-domain shutdown or restart.

**Cleanup ordering remains essential:** disconnect all users and finish allocator
cleanup before this terminal stage in a complete lifecycle. The component test
performs no allocator/VM operations afterward; the remaining prepared runtime
allocations are retained until test-process exit. It does not demonstrate their
cleanup or a reusable/unloadable runtime.

## Checks and evidence

`libbeam/tests/native_unstarted_poll_test.c` now additionally checks:

- 32 cycles of independent real poll groups, including multiple primary sets,
  scheduler/fallback variants, original record bases and result buffers;
- invalid configurations/record indices, bound-group refusal and peer survival;
- real `RLIMIT_NOFILE` exhaustion with zero through eleven spare descriptors:
  failures in later children release earlier children, errno is preserved, fd
  snapshots balance, and construction succeeds when sufficient fds are available;
- refusal after a record is borrowed, without descriptor mutation;
- actual prepared coordinator release, descriptor removal, zero reported I/O size,
  peer disposal, and refusal to resume or discard the still-incomplete engine;
- thread-progress ownership/accounting, wrong-state refusal and terminal release
  of its backing/TSD key.

The borrowed group and previously-waited standalone pollset are intentionally
retained until process exit. No running cleanup is claimed. Descriptor snapshots
cover fd numbers 0–8191; exhaustion-test descriptors are below the reduced limit.

Two useful failures were preserved:

- `/tmp/libbeam-io-group-first/unstarted-poll.log`: the older test read
  `ErtsPollInfo.active_fds`, which the backend information API does not initialize.
  It now checks the initialized poll-set count and cold-disposal predicate.
- `/tmp/libbeam-io-group-release/unstarted-poll.log`: real coordinator cleanup
  exposed the static-lock destruction assertion. The initializer now uses matching
  destructible lock lifetimes.

Successful evidence, serialized under the shared validation lock:

- `/tmp/libbeam-owned-cold-stages/summary.json`: first passing combined stages.
- `/tmp/libbeam-owned-cold-reviewed/summary.json`: final native components, three raw
  startup hosts and four PTY checks, with the added source/header input hashes.
- `/tmp/libbeam-owned-cold-reviewed-package/manifest.json`: frozen native package.
- `/tmp/libbeam-owned-cold-reviewed-verified/summary.json`: six native CTests, one scaffold
  CTest, 29 tooling tests, three CMake-linked hosts, and both unchanged-example
  frontier failures at `Engine::create`.
- `/tmp/libbeam-owned-cold-reviewed-sync.json`: development/snapshot native-file hashes.

Validated scope: incremental macOS ARM64 debug interpreter and its configured
fallback backend. No clean-bootstrap, other-platform, sanitizer, JIT, isolation,
security or performance acceptance is implied.

## Follow-up: allocator ownership

[Engine-owned allocator storage](0002-owned-allocator-domain.md) moves major
allocator storage roots and retained aligned-allocation bases under the engine.
This advances ownership, not the draining/stopping and complete lifecycle still
required below.

## Still required before factory success

1. Separate shared engine initialization from diagnostic namespace/service creation.
2. Own and release allocator backing, remaining TLS and scheduler metadata, with
   dependency-correct cleanup and no reachable references freed.
3. Connect completed stages to recoverable initialization and full rollback. Only
   then release the single runtime claim and support repeated creation.
4. Wire that real lifecycle into C++ creation/destruction/shutdown. Keep failure
   explicit until then; these component cleanup results are not Engine acceptance.
