<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 Leandro Ostera <leandro@ostera.io> -->

# RFD 0003 — Opcode generation, operands, and owned literal preparation

Baseline: `297c896f`. This is a checkpoint in the requested A03–A07 pass, **not
completion of all five clusters**. A03 generation and the A04 preparation path now
connect to a supported A05 literal/metadata subset. A06 has specific-instruction
selection only; transformation/emission and A07 executable publication remain
outstanding. No Engine factory, host API or acceptance example was changed.

This records the preparation checkpoint at `1c47d6c3`, not the current execution
status. The [first-execution record](0003-first-execution.md) connects its output to
real selected-profile transformation, emission, publication and interpreter/GC
consumers. Full A03–A07 breadth still remains incomplete.

## Admitted C boundary

`lb_beam_program_prepare(domain, atoms, bytes, size, ...)` produces an owned,
**non-executable generic-operation program**. It copies the input through the
existing image parser, materializes supported literals into real BEAM term layouts,
parses type/lambda metadata, reads compact operands, synthesizes function boundaries,
and binds all file/ETF atoms in one final namespace transaction.

The operations are adaptations of OTP's `BeamOp`/`BeamOpArg`, not another bytecode
format or an interpreter. There is no dispatch loop, process, heap collection,
implicit OTP service, BIF call, import resolution, executable entry or publication.
A successfully decoded program is not evidence that arbitrary bytecode is safe to
execute. Operand signatures, import indices, register liveness, stack effects,
transform-specific constraints and code fixups still require their actual loader
and execution consumers.

All fallible preparation precedes atom admission. Name destinations are stable
private slots; until commit they contain no published namespace terms. The last
operation with failure potential is the atom batch. On failure, namespace contents,
indices and backing are unchanged, and all program/image/arena allocations are
released. On success a real namespace lease lasts until after the program's terms,
operations and image are physically freed. Bare term/op views borrow that lifetime.
A peer program and atom table survive independent destruction.

The preparation arena uses the existing allocation domain, not a process GC or an
Isolate allocator. Its allocations, including temporary decoding/name plans, remain
bounded and owned until program destruction. They will need retirement or relocation
when an actual emitter consumes the program; no claim of optimal retained size is
made. A 64 MiB arena cap is separate from the image's 64 MiB input cap and zlib's
bounded inflation workspace. These are parser limits, not guest quotas.

## Provenance and generation (A03)

- [`core/otp/opcodes/manifest.json`](../../libbeam/core/otp/opcodes/manifest.json)
  pins the exact `beam_makeops` checksum, ordered `genop.tab`, common tables and
  complete interpreter tables. These are snapshot inputs, including prior libbeam
  modifications, not a claim of pristine upstream content.
- `tools/otp/beam_makeops` and all `.tab` inputs are copied byte-for-byte. The build
  driver verifies hashes and invokes the unchanged generator with `-wordsize 64`,
  `-jit no`, `-DUSE_VM_PROBES=0`, `-emulator`. The input ordering follows the snapshot
  Makefile. This chooses generator data, **not an alternate execution mode**.
- All five full outputs (`beam_opcodes.c/.h`, `beam_hot.h`, `beam_warm.h`,
  `beam_cold.h`) are retained in the external build tree and hashed. They are not
  compiled through invented ERTS compatibility functions.
- A narrow projection compiles the exact generated generic/specific metadata and
  tag array. Opcode numbers, arities, masks, signature/packing descriptions and
  ordering are not maintained independently. Public-internal symbol prefixes
  avoid collisions with the frozen diagnostic archive.
- Header tags/instruction families do not admit their execution or native effects.
  Python/Perl remain build tools only; the runtime never invokes a compiler.

## Operand path (A04)

`beam_reader.c` adapts the tagged-number reader in `beam_file.c`:

- one-byte, eleven-bit and long encodings; signed integer extension and overflow
  classification; bounded recursive size prefixes;
- unsigned arithmetic for sign extension instead of the original signed shifts;
- values too large for a word remain byte views until integer marshalling;
- non-integer overflow becomes the upstream valueless `TAG_o`.

`beam_decode.c` adapts operation-block allocation, integer/allocation-list
marshalling, operand conversion and function-boundary synthesis:

- atoms/NIL, failure labels, integer/dynamic literals, x/y/float registers;
- extended operand lists with fallible growth; static literal indices; type hints
  with upstream ANY fallback when stripped/unknown-version type data is present;
- selected 64-bit allocation-list sizes (word/float/fun/record), not fabricated
  fun or record implementations;
- real `int_func_start/5` and `int_func_end/2`, including optional line locations;
- label/function/export/lambda boundary validation and complete byte consumption.

The adaptation processes an owned decoded list rather than the upstream streaming
pending queue. Operation blocks and expanded argument arrays are arena-owned.
Unused boundary-source operations retire with that arena. Additional validation
rejects duplicate/undefined labels, mistyped entry labels/MFAs and mismatched
export/lambda entries. Register bounds apply inside type-hint encodings too.

The argument cap is 65,536 per operation, and nested size-prefix depth is bounded
at 16. These are explicit preparation limits. No numeric wrapping is substituted
for bignum construction. Dynamic integer literal indices remain negative (`~index`)
as in upstream; static literals use nonnegative indices.

## Literal and metadata path (A05 subset)

The full source checksum record is
[`loader-sources.json`](../../libbeam/core/otp/loader-sources.json). It records both
admitted algorithms/layouts and reference-only consumers still outstanding.

- `external.c:dec_term`: selected decoding cases preserve its intrusive pending-term
  chain, avoiding recursive C descent. Tuple/list construction and signed/unsigned
  term layouts follow the source. Atom publication is deferred; no process-global
  atom cache or heap factory is imported.
- `big.c:bytes_to_big`: little-endian magnitude packing, small promotion and real
  word-digit bignums. `marshal_integer` converts compact two's-complement payloads.
  Bignums retain the selected upstream 65,535-digit bound.
- `erl_bits.h`: genuine on-heap bitstring layout, **at most 64 bytes**. Larger
  binaries return `unsupported`; no fake BinRef/offheap reference counter exists.
- `beam_file.c`: raw OTP 28+ or compressed literal tables, strict declared lengths,
  typed ETF entries, owned static/dynamic literals. zlib uses explicit domain-backed
  allocation callbacks; every allocation/failure prefix is tested. Inflation must
  consume exactly its payload and produce exactly the declared bounded length.
- `beam_file.c` / `beam_types.c`: type versions 3 and 4, min/max/unit data and ANY
  fallback. Signed bounds are read with unsigned shifts; the owned unit field can
  represent 256 instead of wrapping in the source's byte-sized field.
- `beam_file.c:parse_lambda_chunk`: function, arity, label, index, free-variable
  count and old uniqueness data. These are lambda **declarations**, not closures
  or entries registered into a running fun table.

Admitted ETF forms: small/integer/bignum, Latin-1 or UTF-8 atom, tuple, proper or
improper list, string, NIL, finite binary64 float, on-heap binary and bit binary.
Zero-sized tuples are owned two-word objects, not an implicit global singleton.
Boxed/list literal pointers carry the selected BEAM literal bit. No GC/offheap
integration is claimed for these preparation objects.

Explicit refusals include maps, legacy textual floats, ETF-within-ETF compression,
large/offheap binaries, external/local fun ETF terms, pids, ports, refs, atom caches
and distribution encodings. Rejecting them is not conformance for those forms.

**Optional metadata policy:** `Line`, debug/attributes/compile chunks remain opaque
in the owned image. This is an explicit no-line-info preparation policy, not fake
source locations. Unknown `Type` versions use the source's fallback. Known type
and lambda tables are fully consumed and validated. Executable admission must still
reject any operation requiring unimplemented record/debug/native capabilities.

## Emission and publication boundary (A06/A07)

`beam_select.c` ports `beam_load.c:load_code`'s mask-based specific selection,
including the x(0)/`r` operand rule and generated candidate ordering. It returns a
rewrite mask without modifying its input. This primitive requires a
transformation-complete operation. Like upstream, the single-candidate path does
**not** validate its signature; that belongs to the emitter.

It is not enough to emit or publish code. The next integration must include:

1. Generated transformation bodies and required predicate/generator helpers with
   explicit loader ownership and fallible operation creation. They currently call
   into real BIF/import metadata, literals, labels, maps, binaries and records.
   Returning false/success from missing helpers would silently change semantics.
2. `emu_load.c:beam_load_emit_op` signature checking, packing, varargs, label/import/
   literal/string/catch/fun fixups and function layout; `finish_emit` relocation.
3. A real dispatch-address/profile decision with the transplanted interpreter,
   builtin/export resolution and error behavior. No address-shaped dummy handlers
   or silently ignored generated `module_info` imports.
4. Only then an Isolate-owned code publication transaction and entry lookup whose
   frames/funs/literals retain code until physical retirement.

No new unused code-store wrapper was added as A07 evidence. The absence of A06
means A07 executable publication remains unimplemented. Completing these two may
require bringing in their A08–A11 process/interpreter/builtin consumers as the
inventory's coupled execution cluster, rather than marking five isolated wrappers
complete.

## Validation scope

The serialized runner now compiles fresh `first_slice.erl` and both conflicting
`probe` variants. It compares function boundaries with stock `beam_disasm`, tests
real term/lambda/type preparation and every allocation-failure prefix for each
fixture, then repeats in Release and under UBSan. The reference remains installed
OTP 28/ERTS 16.3, not libbeam execution or a freshly pinned OTP 30 compiler.

Observed first fixture: 7 functions, 37 generic/synthetic operations, 1 real
literal. Each stateful probe: 6 functions, 190 operations, 6 literals, 1 lambda.
These counts describe decoded preparation, not executed functions or processes.

Synthetic tests cover signed/overflow compact numbers, depth bounds, exact native
selection, bignum layout, literal shapes, malformed/truncated ETF, compressed
payloads, mutations, atom limits, rollback/retry, peer survival and lifetime guards.
Fault checks require exact allocation counts/bytes and unchanged atom state on
failure. Generated outputs are compared across independent Debug/Release builds.
The public `engine_lifecycle` remains red and neither acceptance example is changed.

Reviewed evidence: `/tmp/libbeam-loader-program-reviewed-fixed/summary.json`.
Five CTests passed in Debug and Release; all three freshly compiled fixtures
prepared in both configurations and under UBSan, including allocation-failure
injection. Full generator outputs were byte-identical across builds. All 29
existing tooling tests passed; the lifecycle target was observed returning 1.
Sources were hash-checked before/after the fresh, user-lock-serialized runs.
The new Linux CI dependency/configuration has not been executed in this session.

Retained failures:

- `/tmp/libbeam-loader-program-expanded/build.log`: incorrect test assumptions
  about generated specific-op identifier spelling; corrected against generated data.
- `/tmp/libbeam-loader-program-reviewed/ctest.log`: a valid empty ETF tuple hit
  `make_arityval(0)`'s debug assertion. The decoder now uses the actual zero-arity
  constructor and retains the two-word backing. The added literal-shape test
  remains a regression witness, in Debug, Release and UBSan.

Earlier successful first/expanded runs remain at
`/tmp/libbeam-loader-program-first/` and
`/tmp/libbeam-loader-program-expanded-fixed/`.
The prior ASan/empty-control timeout is not treated as a pass; this pass makes no
ASan, security, GC, platform-portability or performance claim.
