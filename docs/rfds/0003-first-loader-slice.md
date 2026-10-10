<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 Leandro Ostera <leandro@ostera.io> -->

# First additive slice: owned BEAM image preparation

Parent decision: [RFD 0003](0003-additive-runtime-construction.md).

## Admission record, written before the transplant

The first executable fixture is a normal compiler-produced module with `value/0`,
`identity/1` and `pair/1`. The eventual execution cluster needs term representation,
process heap/GC, atom identities, instruction decoding/transformation, interpreter
registers/continuations, and import resolution. Returning an integer through a
hand-written special executor would not establish that cluster.

This pass admits its first real boundary: **owned BEAM file/metadata preparation**.
It is deliberately called an image, not a loaded executable module. It must consume
ordinary compiler output, own its source bytes, preserve unknown chunks, reject
malformed structural metadata, recover from every allocation failure, and release
all storage independently of peer images. It does not run instructions, intern
runtime atoms, resolve imports, decode ETF literals, or validate instruction bodies.
No public Engine success is authorized by this boundary.

## Provenance

Upstream baseline: Erlang/OTP `cca4e72510a97cfca6427602d3da8a22d5ff7a33`
(OTP 30.0-rc0). Source is the tracked snapshot at repository HEAD `a2d905b5`.
These input files have no local uncommitted modifications:

| Original upstream path (under `erts/emulator/beam/`) | SHA-256 of local `beam/` source |
|---|---|
| `beam_file.c` | `52708b4a8fa136d005d962599914b095e01b7fc311c1748309d3682d4ff56d0d` |
| `beam_file.h` | `82a4760ac4c3b69d385a26c4c004103ca996960621d5f9bb4f62529e3b4f3718` |

Format constants additionally reference `beam/lib/compiler/src/genop.tab`
(`BEAM_FORMAT_NUMBER=0`, SHA-256
`0f999b3797672f28bfbd4007245a95637bd38b68ac67dcfcb95eacce29e0780e`) and
`beam/erts/emulator/beam/erl_vm.h` (`MAX_ARG=255`, SHA-256
`5fc1308a52c46629307cbe0fa085ca561ba1244b0252de1a59b1a35c53d8d02b`).
The validation runner checks all four source hashes before building.

Adapted source families: `BeamReader`, the unsigned-length branch of
`beamreader_read_tagged`, `iff_init`, `read_beam_chunks`,
`parse_code_chunk`, `parse_atom_chunk`, `parse_import_chunk`, `parse_export_table`.
Retain Ericsson's license/attribution in the adapted implementation. The C API,
owner plumbing, strict validation and rollback are new libbeam work, not verbatim
upstream code. This is a bounded extraction/refactor, not a whole-file transplant.

## Dependency disposition

| Dependency | Decision |
|---|---|
| Big-endian reader, IFF tags/alignment, table layouts | Port into C core; immutable format definitions |
| `erts_alloc` / fatal allocation | Explicit `LbAllocDomain`; each failure returns and unwinds |
| Caller binary and chunk pointers | Copy into image-owned storage; returned views borrow that image |
| `erts_atom_put` / global atom table | Exclude; retain validated UTF-8 name views indexed by file atom number |
| Module/import/export atom `Eterm`s | File indices only; not runtime identities or resolved imports |
| Code-header values | Preserve and inspect; bytecode remains opaque, not executable |
| `Process`, global constants, thread library, TP, locks | Not needed at this boundary; do not import |
| ETF, compression, lambda/type/debug/record tables | Preserve opaque chunks, no semantic decoding in this slice |
| File system / compiler subprocess | Test host/tooling only; core accepts bytes and has no I/O |

## Deliberate validation differences

Use unsigned big-endian assembly and checked sizes rather than signed shifts or
signed alignment arithmetic. Require the complete declared IFF form (no trailing
bytes), a BEAM form, and unique required chunks. Unknown chunks are retained;
lookup keeps upstream's last-match behavior for repeated optional chunk IDs.
Require exact atom/import/export table consumption, valid nonzero atom indices,
export labels within the declared code-label range, and arities at most 255.
Upstream is more permissive at some of these parsing boundaries; later stages do
additional validation. These are documented stricter structural checks, not a new
file format.

Support both ordinary one-byte atom lengths and OTP 30's negative-count compact
length encoding. Validate UTF-8/255-codepoint atom limits without creating global
atoms. Unsigned compact length decoding is bounded and does not recursively decode
arbitrary nested operands. It is not a substitute for the full code operand reader.
Legacy Latin-1 `Atom` tables and nonzero code format versions return unsupported.
A 64 MiB input ceiling bounds this initial parser; it is not an Isolate quota.

## Lifetime contract

Creation publishes only a fully constructed image. Failure leaves the output NULL,
releases every allocation made by the attempt and preserves existing images.
Destruction releases tables, copied bytes and control storage through the same
allocation domain. Domains refuse destruction while images remain alive. Callers
must drop borrowed byte/name views before image destruction; there are no queued
users or execution references yet. Operations are serialized by the control caller.

## Required evidence

- Fresh `erlc` output, inspected by both the core and stock OTP `beam_lib`; compare
  module/atom/import/export/code-header metadata. Reference OTP is not our executor.
- Source-buffer mutation/free after image creation; exact copied bytes survive.
- Two domains containing same-named images, independent release/replacement.
- Every allocation-failure prefix, retry in the same process, exact host allocation
  balance, and busy-domain/wrong-owner refusal.
- All truncated prefixes; malformed headers, chunk sizes/padding/counts/indices;
  ordinary and compact atom lengths; UTF-8 boundaries; opaque chunk preservation.
- Mutation stress with cleanup for both accepted and rejected inputs, UBSan, and
  fresh serialized external builds with source hashes. No generated BEAM commits.

## Implemented and verified

Implementation: `libbeam/core/beam_image.c/.h`, with the standalone image test and
`libbeam/tools/run_additive_slice.py`. It links only the additive core, not the
ERTS archive. Undefined-symbol inspection found no ERTS/ethread/pthread dependency.
The public C++ factory and both acceptance examples are unchanged by this slice.

Reviewed evidence: `/tmp/libbeam-additive-image-reviewed/summary.json` and its
adjacent logs. Fresh debug and release builds each passed three CTests. The fresh
compiler fixture's atom/import/export metadata matched stock `beam_lib`; the code
header matched its source bytes. The fixture also passed same-process source-copy,
allocation-failure/retry, peer-survival and reclamation checks under UBSan.
All 29 existing tooling tests passed. Source hashes were checked again after the
validation commands to detect edits during the run. The source-check CI workflow
now also builds/runs the standalone components in debug and release without an
ERTS package; that new Linux CI configuration has not been executed in this session.

The installed reference was **OTP 28 / ERTS 16.3**, not a rebuilt pristine OTP 30.
The negative-count atom encoding was exercised by structural unit inputs, not
claimed as a fresh OTP 30 compiler run. Synthetic metadata inputs are not execution
witnesses. The fixture's reference execution is explicitly labeled stock OTP, not
libbeam execution.

ASan is **not counted as passing**: both the instrumented image test and an
empty-main control timed out after 30 seconds on this host. The retained diagnostic
is `/var/folders/v0/6x4x9vzn10gbdxpzdnsfyxwh0000gn/T/libbeam-image-asan-zo5gmtr1/`.
UBSan and exact allocator bookkeeping do not replace ASan or prove memory safety.

`engine_lifecycle` still exits 1 at the unimplemented factory. Its result is observed
separately, not converted into an expected-failure acceptance test. Neither Engine
lifecycle nor Erlang execution has passed. No claims are made about JIT, other
platforms, security, resource quotas, or performance.

## Execution dependency cluster still to admit

The [additive runtime inventory](0003-additive-runtime-inventory.md) expands this
cluster into source anchors, ownership/retirement obligations, actual fixture
requirements and the proposed next implementation pass.

| Upstream anchors under `beam/erts/emulator/beam/` | Boundary that remains |
|---|---|
| `beam_file.c:beamfile_get_code`, code reader and `BeamOpAllocator` | Decode operands against real atoms/literals/types; retain prepared-code storage |
| `beam_load.c`, `emu/emu_load.c`, `emu/load.h` | Transform generic instructions, construct executable code, fix labels/imports and retire preparation storage |
| `erl_term.h`, `erl_binary.h`, `erl_gc.c` | Preserve tags, heap layout, binary/off-heap lifetimes, roots and moving collection |
| `erl_process.h`, interpreter process/context access | Owned registers, stack/continuations, reductions and execution entry/exit without global scheduler bootstrap |
| `emu/beam_emu.c`, `emu/instrs.tab`, `emu/macros.tab`, generated opcode tables | Actual interpreter dispatch and instruction behavior; no replacement special-case loop |
| Atom/export/code namespace access | Real file-to-runtime identity resolution with explicit Isolate context |

These are a coupled execution cluster, not permission to reproduce the previous
whole-world initialization graph. The next pass should follow the code-reader into
these dependencies and admit what the real instruction path needs.

Next dependency boundary: actual file atom identities and the upstream code-reader /
BeamOp transformation path, coupled to term/process/GC and interpreter requirements.
Do not grow a metadata-only framework indefinitely or wire this image into a fake
successful Engine factory. Execution and Engine lifecycle remain separate red gates.
