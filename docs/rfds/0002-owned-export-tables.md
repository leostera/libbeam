<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 Leandro Ostera <leandro@ostera.io> -->

# Owned export tables

`ErtsIsolateNamespaceState` now owns `ErtsExportNamespace`, alongside its atoms,
module slots and external-fun literal pool. Diagnostic startup constructs the
same component and explicitly binds it; fresh states receive empty private tables.

## Ownership and access

The component owns all export index/hash tables, their read/write lock, unique
export-blob accounting, admission limit and open-staging marker. Each blob contains
one real `Export` and one index entry per code slot. Index entries retain their
immutable allocation owner. Staging shares the blob within that owner, including
its external-fun literal, rather than making another export object or allocating
another literal.

`export.c` no longer instantiates the global staged-table template. Its actual
runtime allocation, lookup, loading, stub lookup, enumeration, diagnostics and
staging paths use the owned implementation. The generic template remains in use
by the **unmigrated** fun and record tables. There is no parallel mock export table.

Component operations take the owner and explicit code slots. Atom indices are
local keys; callers must establish membership in the corresponding atom namespace.
The parent supplies engine affinity. The fixed `diagnostic_exports` binding exists
only at the remaining contextless runtime boundary, not as a current-isolate API.

The diagnostic lock-free active lookup and rechecked active/staging stub lookup
retain their code-index protocol. Existing code-index commit locks delegate to the
same owned export lock. Frequent-read lock configuration and the blob-slot staging
membership fast path are retained. Component insertion enforces an exact per-slot
limit before allocation, independently of index-page rounding; allocation OOM
still has fatal ERTS semantics.

## Staging is not a complete code transaction

`start_staging(owner, source, destination)` preflights identity and capacity before
changing dispatch slots or tables. Invalid/open-stage/conflicting/full cases refuse
without mutation. `end_staging` closes the component's staging interval only.
Neither operation selects or publishes the active index, and neither promises
whole-code-space rollback. As before, diagnostic abort can retain stubs in inactive
tables. No standalone export commit/abort acceptance is claimed.

Active/staging index rotation, permissions and waiters, barriers, fun/record tables,
catches, ranges, loader imports, code/literal retirement and process affinity remain
coupled work. Executable private loading remains unavailable.

## Disposal

Unpublished disposal requires exclusive ownership and no borrowed terms, pointers
or readers. It refuses bound owners, open staging, non-stub dispatch targets,
BIF/tracing/breakpoint/deferred-code markers and nonempty code-info metadata.
The parent preflights **all** children before destruction. Index/hash storage is
released, shared blobs are freed once their final slot is removed, accounting must
reach zero, and the parent then releases its literal pool and atoms.

These checks do not prove absence of process references. Published diagnostic
state stays retained; there is no forced free or live-code reclamation shortcut.

## Evidence

Serialized under the shared validation lock, incremental macOS ARM64 debug
interpreter:

- Two namespace states insert equal MFAs and obtain distinct real exports/literals.
  Repeated insertion is idempotent; all three slots share one blob after staging.
  Unique-blob accounting does not multiply by slot count.
- Invalid inputs, exact capacity, incompatible slot identities and open staging
  refuse safely. Synthetic resource markers exercise parent retention guards.
  Growth reaches at least 2,200 entries; disposing that owner leaves its peer's lookup,
  literal and accounting intact. Sixteen fresh replacements allocate/dispose.
- Real diagnostic bytecode loads alternating implementations of the same function
  through eight loads, invokes a retained external fun across reloads, purges,
  deletes and autoloads again. Sixteen worker processes perform concurrent stub
  lookups while the controller loads code. This is **one diagnostic world**, not
  two-isolate execution or a proof of every scheduling interleaving.
- Namespace/literal/module guard tests, six native CTests, one scaffold CTest,
  29 tooling tests, three raw host trials, four PTY cases and three CMake-linked
  host trials pass. The unchanged example remains at the honest factory frontier.

Artifacts:

- `/tmp/libbeam-export-tables-native/summary.json`: initial extraction witness.
- `/tmp/libbeam-export-tables-regression/summary.json`: growth/guard/reload checks.
- `/tmp/libbeam-export-tables-final/summary.json`: final native/components/hosts.
- `/tmp/libbeam-export-tables-package/manifest.json`: frozen archive package.
- `/tmp/libbeam-export-tables-verified/summary.json`: final regression checks,
  matching native development/snapshot hashes, and archive-symbol verification
  that the former export table/lock/accounting/staging/limit globals are absent.

No public Engine/Isolate success, process isolation, live code retirement, engine
shutdown, clean-bootstrap, JIT, sanitizer, security, latency or density claim.
