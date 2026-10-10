<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 Leandro Ostera <leandro@ostera.io> -->

# Private atoms in isolate namespace state

Checkpoint at `d704a97e`. The subsequent [diagnostic-state migration](0002-diagnostic-namespace-state.md)
uses the same owned name storage for diagnostic atoms, removes the cached binary
representation, and adds module slots to namespace state. Details below describe
this earlier extraction checkpoint.

`ErtsIsolateNamespaceState` now owns a private atom namespace and the extracted
module metadata table, with explicit engine affinity. This is real allocated
namespace state, **not a runnable Isolate or a successful public API handle**.
The unchanged example still fails at `Engine::create`.

## Ownership and representation

- `erl_isolate_state.c/.h` constructs unpublished namespace state only against a
  prepared/started native engine. Borrowed child components belong to this state.
- `ErtsAtomNamespace` in `atom.c` owns its index/hash, reader/writer lock, UTF-8
  name bytes, exact admission limit and text accounting. Atom indices are local
  to this namespace; equal integers can name different atoms in different states.
- Each namespace seeds only the immutable predefined vocabulary at its required
  indices. It does not import dynamically interned diagnostic-world atoms.
- Private atom records own inline copied name bytes. They do **not** allocate or
  register names in the process-global literal registry. Hash/index destruction
  releases those records and bytes; the namespace lock is destroyed too.
- Validation, UTF-8 analysis, hashing, comparison and ordinal calculation are
  shared with the existing atom implementation. The private interface is UTF-8,
  non-truncating; malformed names, overlong names and full-table insertion are
  rejected without publication. Existing names remain retrievable at capacity.
- Name reads copy into caller storage; no name pointer escapes this interface.

The old `erts_atom_put_index`/`atom_tab` APIs still address the diagnostic global
world. Their adapter explicitly chooses that table; no current-isolate selector
or global-table swap has been introduced. The legacy allocator still uses global
literals. The private allocator has a different, reclaimable backing store.

`Atom.owned_name` distinguishes that storage from the legacy `u.bin` literal.
**Do not pass private indices/records through global term operations yet.** In
particular, direct `u.bin` users in `erl_unicode.c` and `erl_bif_binary.c` need an
ownership-aware binary conversion or genuinely shared immutable constant path.
Private atom-to-binary semantics, term lifetime, comparison through process context,
loader imports and execution have not been implemented.

## Cleanup boundary

Namespace-state disposal first checks/discards its unpublished module table. If
module resources remain, it leaves the parent and atoms alive. Otherwise it frees
the atom namespace and parent. Callers must exclusively own unpublished state and
release all borrowed pointers before disposal. These are not generation-checked
host handles and this is not concurrent code/process reclamation.

Namespace locks protect atom access, but there is no demonstrated multi-host-thread
lifecycle protocol. Component allocation still uses ERTS's fatal OOM semantics;
recoverable initialization/failure injection remain open. Engine preparation and
its remaining globals are still process-lifetime diagnostic infrastructure.

## Validation

Shared validation lock; incremental macOS ARM64 debug interpreter only:

- Two simultaneous namespace states preserve predefined atom indices while
  assigning different names the same first dynamic index.
- Each state owns a distinct module record keyed by that local index.
- Repeated interning is idempotent; caller buffer mutation does not change names.
- A 255-character/1020-byte UTF-8 name survives round-trip; invalid encoding,
  invalid buffers, excessive byte length, insufficient output capacity and an
  invalid index are rejected. An exact private capacity limit rejects new names
  while retaining existing ones.
- A synthetic module retention marker blocks parent disposal without losing atoms.
  After clearing it, disposing A leaves B's name and module record intact.
- Sixteen fresh namespace states start with only predefined atoms and empty module
  metadata, then can be discarded. Global atom count and text accounting do not
  change. This is namespace metadata testing, **not isolate application churn**.
- Six native CTests, one scaffold CTest, 29 tooling tests, twelve module-root guard
  cases, three raw startup trials, four PTY cases and three CMake-linked startup
  trials pass. Both example variants retain the expected factory failure.

Evidence:

- `/tmp/libbeam-atoms-native/summary.json`: preserved failure, private lock name
  missing from ERTS lock-order validation. Added `isolate_atom_tab` to the lock-order
  registry rather than disabling the checker.
- `/tmp/libbeam-atoms-native-lock-order/summary.json`: passing private atom-state,
  resource-guard, native startup and PTY witnesses, with source/link hashes.
- `/tmp/libbeam-atoms-package/manifest.json`: frozen native package.
- `/tmp/libbeam-atoms-verified/summary.json`: CMake/tooling tests, additional startup
  trials, example failures and matching tested/development native source hashes.

No new sanitizer, JIT, clean-bootstrap, OOM, untrusted safety or performance claim.

## Next

Add exports/funs and code-index/staging ownership to the same code-space context.
Propagate the explicit isolate context through loader and process execution, and
replace global atom resolution at those call sites. Audit direct atom binary/literal
uses before allowing private terms to execute. Do not move private namespaces into
Engine or make teardown of the old diagnostic OTP world a prerequisite.
