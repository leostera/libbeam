<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 Leandro Ostera <leandro@ostera.io> -->

# Namespace lifetime flow: process, loader, code and literal checkpoint

**This is a partial checkpoint, not completion of the requested three-area pass.**
World-service ownership and private execution remain unfinished. The unchanged
example still fails at the public `Engine::create` frontier. That failure cannot
be fixed safely by C++ glue alone while native dispatch/services remain diagnostic.

## Implemented

`ErtsIsolateNamespaceState` now tracks atomic lifetime leases. Acquiring requires
an existing owner/lease; new admission and administrative disposal must be
externally serialized. Disposal refuses outstanding leases. Releasing the last
lease does not automatically destroy the namespace.

- Real processes acquire a namespace lease before PID publication. The namespace
  is inherited explicitly by spawn, and released after final process destruction
  and physical allocation release, not merely when the process exits.
- Dirty shadows and scheduling proxies borrow affinity through their retained
  real process. Empty scratch processes start without a world. The bootstrap
  dummy parent and diagnostic distribution boundary bind explicitly.
- Prepared loader binaries retain their namespace. Destructor cleanup releases
  the lease after prepared resources and is idempotent. Finish-loading validates
  prepared-code provenance before entering the code transaction.
- Code headers carry namespace provenance. Publication acquires one lease per
  physical code allocation, not per module-table slot alias. Purge releases that
  lease only after freeing the code and clearing the old-code descriptor.
- Detached code and persistent-term literal areas retain a separate lease. It
  survives queuing, copying and deferred retirement, and is released only after
  off-heap cleanup and physical area release.
- Engine constants and parent-owned export literal pools explicitly use no
  detached lease. Their existing lifetime contracts remain in force, avoiding a
  parent-to-child-to-parent lease cycle for unpublished export pools.

This changes real runtime allocation, spawn, loading and purge paths. It does not
introduce a second implementation or a current-isolate selector.

## Deliberate execution boundary

Private process admission and private preparation still refuse before entering
unmigrated diagnostic dispatch/literal-decoding/import paths. These internal
refusals currently use `badarg`; they are not new successful public API operations.
There is no fallback that silently executes private input in the diagnostic world.

Process affinity is **not yet** complete owner propagation through the interpreter,
JIT, atom decoding, imports, lookups, messaging or native services. Lifetime leases
are **not yet** an owner-aware purge/collector job protocol or stop/drain algorithm.
Persistent-term literal lifetime is **not** private persistent-term storage.

## Focused validation

Native component tests exercise independent loader leases, parent-disposal
refusal, foreign-owner rejection before preparation/publication, repeated loader
destructors, detached literal retention, final release and peer preservation.

Real diagnostic bytecode now prepares/drops 64 modules, retains a code literal
across delete/purge and GC, puts/gets/erases a persistent term, and transfers the
retained value to a monitored child through normal process exit. Existing reload,
closure, native-record, catch, export, atom and host-boundary tests remain enabled.

All tests use the shared validation lock and external outputs:

- `/tmp/libbeam-owner-flow-first/summary.json`: process/loader affinity and leases.
- `/tmp/libbeam-owner-flow-literals/summary.json`: detached literals and expanded
  diagnostic lifetime fixture.
- `/tmp/libbeam-owner-flow-final/summary.json`: final native component tests,
  loaded-code leases, three raw hosts and four PTY checks.
- `/tmp/libbeam-owner-flow-final-sync.json`: development/snapshot source hashes.
- `/tmp/libbeam-owner-flow-package/manifest.json`: frozen runtime package.
- `/tmp/libbeam-owner-flow-verified/summary.json`: six native CTests, one scaffold
  CTest, 29 tooling tests, three CMake-linked hosts and both unchanged-example
  frontier runs; source hashes match.

Validated backend: incremental macOS ARM64 debug interpreter. JIT source changes
are unvalidated. No private execution, complete world retirement, engine cleanup,
security or performance acceptance is claimed.

## Remaining implementation, not proof gates

1. Pass owners through bytecode/ETF atom and literal decoding, imports/fixups,
   active-code lookup, interpreter/JIT dispatch and process communication.
2. Migrate purger/collector state, queued callbacks and service roots to explicit
   owner-aware jobs; implement executable-world stop/drain and retirement.
3. Migrate registrations, ETS, persistent-term tables/update queues, timers and
   remaining world services. The process affinity field is a prerequisite, not a
   substitute for this migration.
4. Implement owned engine initialization/rollback and public C++ creation,
   loading, calls, completion, cancellation and reclamation; rerun the unchanged
   two-isolate example as those native paths become available.
