<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 Leandro Ostera <leandro@ostera.io> -->

# Namespace-owned external-fun literals

Checkpoint at `90fa55c2`. [Owned export tables](0002-owned-export-tables.md)
supersedes the global-table limitation below; full code-space transactions and
private execution remain unfinished.

Export ownership migration begins with a lifetime dependency: each export entry
previously allocated its external-fun literal in the process-global literal arena.
Moving its table alone would leave that permanent root outside the isolate owner.

`ErtsExportLiterals` now belongs to `ErtsIsolateNamespaceState`. It owns a synchronized
list of real ERTS literal areas, allocated with `ERTS_ALC_T_LITERAL`, initialized
with external-fun objects and tagged using `erts_set_literal_tag`. Diagnostic
`init_export_table` explicitly receives and binds this owner. `export.c` no longer
allocates/registers export lambdas in the global literal arena.

This preserves the runtime's existing literal representation: it does not return
ordinary heap pointers while pretending they are immortal literals. An unpublished,
exclusively owned pool can release its areas through `erts_release_literal_area`.
Once bound for execution, disposal refuses and retains ownership. There is no
unbind shortcut or claimed live-term retirement protocol.

Parent namespace disposal checks literal ownership before releasing module tables
or atoms. Borrowed export descriptors and terms must not outlive unpublished state.
The initial implementation uses one area per literal; memory efficiency, batching
and performance are not established.

## Scope

**Export tables are still global staged tables.** Their dispatch entries, locking,
index selection and transactional access must become context-aware next. The
fixed diagnostic literal-owner binding is not a current-isolate selector and is
not a replacement for that migration. Local-fun tables, code-index coordination,
process ownership, runnable private isolates and Engine shutdown remain pending.

## Validation

Shared lock; incremental macOS ARM64 debug interpreter:

- Two namespace owners allocate distinct external-fun literal objects, carrying
  the expected export-descriptor references. One owner grows to 257 areas and is
  discarded; the other's literal remains valid. Sixteen fresh empty owners are
  reclaimed. Bound pool/parent disposal refuses without dropping other state.
  Test-local export descriptors are never dispatched: this is storage evidence,
  not private code execution.
- Real diagnostic bytecode tests external-fun lookup, BIF and module dispatch,
  serialization round-trip, GC, fun metadata and transfer to another process.
  The host runner requires its success marker.
- Existing namespace/root guards, six native CTests, one scaffold CTest, 29 tooling
  tests, three raw host trials, four PTY cases and three CMake-linked host trials
  pass. The unchanged two-isolate example still fails at `Engine::create`.

Evidence:

- `/tmp/libbeam-export-literals-native/summary.json`: preserved test compile failure
  from incorrect term macros/types; corrected to `is_any_fun` and `ErlFunThing *`.
- `/tmp/libbeam-export-literals-native-fixed/summary.json`: preserved count failure
  exposing multiple evaluation of an allocating expression inside a term macro.
  The fixture now stores the expression result before testing it.
- `/tmp/libbeam-export-literals-native-final/summary.json`: passing native build,
  literal/component witnesses, real bytecode, startup/PTY and source/link hashes.
- `/tmp/libbeam-export-literals-package/manifest.json`: frozen native package.
- `/tmp/libbeam-export-literals-verified/summary.json`: final CMake/tooling and
  additional host checks; tested native sources match development source hashes.

No full migration, live literal/code reclamation, crash-dump integration audit,
OOM recovery, sanitizer, JIT, clean-bootstrap, latency or density claim.
