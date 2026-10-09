<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 Leandro Ostera <leandro@ostera.io> -->

# Owned module-table component

First namespace-storage extraction beneath the unchanged two-isolate example.
**This is not private BEAM loading, an Isolate implementation, or a passing
application-isolation test.**

## Implementation

`beam/erts/emulator/beam/erl_module_table.h` defines an internal opaque
`ErtsModuleTable`. Its implementation lives in the existing `module.c`, not a
parallel mock runtime. Each instance owns its index/hash storage and module-record
accounting. Each `Module` now carries its immutable table allocation owner;
allocation/free callbacks no longer update one global module-byte counter.

The existing loader's module lookup/insertion uses this component. Staging copies
explicitly allocate new records against the destination table rather than copying
the source record's allocation owner. The old code-index-selected tables remain
as a clearly marked diagnostic OTP-world adapter. They are not swapped when a
private table is accessed.

The new internal operations create/find/insert/count/discard tables through an
explicit table argument. Keys are **untagged atom indices supplied by the caller**;
this component neither interns names nor makes the global atom table private.
Invalid limits/indices and full-table insertion are rejected before mutation.
Effective capacity still follows the existing index-page rounding rules. Underlying
allocation failure remains fatal ERTS behavior, not recoverable host initialization.

`erts_index_destroy()` releases hash records/buckets, allocated index pages and
the segment vector. An independently allocated, unpublished module table can use
this to release its storage. Module instances now initialize their region/metadata
pointers as well as their existing fields, allowing conservative resource checks.
Discard refuses without freeing when current/old/on-load state contains code,
NIF, catch, metadata, region, breakpoint, traced-export or unsealed-state references.
Static diagnostic tables are not eligible for this destructor.

**Exclusive ownership is a precondition:** callers must retire all borrowed record
pointers before discarding an unpublished table. These raw internal pointers are
not generation-checked public handles. Resource checks do not prove absence of
external references and are not a code-retirement or concurrent-reclamation protocol.

## Evidence

Serialized under the shared validation lock, incremental macOS ARM64 debug emulator:

- `native_module_table_test.cpp`: two simultaneously allocated tables hold distinct
  records for the same atom index. Insertion is idempotent. One table grows across
  multiple index pages to capacity, rejects another new entry without mutation,
  and is physically discarded. The other retains its record/count. Sixteen fresh
  table replacements and empty-table disposal pass. No BEAM bytes are loaded and
  the prepared runtime retains zero processes/ports/system-process roots.
- `native_module_table_roots_test.c`: compiled with the configured emulator's own
  C flags/private headers and linked to the real archive. Twelve **synthetic**
  resource-marker cases exercise discard refusal, then clearing those markers
  permits disposal. These markers are never executed/dereferenced; this is not
  loaded-code reclamation evidence. The native startup runner now requires this
  witness as well as its existing host/bytecode checks.
- Six native CTests, one scaffold CTest, 29 tooling tests, three raw startup trials,
  four PTY cases and three CMake-linked startup trials pass. These startup trials
  exercise the diagnostic loader through the extracted component.
- Both two-isolate example builds still exit 1 at `Engine::create`; its assertions
  and fixtures have not been weakened. No successful public handle is fabricated.

Evidence:

- `/tmp/libbeam-module-tables-native/summary.json`: preserved compiler failure for
  an incorrectly named atomic integer type (`ErtsAint`); corrected to `erts_aint_t`.
- `/tmp/libbeam-module-tables-guards/summary.json`: final native build, twelve root
  guard cases, startup and PTY witnesses, source/link-input hashes.
- `/tmp/libbeam-module-tables-guards-package/manifest.json`: frozen native package.
- `/tmp/libbeam-module-tables-verified/summary.json`: final CMake/tooling/metadata
  component tests, CMake-linked startup trials and unchanged example failures.
  Tested native source hashes match the development tree.

No new whole-VM sanitizer, JIT, clean-bootstrap, OOM, latency or density claim.
Repeated metadata disposal is **not** the RFD's isolate churn/reclamation gate.
The enclosing diagnostic preparation still lives until process exit.

## Next

Bundle module-table roots with isolate-owned atom/export/fun/code-index state in
an explicit code-space context, then propagate that context through loader and
process execution. Old-code locks, staging coordination, unsealed-code tracking,
atom resolution, exports/imports/funs and code/literal retirement still use global
state and must move coherently. Do not put those world-specific tables in Engine,
select them through a current-world singleton, or boot a global OTP application
world to stand in for an isolate.
