<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 Leandro Ostera <leandro@ostera.io> -->

# Code-space ownership batch

This batch extends `ErtsIsolateNamespaceState` beyond atoms/modules/exports. The
real diagnostic runtime uses these same owned components; no parallel mock tables,
current-isolate selector, root swapping or second emulator is introduced.

## Moved together

| Component | Owned state |
| --- | --- |
| Local funs | All index/hash slots, shared entry blobs, dispatch slots, staging lock, accounting and staging interval |
| Native records | All index/hash slots, shared entries, definition references, staging lock and interval |
| Catches | Slot pools, shared/growing vectors, free lists, high-water marks and staging flags |
| Code ranges | Slot arrays, search caches, accounting, insertion cursor/budget, staging interval and literal-dump scratch storage |
| Module coordination | Old-code locks, unsealed-module tracking, staging slot and abort boundary |
| Code-space coordinator | Active/staging indices, transaction phase, modification/staging permissions, holders and waiter queues |

Fun and record storage now uses `erl_code_table.c`, extracted from the former
macro-generated implementation. The unused `erl_code_staged.h` and redundant
individual diagnostic staging entrypoints are removed. Each allocation callback
receives its table owner through the entry, and shared blobs are freed only after
their final slot is removed. The actual loader/lookup/purge/record paths use this
storage.

Old-code locking and unsealed tracking live together with module staging metadata,
not as detached global locks. Range crash-dump scratch is owned with ranges; the
existing diagnostic dumper explicitly borrows it. Catch vectors can be shared
between slots **within one owner**, and growth retires a vector only after its last
slot reference disappears.

`ErtsCodeSpace` is constructed after its namespace's children. Diagnostic startup
explicitly binds it and its children for initial slot-zero preloading. Fresh
unpublished namespaces start with active slot zero and staging slot one. The
contextless runtime boundary still has fixed diagnostic bindings, including JIT
code-index address emission. These are not private process dispatch.

## Coordinated table transactions

- Begin preflights **every** component before changing any component. Identity,
  capacity, pending fun purge, open staging and unsealed-module failures preserve
  the previous state.
- End checks the module staging/unsealed state and refuses abort if newly added
  module entries still retain resources. Callers must unwind those resources;
  abort never force-frees them.
- End/commit moves the coordinator through staging, ready and idle phases.
  Publication holds the export, fun and record locks while rotating only that
  owner's indices. Invalid phase transitions refuse.
- Abort leaves the active index unchanged, erases newly introduced module metadata,
  and clears the destination range set. As in the original runtime, inactive stub
  entries may remain in export/fun/record tables. Abort is not a byte-for-byte
  restoration of every inactive cache.

The APIs require exclusive staging authority. Borrowed children must not be
independently staged while a coordinator transaction owns them. A violation of
that internal invariant is not a recoverable host operation. ERTS OOM also remains
fatal.

**A table commit is not permission to execute arbitrary private code.** The
existing diagnostic caller still supplies instruction barriers, tracing-cache
maintenance and execution retirement. Private loader/process propagation and an
owned retirement protocol are not implemented by these metadata APIs.

## Cleanup and shared infrastructure

Parent disposal preflights all children, including the coordinator. It refuses
open/ready transactions, unsealed modules, loaded fun targets, pending purge,
record definition references, live catches/ranges and module resources. Only an
exclusive unpublished owner with no borrowed terms, pointers, readers or jobs can
be discarded. Published diagnostic state stays retained.

Scheduler-wide instruction-barrier accounting and the debug per-thread barrier
flag remain shared infrastructure. They are not current-world selection. The
aggregate loaded-code byte counter also remains shared accounting, not a private
quota implementation.

## Validation

Incremental macOS ARM64 debug interpreter, shared validation lock:

- Two owners have distinct equal-key fun and record entries. Growth adds 1,500
  keys to one owner's tables. Slot copies preserve one blob per owner and
  accounting does not multiply by slot count.
- A refusal in the last staging component leaves earlier components untouched.
  New module resource retention refuses abort without advancing the active index.
  Independent commit/abort and repeated index rotations leave the peer unchanged.
- Catch growth beyond 1,024 slots exercises shared-vector retirement; equal local
  catch indices resolve to different owner-local addresses. Ranges and their
  caches survive peer disposal. Synthetic addresses are **never executed**.
- Unsealed tracking is tested with two simultaneous interpreter metadata owners;
  wrong-owner sealing refuses. Resource guards, exact table capacity and sixteen
  fresh namespace replacements are exercised.
- Real diagnostic bytecode repeatedly reloads two implementations, creates and
  serializes local closures and native records, catches exceptions and resolves
  stack traces through the new tables. Concurrent stub lookup and retained
  external-fun dispatch tests remain enabled.
- Full namespace/export/module tests, six native CTests, one scaffold CTest,
  29 tooling tests, three raw hosts, four PTY cases and three CMake-linked hosts
  pass. Both unchanged example builds still fail explicitly at `Engine::create`.

Evidence:

- `/tmp/libbeam-code-space-first/summary.json`: initial fun/record runtime witness.
- `/tmp/libbeam-code-space-ranges/summary.json`: **not range-migration evidence**;
  a whitespace-check failure skipped synchronization in the initial shell chain.
  The stale-source run was identified and not accepted as validation.
- `/tmp/libbeam-code-space-ranges-synced/summary.json`: corrected synchronization.
- `/tmp/libbeam-code-space-coordinator/summary.json`: diagnostic coordinator run.
- `/tmp/libbeam-code-space-components/summary.json`: native ownership/transaction
  tests and real local-fun/native-record/catch/stacktrace execution.
- `/tmp/libbeam-code-space-final/summary.json`: unsealed tracking and removal of
  the legacy template/entrypoints.
- `/tmp/libbeam-code-space-final-checked/summary.json`: final native build and
  component/execution/startup/PTY checks, including fun/record growth, restored
  diagnostic permission assertions and strict seal-state checking.
- `/tmp/libbeam-code-space-package/manifest.json` and
  `/tmp/libbeam-code-space-verified/summary.json`: initial frozen package and full
  regression checks; build logs retain the new compiler warnings found in review.
- `/tmp/libbeam-code-space-clean-native/summary.json`: final build after fixing
  prototype visibility, declaration placement and legacy name-parameter const
  qualification. The new warnings are gone; existing upstream warnings remain.
- `/tmp/libbeam-code-space-clean-sync.json`: final development/snapshot hashes
  and deletions.
- `/tmp/libbeam-code-space-clean-package/manifest.json`: final frozen package.
- `/tmp/libbeam-code-space-clean-verified/summary.json`: repeated final CMake,
  tooling and host checks, matching source hashes, and absence of the former
  global table/index/permission/lock/range symbols.

## Still missing before private execution

This is the main **code-table and transaction ownership** batch, not all mutable
BEAM state or completion of the two-isolate example.

1. Propagate namespace/code-space ownership through prepared loaders, imports,
   fixups, lookup, dispatch and immutable process affinity. Contextless callers
   currently resolve the diagnostic world.
2. Give loaded/prepared code and literal references a complete ownership/retirement
   protocol; the purger and literal collector still need owner-aware jobs and
   roots. The reduced-profile loader must reject unsupported effects and `on_load`
   before publication.
3. Migrate process/world services: registrations, ETS, persistent terms, timers,
   communication/resource membership, and necessary application roots.
4. Implement owned engine initialization, host calls/completions, stop/drain and
   physical executable-world reclamation through the public C++ API.

No private bytecode execution, successful public Engine factory, live-code
reclamation, engine shutdown, clean-bootstrap, JIT/platform validation, sanitizer,
security, latency or density acceptance is claimed. JIT source references were
updated but the validated backend remains the debug interpreter.
