<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 Leandro Ostera <leandro@ostera.io> -->

# A01/A02: BEAM term representation and owner-local atoms

Admission record for the next [inventory](0003-additive-runtime-inventory.md) slice.
Baseline: `5d5d8c92`; upstream baseline remains OTP
`cca4e72510a97cfca6427602d3da8a22d5ff7a33`. Earlier uncommitted lifecycle changes are
not inputs to this transplant and must remain untouched.

## Boundary selected before implementation

Preserve the actual BEAM word/tag representation, not a parallel term object model.
Extract its immediate, pointer, tuple/list and header definitions into a small C
platform boundary. Initially require flat 64-bit pointers/words, 8-bit bytes,
two's-complement signed words and arithmetic signed right shift. Fail compilation
for unsupported layouts rather than silently choosing a different ABI. Keep the
64-bit non-reservation literal-pointer tag; do not import mmap or OS pre-init.

Implement namespace-owned atom identities with stable indices and copied UTF-8
names. Preserve OTP's hashpjw (including the Latin-1 clutch), Fibonacci bucket
selection and separate chaining. Index storage and insertion transactions are
adapted for explicit allocation domains and recoverable failure. This is not an
attempt to expose the old global atom table through C++ wrappers.

Predefined atom names/aliases/indices come from the unchanged upstream
`make_tables` generator and admitted atom/BIF declaration inputs. BIF declarations
are **naming data only**, not imported builtin implementations or a capability
list. Only the atom outputs are compiled; generated BIF wrappers are not admitted.
All generated files belong in the build tree. Two exact-path whitespace-check
exceptions preserve the original trailing spaces in `atom.names` and `make_tables`;
input hashes, rather than whitespace normalization, guard their provenance.

Bind existing owned BEAM images to these real atom words. File atom zero means
NIL; runtime atom zero is a real predefined atom (`false`). Binding must retain
its namespace, survive image destruction, reject a mismatched expected owner,
and unwind all new atoms and backing on failure without changing prior contents.
No operation uses a current-namespace global/TLS selector.

## Provenance

Paths below are under `beam/erts/emulator/`:

| Input | SHA-256 |
|---|---|
| `beam/erl_term.h` | `8e016126fb7a1bc1db8d31111601c0bea21d72207211bf91e1f3c127e943b5c2` |
| `beam/erl_vm.h` | `5fc1308a52c46629307cbe0fa085ca561ba1244b0252de1a59b1a35c53d8d02b` |
| `beam/sys.h` | `8b14fbf3924e700ec613bdaa0c23b7ec560e95e4371e32422d640ef8dc12c438` |
| `beam/atom.c` | `7d73c9f537444aa725c4dad64bce65460a7282fb0d3de4d2917f9f9cabd5a6d5` |
| `beam/hash.c` | `8a90d388e20ad89f981b9a38731459be3ea82efaf92f3347e9af702f94d5e40c` |
| `beam/hash.h` | `9d4523310ac9362320b463cc48cefe28e6f5a46acdacd80229352a8252afbe22` |
| `beam/index.c` | `7398720c4a8523fbe6a4c9fc644a1255371cecf8614f0aad9ef0c09ec85c649d` |
| `beam/atom.names` | `4e6da90ce1effb6d922e01da32dcefd399366fb287b13d04924504d90e4fd309` |
| `beam/bif.tab` | `39f32210feb58f9c60482d7b97db10d21c53a988207fd7a81221ff3506331564` |
| `utils/make_tables` | `4503d68bd345492db8127aad8c3ce584319a8355e3e1dafbbed2b680eb20d953` |

`atom.c`, `sys.h` and `bif.tab` include prior repository adaptations; these hashes
identify the actual inputs, not a claim that every input is pristine upstream.
The atom hash and term definitions are extracted; the generic hash/index APIs are
not copied wholesale. Retain upstream attribution in adapted C code and copied
inputs. New allocation/transaction/lease plumbing is libbeam code.

## Lifecycle and semantic constraints

- Constructor failure leaves no published namespace. Successful insertion assigns
  stable indices; table growth never invalidates name storage or existing terms.
- Batch/image binding prepares all fallible work before committing. Failure must
  not leave newly interned names, changed counts or enlarged backing behind.
- Atom words are namespace-relative, as required by unchanged BEAM tags. The same
  dynamic word can name different atoms in two worlds. A raw Eterm is not a
  cross-world capability or a provenance-bearing host value. Process/code/binding
  ownership must supply and validate the context; arbitrary cross-world raw term
  transfer is forbidden.
- Destruction refuses live bindings/borrowers. Raw borrowed names/terms require
  their owner to remain alive; there is no tracing of arbitrary host C pointers.
- Term pointer helpers assume valid owned/aligned storage. Tag checks are not a
  validator for arbitrary machine addresses or proof of memory safety.
- Empty tuples require two words of backing because of BEAM's read-ahead invariant.
  No process-global `TUPLE0` object is introduced. An explicit owner will supply
  canonical literal storage when the literal/context cluster is admitted.

## Implementation and evidence

- `term.h` extracts the admitted BEAM tags/access conventions. Debug/release
  `THE_NON_VALUE` follows the corresponding upstream representation using the C
  build's `NDEBUG` setting; consumers of this internal header must use the same
  profile. It is not a stable public ABI or a general platform port.
- `atoms.c` owns copied names, hash buckets, a stable-index directory, and leases.
  The index directory is an adapted contiguous vector, not a wholesale port of
  OTP's segmented index implementation. Growth is preflighted with the staged
  names, and no allocation remains after the commit point. Existing entry/name
  addresses remain stable. No performance equivalence is claimed.
- `LbAtomBinding` is a real consumer of both the owned-image and atom-table APIs.
  It holds a namespace lease; destroying its image does not invalidate its terms.
  Binding checks an expected owner before returning namespace-relative terms.
- Shared UTF-8 validation was factored from the first image slice; no second
  encoding policy or global interning path was introduced.
- Build outputs come from the admitted OTP `make_tables` plus a narrow projection
  to private immutable names/aliases. The full predefined sequence is checked
  against namespace lookup. Merely admitting BIF naming data grants no effects.

The representation oracle compiles exact macro expressions extracted from the
pinned `erl_term.h` for the selected 64-bit/non-reservation profile and compares
results with the core: header tags, NIL/atoms, signed small boundaries, tuple/list
words and literal-pointer masking. This is a macro-subset comparison, not execution
of the full upstream term implementation or an ABI claim for unsupported layouts.

Tests inject failure at every namespace-construction allocation and every allocation
in a 1,600-name binding transaction that crosses both index and bucket growth.
They require identical live allocation counts/bytes and original namespace contents
after each failure, then successful retry. Additional checks cover partial atom-limit
rollback, duplicate file names at the limit, stable strings through growth, two
independent namespaces, wrong-owner binding refusal, leases and image destruction.

Reviewed validation: `/tmp/libbeam-additive-terms-atoms-reviewed/summary.json`.
Four CTests passed in both debug and release, plus real compiler-image binding in
both configurations and under UBSan. The macro-subset oracle matched in both
configurations; independently generated atom outputs were byte-identical. All 29
existing tooling tests passed. Sources were hash-checked before/after validation;
checks used fresh external outputs and the user-wide lock. Earlier first/expanded
runs remain under `/tmp/libbeam-additive-terms-atoms-{first,expanded}/`.

The compiler fixture/reference remains stock OTP 28 / ERTS 16.3, not the new core
executing Erlang or a pristine OTP 30 build. No ASan result is claimed for this
pass; the prior image pass's ASan/empty-control host timeout remains documented.
The updated Linux CI configuration has not been run in this session.
`engine_lifecycle` is still observed failing at the unimplemented public factory.

## Scope not to overclaim

This is the A01 platform/representation boundary and a usable A02 atom/basic-term
slice. Full term comparison/copy/hash, bignum/float/binary/fun/map layouts, process
heaps, moving GC, executable code and the public Engine are not completed by it.
Opcode generation (A03) is not implemented merely by reusing the atom generator.
Run component failure/churn tests and real image-to-atom binding before advancing;
keep execution and lifecycle acceptance red until their real dependencies exist.
