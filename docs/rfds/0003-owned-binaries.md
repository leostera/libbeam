<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 Leandro Ostera <leandro@ostera.io> -->

# Ordinary offheap binaries through loading, execution and retirement

Baseline: `c08f1e82`. This extends the [first-execution profile](0003-first-execution.md)
under [RFD 0003](0003-additive-runtime-construction.md). It connects the A05/A09
binary dependency to real interpreter and metadata-copy consumers and adds a
copied byte-input boundary needed by A12/B09. It does **not** complete those
clusters, Engine lifecycle, bit-syntax instruction coverage, or the stateful example.

## Source boundary and layouts

`libbeam/core/otp/binary-sources.json` pins the actual modified OTP snapshot inputs:
`erl_binary.h`, `erl_bits.h/.c`, `erl_gc.h/.c`, `copy.c`, `erl_message.h/.c`,
`external.c` and `erl_term.h`. Existing upstream notices remain in the adapted C
sources; no whole-source-file admission is implied.

The admitted paths are ordinary binary allocation, `erl_sub_bits_init`,
`erts_wrap_refc_bitstring`, `copy_struct`'s paired SubBits/BinRef copy, the fullsweep
portion of `sweep_off_heap`, and binary-reference offheap cleanup. These use:

- The same 64-byte on-heap threshold and heap-binary term layout.
- The actual three-word BinRef and five-word SubBits shapes and header tags.
- The native binary header's flags/apparent-size/reference-count/size words and
  aligned payload offset. A C flexible array replaces the old one-byte trailing
  array; allocation remains header-offset plus payload, not `sizeof(Binary)`.
- A real word-sized C11 atomic reference counter rather than ethread startup.
  This primitive does **not** make allocation domains or control APIs concurrent.
- An aligned ownership prefix before the binary header. Final release returns the
  exact allocation to its recorded domain; no process-global binary allocator or
  singleton cache is introduced. The domain refuses destruction while it is live.

`check_binary_representation.py` extracts the pinned declarations/macros and
compares their offsets, headers, threshold and factory word sizes in Debug/Release.
It explicitly fixes the reference-count primitive to a word-sized atomic for this
flat-64 profile. This is not a claim about every ethread ABI, magic binary, driver,
NIF resource, writable binary or match-context configuration.

## Actual ownership flow

### Loader

ETF `BINARY_EXT` and `BIT_BINARY_EXT` no longer fail solely because their payload
exceeds 64 bytes. Small values retain real heap-binary storage. Larger values get
an owned binary allocation plus a BinRef and SubBits in the preparation arena.
Both outer and underlying literal pointers carry the existing literal bit.

Literal and Attr/CInf decoding use the same construction primitive. The program's
existing aggregate arena budget now also charges native binary allocation sizes.
Payload length, trailing-bit count and remaining input are checked before use;
unused trailing bits are masked as before. Construction never exposes a partial
term or chain link. Abort and destruction release native references **before**
freeing the arena that contains the chain nodes. Atom commit remains after all
fallible loading/linking, with the existing rollback and busy guards intact.

The 64 MiB core binary/arena limits are implementation bounds, not the public
64 KiB call/result limit and not a guest memory quota.

### Process roots and collection

A process has an actual offheap chain. During copying GC, SubBits' `orig` is a
traced heap reference, not an opaque word. Its BinRef moves exactly once through
the existing forwarding mechanism. Immutable offheap data stays at its allocated
address. After the root/heap scan, fullsweep rewires surviving chain nodes to their
new addresses and decrements dead references before releasing the old heap.
Moving a live reference does **not** increment its counter.

There is no fallible allocation after the first forwarding write. Failed tospace
allocation preserves old roots, chain links and reference counts; retry is safe.
Process destruction also clears the chain before freeing its heap. These paths
handle duplicated tuple roots without double-retention or double-release.

### Metadata copies and source retirement

A flat metadata copy builds each SubBits/BinRef pair together, increments the
payload's real counter and adds the new BinRef to the destination chain. It does
not leave a process term pointing into the source module's literal arena. Checked
counter/overhead exhaustion rolls back the new chain prefix and heap top. The two
metadata BIFs propagate failure and undo any earlier fields from the same result.

An executable witness invokes `code_peer:metadata(binary_slice)`, physically
unloads `binary_slice` while the peer process keeps its copied binary, collects
that process, loads a replacement module, and exercises both old and new results.
The old payload remains until its last process reference dies; replacement storage
is distinct. This is binary lifetime evidence, not public Isolate reclamation.
Raw literal results still require their actual module/code lease as before.

### Copied byte input

`lb_process_create_binary` constructs an arity-one invocation using the real entry,
process heap and ordinary binary representation. It copies the caller's bytes before
returning, unwinds every failure prefix, and accepts an empty binary. Ordinary
compiled `identity/1`, `wrap/1` and `drop/1` functions consume it. This is a C-internal
execution boundary—not a successful implementation of asynchronous `Call`, once-only
completion, host-owned output transfer, queue limits or Engine/Isolate ownership.

`lb_bitstring_view` returns a borrowed data/bit-offset/bit-size window over valid
owned terms. It is not a hostile-word validator or permission to transfer raw Eterms
across worlds. Callers must preserve the owner and reacquire views after collection.

## Validation and the compiler dependency exposed

The first execution run is recorded at
`/tmp/libbeam-offheap-execution-reviewed/summary.json`: Debug/Release/UBSan execution,
86 load-allocation failure prefixes with retry, copied inputs through 64 KiB,
collection at each dispatch boundary, dead-input release during GC, and physical
source-module retirement while a copied binary survives. Later expanded checks add
independent layout comparison, raw/compressed ETF preparation failure prefixes,
malformed trailing-payload cleanup, both metadata BIFs' copy-failure rollback, and
simultaneously live old/replacement payloads. Final reviewed results are recorded
below, not inferred from the initial witness.

Final evidence: `/tmp/libbeam-offheap-commit-reviewed/summary.json`. Seven CTests
pass in Debug/Release; real binary fixture execution and retirement pass in
Debug/Release/UBSan. The expanded fixture passes **94 load-allocation failure
prefixes**, constructor failures and GC failure/retry, native copy-failure cleanup
for both metadata BIFs, and old/replacement payload lifetime. The independent
binary layout probes match. Existing first-slice/linked/code-growth, preparation,
representation and reproducibility checks continue to pass. All 29 tooling tests
pass under the lock at `/tmp/libbeam-offheap-tooling/`. `engine_lifecycle` is still
observed exiting 1 at the public factory; no acceptance failure is relabeled as
success. Earlier expanded evidence remains in `/tmp/libbeam-offheap-final-reviewed/`.

Retained development failures: `/tmp/libbeam-offheap-{first,diagnostic}/`. The first
fixture used a large integer bit segment such as `<<0:4096,...>>`; stock OTP 28
compiled it as **`bs_create_bin`**, not a pooled literal. The reader could decode it,
but execution did not support that family. Its variadic arity was initially
misreported as malformed; admission now reports unsupported before applying the
supported profile's fixed-arity rules.

The literal-specific witness uses ordinary compound string/bitstring constants,
which the stock compiler really emits into LitT. Disassemblies are retained at
`/tmp/libbeam-offheap-literal-inspect{,2}/`. This deliberately tests literal ownership,
not a claim that all binary expressions execute. The original `bs_create_bin` family
still needs its real generators, bit helpers and interpreter consumers; it is also
required by the unchanged stateful acceptance fixture. No success stub or alternate
bytecode was added to bypass it.

The integrated runner keeps reference OTP semantics separate from libbeam execution,
uses fresh external outputs and the user-wide lock, hashes source inputs, checks
projections and retains failures. Generated BEAMs are not committed. Reference
compiler is still OTP 28 / ERTS 16.3, not the pinned OTP 30 compiler. No new ASan,
Linux, concurrency, security, quota or performance claim follows from this slice.

## Remaining RFD milestones

Maps, closures, catches/unwind, broad bit syntax, processes/mailboxes/monitors/timers,
and scheduling remain outstanding. Native imports are still only the two real
module-info BIFs. A12 must put the actual shared execution infrastructure beneath
a fallible C Engine and connect the C++ adapter, then demonstrate the unchanged G1
lifecycle target. B-cluster stateful execution and unchanged G3 acceptance still
follow. Earlier uncommitted native lifecycle work remains preserved, not validated
by these additive tests.
