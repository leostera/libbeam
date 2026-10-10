<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 Leandro Ostera <leandro@ostera.io> -->

# Diagnostic atom/module roots use namespace state

First validated slice of the four-step diagnostic-world migration. **Exports/funs,
code-index coordination and contextual execution are not migrated by this change.**

## What changed

`erl_init.c` explicitly constructs and retains an `ErtsIsolateNamespaceState` for
the diagnostic host. That state owns its atom namespace and one module table per
`ERTS_NUM_CODE_IX` slot. Fresh unpublished states have the same storage layout and
constructors. The diagnostic state is bound to execution adapters and cannot use
the unpublished-state destructor.

The old `erts_atom_table` global data symbol is gone. Atom indices, locks, name
storage, limits, text accounting and optional put accounting live in the owned
atom namespace. `atom.c` retains only a fixed borrowed diagnostic binding for old
entry points; `module.c` likewise borrows the diagnostic state's module slots.
Those adapters are installed once during initialization. They do not select a
current tenant, use TLS to switch worlds, or swap global table contents.

The atom intern implementation no longer falls back to global storage for a NULL
owner. Diagnostic callers pass their explicitly bound namespace; fresh states
pass their own namespace. The interpreter/loader's remaining contextless calls
still use the diagnostic adapters, so this is **ownership migration, not contextual
execution**.

Diagnostic preparation now explicitly constructs diagnostic namespace storage.
It still loads no BEAM code, creates no init/system processes and launches no
workers. It is reference/test machinery, **not the implementation of public
Engine creation**; the Engine must not automatically create this diagnostic world.

## Atom names no longer rely on global literals

All atom names—including diagnostic atoms—use the same owned record/text allocation.
The atom-name global-literal allocation path and no-op atom free callback are removed. `Atom`
no longer contains a cached Erlang binary term or a private-versus-global name
representation discriminator.

That required fixing real consumers, not just relocating a table:

- `atom_to_binary/2` copies UTF-8/ASCII bytes into a process-owned binary; Latin-1
  conversion retains its existing conversion path.
- Binary splitting formerly borrowed the empty atom's cached binary. It now uses
  a dedicated immutable `ERTS_GLOBAL_LIT_EMPTY_BINARY`, alongside the existing
  shared empty tuple. This constant contains no namespace names or references.

These changes do not establish performance parity. They remove permanent atom-name
literal roots and avoid returning binaries borrowed from reclaimable atom storage.
Private execution still needs the correct atom owner supplied by its process.

## Disposal and remaining ownership

Unpublished state disposal preflights **all** module slots before freeing any slot
or atom storage. A retained resource in a later slot leaves earlier slots and
atoms intact. Bound diagnostic state refuses disposal; its adapters and running
processes are not retired by this work. All unpublished disposal still requires
exclusive ownership and no outstanding borrowed pointers.

Still global: export/fun/record staging storage, active/staging indices, code
permissions, old-code locks and coordination, catches/ranges and executable-code
retirement. `export.c` also creates shared lambda literals. Those dependencies
must move coherently, not simply wrap the two code-index integers in a struct.

## Validation

Shared validation lock, incremental macOS ARM64 debug interpreter:

- Native diagnostic bootstrap passes using state-owned atom/module roots.
- Real bytecode exercises UTF-8/unicode/Latin-1 atom-to-binary conversion, empty and
  embedded-NUL names, a 255-character long UTF-8 name, GC between conversions and
  binary splitting with empty segments. The host runner requires the new witness.
- Namespace-state tests retain different names at equal local indices, peer
  survival and fresh metadata-state disposal. A new late-module-slot retention
  case checks that parent disposal performs no partial destruction.
- Six native CTests, one scaffold CTest, 29 tooling tests, native namespace/root
  guards, three raw startup trials, four PTY cases and three CMake-linked startup
  trials pass. `nm` confirms no defined or unresolved `erts_atom_table` data symbol.
- Both unchanged two-isolate example builds still fail at `Engine::create`.

Evidence:

- `/tmp/libbeam-diagnostic-state-native/summary.json`: native build, component and
  bytecode witnesses, startup/PTY checks, source and link-input hashes.
- `/tmp/libbeam-diagnostic-state-package/manifest.json`: frozen native package.
- `/tmp/libbeam-diagnostic-state-verified/summary.json`: final CMake/tooling tests,
  additional host trials, symbol check, example failures and matching native hashes.

No initialized Engine shutdown, runnable private isolate, live-code reclamation,
OOM/partial-start recovery, sanitizer, JIT, clean-bootstrap or performance claim.

## Four-step status / next

1. **Atom/module roots:** diagnostic and fresh states now share owned storage.
2. **Exports/funs/code indices:** pending, including their literal and lock ownership.
3. **Explicit context propagation:** component access is explicit; loader/process
   execution still needs propagation. Fixed diagnostic adapters are a migration
   boundary, not the final VM lookup contract.
4. **Diagnostic construction:** explicit for atom/module state; extend it as the
   remaining code-space components migrate.

Next: extract export/fun staging ownership together with code-index transactions,
then test independent staging/commit/abort before admitting private execution.
