<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 Leandro Ostera <leandro@ostera.io> -->

# Engine-owned allocator storage

This is an allocator ownership sweep, **not completed allocator shutdown or
Engine lifecycle acceptance**. `Engine::create()` still refuses success. No
allocator is stopped while its allocations, queues or mappings remain reachable.

## Storage moved

The real startup path passes `ErtsEngine` into `erts_alloc_init`. Its
`ErtsAllocatorDomain` owns:

- allocation/reallocation/free dispatch tables, allocator information and
  thread-specific/preferred instance descriptors;
- initial strategy-state objects, including temporary, standard, long/short-lived,
  process-heap, binary, ETS, driver, fixed-size, literal and test allocators;
- debug dispatch originals and the allocator TLS key;
- per-instance state-vector backing, aligned fixed-size-list backing, and counts
  of successfully started instances;
- carrier-pool sentinel storage used by the real allocator utility implementation;
- segment allocator instances and default/literal memory-mapper controls;
- retained bases and accounting for aligned infrastructure allocations that were
  previously allocated through the permanent-allocation helper.

The runtime still has one fixed engine binding. Former global table objects are
now pointers into owned storage, not per-isolate selectors or swappable worlds.
All ordinary allocation, reallocation, debug wrapping, carrier management and
segment/mapping operations continue through the existing implementations.

The literal mapper is now assigned to literal-allocator options **after** mapper
construction, rather than capturing a pointer before its object exists. Debugger
helpers use the explicit allocator count instead of applying `sizeof` to what is
now a pointer.

## Bootstrap storage and release boundaries

Allocator metadata cannot depend on the allocators it is constructing or stopping.
The domain therefore owns system-allocated, aligned bootstrap blocks with exact
base pointers. Carrier pools, segment instances, mapper controls and per-instance
state/fixed-size lists use this path. Their ordinary runtime pointers are views
into these recorded allocations.

Unpublished domains can allocate/release bootstrap buffers and destroy their
control storage, mutex and TLS key. Disposal refuses bound domains and domains
with outstanding bootstrap/permanent storage or started instances. Bootstrap
release itself refuses bound domains: backend controls cannot be force-freed
without coordinated backend shutdown. Contained resources must be destroyed and
all users disconnected before releasing unpublished storage.

The existing permanent-aligned allocation helper now records the actual allocator
base, type, aligned pointer and requested backing size. The explicit release API
checks the engine, exact pointer and type before handing the base back to the
same real allocator. Wrong-owner, wrong-type, interior and duplicate releases are
refused. Callers must first destroy contained locks/objects and disconnect their
users; there is no bulk permanent-storage free.

Returning a block to an allocator does not necessarily unmap its carrier or drain
foreign-thread deferred frees. Ledger counts are not a physical Engine-reclamation
proof. Started-instance storage and active backend mappings remain retained.

The owner reports control bytes, bootstrap backing/blocks, started instances and
permanent ledger bytes/blocks. Both C memory reporting and `allocated_areas`
external-allocation accounting include system-allocated domain/bootstrap/ledger
storage without counting permanent payloads twice. These are ownership/accounting
observations, not isolate quotas or RSS guarantees.

## Failure behavior

Unpublished domain construction unwinds its system allocation and mutex if TLS-key
creation fails. Bootstrap allocation rejects invalid alignment/overflow and
unwinds failed allocation prefixes. Real TLS exhaustion/recovery is tested.

The larger runtime initializer still has fatal paths. Allocator strategy start,
segment/mapping setup, atom-cache initialization and other subsystem failures are
not a complete recoverable Engine initialization transaction. A successful
metadata-domain constructor is explicitly **not** a successful Engine factory.

## Checks

`libbeam/tests/native_allocator_domain_test.c` runs against the native archive:

- verifies real prepared-runtime ownership, started instances, bootstrap backing
  and existing permanent records;
- refuses disposal of the active domain and release of its mapper control;
- cycles independent unpublished domains/TLS keys and aligned bootstrap storage,
  checking peer survival, invalid alignment/overflow, wrong-owner/interior release
  and outstanding-resource disposal guards;
- performs 32 permanent allocate/release cycles with 64-byte and 4096-byte
  alignment, exact ledger balance and wrong-owner/type/interior/double-free guards;
- exercises real binary allocation/reallocation/free from 3 MiB to 5 MiB, including
  debug dispatch and backing-instance paths;
- exhausts real TLS keys twice, observes `EAGAIN`, checks equal recovered capacity,
  and successfully constructs another domain afterward.

The bytecode startup fixture checks live `erlang:memory/0` and external-allocation
statistics. Existing process/literal transfer, code reload, native-record, closure,
GC and exception fixtures exercise the migrated allocator paths with workers.
The tooling witness test rejects missing or duplicate allocation-statistics markers.

Evidence, serialized under the shared validation lock:

- `/tmp/libbeam-allocator-domain-first/`: preserved fixture compile failure from
  missing configuration/header setup.
- `/tmp/libbeam-allocator-domain-backing/summary.json`: first passing ownership,
  TLS exhaustion, native components and startup/PTY checks.
- `/tmp/libbeam-allocator-domain-final/`: preserved fixture compile failure from
  missing `ERTS_WANT_MEM_MAPPERS` when inspecting the private mapper binding.
- `/tmp/libbeam-allocator-domain-checked/summary.json`: final seven native component
  probes, three raw startup hosts and four PTY checks.
- `/tmp/libbeam-allocator-domain-checked-package/manifest.json`: frozen package.
- `/tmp/libbeam-allocator-domain-checked-verified/summary.json`: six native CTests,
  one scaffold CTest, 29 tooling tests and three CMake-linked hosts. Both unchanged
  example builds still fail at `Engine::create`.
- `/tmp/libbeam-allocator-domain-checked-sync.json`: development/snapshot hashes.

Scope: incremental macOS ARM64 debug interpreter. Existing upstream warnings
remain; JIT, other platforms, hard-debug variants, debugger execution, sanitizers,
security, latency and density are not validated by this checkpoint.

## Remaining

Ownership of these major storage roots does not complete every allocator-global
migration. Backend initialization flags/options, atom caches/initialization locks,
platform allocation policy and some diagnostics still use shared bindings/static
state. Process-wide policies such as physical-memory locking are not made safely
engine-local by moving the option field.

Before a destructible/repeatable Engine can be exposed:

1. Retire users of infrastructure allocations and release those allocations in
   dependency order, including scheduler/TLS metadata and diagnostic world state.
2. Drain deferred frees, fixed-size caches, carrier pools and segment caches;
   stop backing instances only after all live allocations/references are gone.
3. Destroy backend locks, close/unmap owned mapping resources, release bootstrap
   controls and TLS, and clear bindings/initialization state safely.
4. Connect this to staged recoverable initialization/rollback and native/C++
   lifecycle APIs. Only then release the runtime claim and test repeated Engines.
