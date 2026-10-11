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

## Follow-on: native forward bit copying

Baseline `068aa543`. The preceding milestone status is historical; shared worker
execution and public copied results now exist as recorded in
[public Isolates](0003-public-isolates.md). `bit_copy.c` brings the native
`erts_copy_bits_fwd` algorithm into that existing result-copy path. The source
hash and exact adaptations are in `core/otp/bit-copy-sources.json` under `libbeam/`.

The pure copier handles single-byte masks, aligned `memcpy`, shifted bytes and
masked tails. It allocates nothing, retains nothing, and never collects. Its
checked boundary refuses out-of-range offsets/counts, arithmetic overflow, NULL
nonempty spans, and overlap of touched bytes **before writing**. Disjoint byte
intervals in one allocation are allowed; disjoint bit intervals sharing a byte
are not. Partial destination bytes must be initialized and retain their outside
bits. Full-byte destinations need no initialization. Zero-length copies never
dereference the spans. Valid declared pointer spans remain a caller obligation;
this is not arbitrary-address validation.

The native masked-tail algorithm already reads an extra source byte only when
needed. It does not rely on payload padding. Public completion retains its existing
byte-sized-result check, 64-KiB bound and fallible allocation; the new helper replaces
the local aligned/unaligned copy loop without changing completion or ownership.

Validation covers source/destination offsets 0–15, lengths 0–257, three data
patterns, a 64-KiB unaligned copy, an independent bit-at-a-time oracle, preservation
of outside destination bits and source bytes, unchanged destinations on refusal,
and exact-end source/destination guard pages. Real compiled `binary_slice:identity/1`
also returns native private SubBits argument views at 16 offsets and 513 lengths,
with collection after every dispatch, copied result checks and exact retirement.
Those argument views are constructed by the C test; **guest slicing is not
admitted**. Existing public tests exercise the real worker's copied-result path.

Final evidence: `/tmp/libbeam-bit-copy-commit-reviewed/summary.json` records 16
passing Debug/Release CTests, actual fixture/public execution and UBSan, including
the new copier and moving-GC view cases. All 29 tooling tests pass under the lock
at `/tmp/libbeam-bit-copy-tooling/summary.json`. Earlier passing evidence is retained
at `/tmp/libbeam-bit-copy-integrated-first/summary.json`. This is not new ASan,
platform or performance acceptance. Stateful `two_isolates.cpp` remains red and
unchanged.

`bs_create_bin` remains explicitly refused. Its single generated case covers
binary, integer, float, UTF, append/private-append and associated error handling.
The copier supplies one real dependency; it does not justify dropping branches,
substituting successful helpers, or admitting that case before its other consumers
exist. Reverse copying, overlapping copies, writable binaries, matching contexts,
and those construction instructions remain outside this slice.

## Follow-on: integer and Unicode segment writer dependencies

Baseline `29645963`. `bit_writer.c` transplants the native small/bignum BE/LE
formatters, byte-borrow negation, restore-and-shift path, integer writers and
UTF-8/16 encoders. UTF-32 retains the native instruction's scalar validation and
fixed-size integer delegation. Provenance is pinned in
`libbeam/core/otp/bit-writer-sources.json`. This is another construction dependency,
not admission of a partial `bs_create_bin` case.

The caller owns an initialized writable span and explicit next-bit cursor. The
helpers allocate nothing, collect nothing and retain no input terms. The
caller-owned cursor borrows the output span; input terms must remain valid and
owned throughout. Checked state, capacity, endianness and type refusals precede
mutation. Writer-control/output overlap is refused, as is bignum-input/output
overlap for nonempty integer writes. Declared pointer spans and valid term representations remain caller
obligations. Successful writes preserve both the existing prefix and unused tail
bits; the tail-preservation wrapper strengthens the native append-only contract.

Native integer truncation, arbitrary sign extension and partial little-endian
byte grouping remain intact. Backward stores use bounded indices rather than
forming a pointer before the buffer. Small-value left shifts use unsigned bits,
and the byte-store unrolling becomes the same bounded arithmetic-shift loop.
Native bignum borrow uses either the compiler primitive or its original fallback.
UTF only copies its temporary when the **intra-byte** offset is nonzero: at a
nonzero aligned offset, the native routine already wrote directly to the right
address, so its redundant same-address `memcpy` is removed.

Tests cover 16 integer offsets and lengths 0–257, signed boundaries and bignums,
10,000 independent unsigned-128 oracle cases, 3/257/65,535-limb values, both byte
orders, unchanged inputs, preserved outside bits and pre-write refusals. Every
Unicode scalar candidate through `0x10ffff` is decoded independently, rotating
five encoding/endian modes and 16 offsets; surrogates refuse without mutation.
Exact-end guard pages exercise integer and Unicode destinations. Compiler borrow
and `DEBUG` fallback paths run separately under UBSan.

A separate **stock-OTP-only** `bit_writer_reference.erl` emits 1,070 integer and
Unicode vectors, including invalid scalars, partial widths and nonzero aligned
starts. Both C borrow implementations must match exactly. That module is never
loaded into libbeam and is not evidence of guest construction support.

The execution bridge is explicit: C writers produce bytes, the real owned
constructor copies them into heap/offheap bitstrings, and ordinary compiled
`binary_slice:identity/1` returns those arguments across collection at every
dispatch. Poisoning the source span does not affect the process result; destruction
balances all resources. This demonstrates consumption and lifetime of the native
helper output, **not execution of a guest bit-construction instruction**.

Final evidence: `/tmp/libbeam-bit-writer-commit-clean/summary.json` records 17 passing
Debug/Release CTests, reference agreement, fixture/public execution and UBSan.
All 29 tooling tests pass at `/tmp/libbeam-bit-writer-tooling-clean/summary.json`.
Earlier passing evidence remains at `/tmp/libbeam-bit-writer-integrated-first/`;
`/tmp/libbeam-bit-writer-first/` retains the initial strict-compiler signed-counter
warnings corrected before execution validation.
Float segments, binary/string assembly, append/writable ownership, size analysis,
fixups and error-context dependencies still precede whole-case admission. The
unchanged stateful acceptance remains red; M3/M4 are not completed by these helpers.

## Follow-on: binary/string and floating segment dependencies

Baseline `e9099c29`. The writer now includes native binary-window copying,
`binary-all` unit checks and string copying. Type, short-source, unit and capacity
checks precede writes; units are restricted to the language's 1–256 range.
Touched-byte overlap refuses rather than corrupting an input. These pure helpers
retain no source terms and do not charge a process: native bit-copy reductions
remain an explicit responsibility of the future instruction consumer.

`bit_float.c` implements the native 16/32/64-bit conversion paths. Small integers
convert **directly** to binary32; routing through binary64 can double-round them.
Bignums preserve the native conversion through `big_to_double`. Binary16 uses the
complete native software `f64_to_f16` algorithm directly, with one ties-to-even
rounding step, rather than narrowing via binary32 or requiring a compiler half
extension. Narrowing a finite value to binary16/32 may produce infinity **bytes**,
as native bit syntax permits; no nonfinite Erlang float term is produced.

Conversions save the caller's floating environment, install the default IEEE
environment, compute into independent storage, and restore before copying into
the destination. Save/setup/restore failure is a separate infrastructure status,
not badarg or false guard, and cannot publish bytes or advance the cursor. A
failed restoration cannot promise a restored environment. Invalid width, wrong
type and unconvertible integer retain distinct native error categories.
Binary32 overflow is spelled out around the exact midpoint `0x1.ffffffp127`,
avoiding out-of-range ISO C casts while retaining native nearest-even results.
Endian emission and unaligned publication use the existing checked bit copier.

Coverage includes binary source/destination offsets 0–15 and lengths 0–257,
heap/offheap inputs, unit boundaries, source immutability and unchanged refusal
state. Floating tests cover every adjacent finite positive half midpoint and
its binary64 neighbors, both signs, binary32 midpoint samples across all finite
exponents, signed zero, underflow, overflow, direct-small versus native-bignum
rounding, default-environment scoping, three real fenv failure injections and
exact-end destination guard pages. An additional **840 stock-OTP-only vectors**
match Debug, Release and UBSan output, separately from the earlier 1,070 integer
and Unicode vectors. The reference module is still never loaded into libbeam.

C-built argument witnesses now mix integer, UTF, binary, string and all three
float widths, then exercise the real heap/offheap constructors, compiled identity
execution, moving collection, detached source bytes and balanced destruction.
This still does **not** execute a guest construction instruction. Final evidence
is `/tmp/libbeam-bit-segments-commit-reviewed/summary.json`: 18 passing
Debug/Release CTests, reference comparisons, selected public execution and UBSan.
All 29 tooling tests pass under the lock at
`/tmp/libbeam-bit-segments-tooling/summary.json`. Earlier evidence remains at
`/tmp/libbeam-bit-segments-integrated-first/summary.json`.

Append/private-append ownership, aggregate sizing, loader fixups, error contexts
and complete transform/dispatch integration remain ahead of `bs_create_bin`
admission. Neither helper availability nor opcode identity grants that admission;
stateful acceptance and the M3/M4 contracts remain incomplete.

## Follow-on: process-owned writable backing and normal append

Baseline: `877207fd`. This snapshot adds the native writable representations and
non-private append ownership path in `core/writable.c`, not guest instruction
admission. Provenance is recorded in `core/otp/writable-sources.json`.

A process now owns two native BinRef chains: ordinary immutable references and
writable references. A writable backing has exactly one BinRef/reference-count
owner, but multiple prefix views may point to that same BinRef. Normal append
clears the previous view's mutable bit, preserves its length, and returns a new
writable SubBits. An older view remains volatile: it follows its BinRef instead
of dereferencing a cached pointer after the backing moves. Appending to a revoked
prefix or a pinned backing takes the native copying branch. There is no inferred
uniqueness of heap headers and no private append implementation in this slice.

### Fallible preparation and commit

Capacity allocation, initialization and prefix copying happen before the final
heap reservation. Replacement uses a fresh owned allocation rather than a fatal
allocator realloc. An explicit temporary process root preserves incoming terms
that are not in X registers across moving GC. After the last fallible step, the
helper reacquires heap pointers, revokes the old view, changes the BinRef when
necessary, publishes the new view, and releases replaced backing. Allocation or
collection failure preserves source bytes/flags/ownership, output slots and heap
credit; retry uses the same process. No borrowed heap pointer crosses collection.

The returned checked writer borrows initialized bytes. Callers must finish its
exact requested bit count before publishing the term to guest code; at most
seven tail-padding bits lie within its byte capacity. Output slots must not live
in the moving heap. Source terms belong to this process or its retained literals;
these are not hostile-word or foreign-pointer validation APIs. Zero-size append
returns the actual source and an empty writer instead of native crash-poison
pointers. Init accepts a decoded capacity; it does not implement the language
argument/default rules of `bs_init_writable`.

Growth retains native doubling below the 16-MiB threshold and 20-percent growth
above it, using integer arithmetic and a 64-MiB payload-capacity ceiling. The
immutable-source path retains the 256-byte minimum. This is a per-backing profile
bound, not an aggregate memory quota or a low-peak-allocation claim.

### Pinning, collection and physical release

Read-only views and exact comparison resolve volatile data without modifying
source headers. Explicit pinning revokes backing write permission and normalizes
the chosen view before immutable flat-copy/sharing. Direct reference retention
refuses writable backing; existing flat-copy still requires immutable inputs.
Other volatile views keep resolving correctly after one view is pinned.

Copying GC moves SubBits origins and transfers forwarded writable BinRefs without
incrementing their counters. It releases dead references, clears active-writer
state, and migrates frozen references into the normal offheap chain. Both chains
account actual allocated capacity per reference. Destruction releases both before
freeing the heap. A copied pinned payload remains valid after its source process
is physically destroyed and is freed on its own final release.

Pin and GC deliberately do not shrink allocations. Native shrinking would need
new fallible host allocations, incompatible with the collector's allocation-free
commit after forwarding and with infallible pin. Retaining spare capacity is an
explicit policy, not an ignored shrink failure. It costs retained memory until
final release; no density claim accompanies this choice.

### Evidence and remaining admission work

The focused binary suite tests eight real allocation-failure prefixes across
init, immutable-source copy, backing growth, in-capacity append and zero append,
with exact balance/unchanged ownership and same-process retry. It checks 4,128
unaligned source windows, heap/refcounted sources, old prefixes through growth
and collection, branching from revoked/frozen views, pin/copy/source destruction,
dead writable release, units, zero lengths, growth thresholds and 64-MiB capacity.
C-created writable arguments execute actual compiled identity and exact-comparison
functions with moving collection, including unequal results. These are C producers
consumed by real BEAM code, **not guest binary construction**.

Private append and its uniqueness proof, aggregate sizing, native loader fixups,
error contexts, work/reduction accounting and full transform/dispatch integration
still precede `bs_create_bin` admission. Stateful acceptance and M3/M4 remain
incomplete; neither acceptance example source is changed.

Final validation: `/tmp/libbeam-writable-commit-reviewed/summary.json`, with 18
Debug/Release CTests, actual fixture/public execution, native flag/layout oracle,
UBSan, and all 99 expanded binary-module loader-failure prefixes. The 29 tooling
tests are recorded at `/tmp/libbeam-writable-tooling-final/summary.json`. Source
hashes remain unchanged during each locked run. Earlier passing integration is
retained at `/tmp/libbeam-writable-integrated-first/summary.json`. The unchanged
stateful example still reports unsupported BEAM profile/import; no M3/M4 pass is
inferred from these dependency and selected-profile execution witnesses.

## Follow-on: checked private append

Baseline: `dab688df`. `lb_writable_private_append` now retains both native
`erts_bs_private_append_checked` paths. With writable backing, it extends the
existing SubBits header in place, preparing any backing growth before mutation.
With frozen backing, it allocates a new binary, BinRef and SubBits even for a zero
extension. That copy is bounded by the smaller capacity; unlike normal append's
immutable-source path, it does not impose a 256-byte minimum. Heap fragments are
replaced by a fallible heap reservation before publication.

A reference count of one does **not** establish a unique SubBits header: several
VM roots can contain the same term. The C boundary therefore takes a typed X/Y
source location and checks other X/Y, exception and temporary roots plus stored
heap terms before mutation. It skips opaque numeric/binary payloads rather than
mistaking encoded pointer bits for references. Unknown object families abort
instead of concealing roots. Even dead allocated terms count conservatively until
collection. `LB_WRITABLE_NOT_UNIQUE` is an unchanged ownership refusal, not a guest
`badarg` or false guard. The scanner allocates nothing and has linear cost.

Success consumes the source slot before assigning the result; a Y location is
resolved again after moving collection. Unrooted C aliases are forbidden, as are
output pointers into moving heap storage. Distinct old prefix headers can still
share the backing BinRef: private growth leaves their lengths unchanged and their
volatile reads follow the replacement. Frozen payloads copied elsewhere remain
immutable and independently retained through fallback and process destruction.

Tests cover eight alias locations/shapes, conservative dead aliases and collection,
opaque payload words equal to the source address, X/Y roots, allocation-free
in-capacity/zero growth, three allocation-failure prefixes with exact rollback and
retry, frozen zero/smaller-capacity copies, shared payload retirement, range/type/
revoked-source refusals, and actual identity execution with moving GC.

This is **not** guest instruction admission or a compiler uniqueness proof. Future
admission must establish linear source consumption, including no subsequent reuse
by other segment operands or instructions, and account for the scanner's work.
The complete sizing/fixup/error-context/dispatch path remains absent. M3/M4 and the
unchanged stateful acceptance example remain incomplete.

Final locked evidence: `/tmp/libbeam-private-commit-reviewed/summary.json`
(Debug/Release, 18 CTests, actual selected-profile execution, UBSan and source
hashes) and `/tmp/libbeam-private-tooling-final/summary.json` (29 tooling tests).
The initial passing integration is retained at
`/tmp/libbeam-private-integrated-first/summary.json`. No new guest opcode or BIF
is admitted and neither acceptance example source changes.

## Follow-on: native error-payload flatmaps

Baseline: `9d24bf57`. The native construction-error helper does more than set
`badarg`: it builds `#{cause => {Segment, Operation, Info, Value}, function =>
format_bs_fail, module => erl_erts_errors}`. `core/binary_error.c` now constructs
that exact payload using the native operation-kind switch, cause tuple and MAP3
layout. It is a **payload factory**, not exception/stacktrace handling or guest
`bs_create_bin` admission. Provenance is in `core/otp/flatmap-error-sources.json`.

`core/flatmap.h` retains the native three-word flatmap prefix, separate sorted
keys tuple and contiguous values. The selected representation range is 0..3,
valid under both native Debug and Release small-map policies. The error factory
uses native atom-name ordering (`cause`, `function`, `module`), not namespace-local
atom-ID ordering. This does not introduce general map construction, MAP_EXT
loading, map operations or HAMT nodes.

Map body sizing now reads the native size word; generic header arity is not a
map allocation size. GC, flat sizing/copying and the private-append alias check
visit the keys tuple and every value. The native exact-equality branch compares
size, then keys/values through the existing fallible term-array stack. Unknown
map headers/sizes abort rather than conceal roots or become equality misses.
Copied offheap values remain retained after their source process is destroyed.

The payload factory replaces HeapFragOnlyAlloc with a final fallible heap
reservation. An explicit temporary root protects even a boxed value held only
in a C argument during moving collection. Publication follows all failure points;
OOM preserves old roots, outputs, heap credit and payload ownership, allowing
same-process retry. It does not set `freason`, `fvalue` or `EXF_HAS_EXT_INFO`.
Those exception-state transitions remain part of the future complete instruction
and exception integration; helper availability grants no execution permission.

Tests cover the extracted native layout/MAP3/BSC identity oracle, all 14 native
operation-switch outcomes including `none`, 128-deep map equality/copying, scratch
OOM with unchanged outputs, failed-copy refcount rollback, rooted X/Y/exception
values, source-process retirement, and writable aliases captured directly in map
values. C-created payloads execute real identity and equal/unequal BEAM functions
with moving collection. A stock-OTP-only fixture supplies five actual exception
payload vectors; notably, invalid float width reports `invalid`, not `size`.
The reference fixture is never loaded into libbeam. No guest map or construction
instruction, full error handling, stateful acceptance or M3/M4 completion follows.

Final evidence: `/tmp/libbeam-flatmap-commit-reviewed/summary.json` (19 passing
Debug/Release CTests, actual fixture/public execution, representation and five
stock-OTP payload vectors, UBSan, unchanged source hashes) and
`/tmp/libbeam-flatmap-tooling-final/summary.json` (29 tooling tests). Earlier
passing integration remains at `/tmp/libbeam-flatmap-integrated-first/summary.json`.
Both acceptance sources are unchanged; the stateful example remains refused.

## Follow-on: fresh construction storage

Baseline: `5b9c4508`. `lb_binary_begin` in `core/writable.c` adds the fresh-storage
counterpart of append: native heap bits through 512 bits, otherwise an ordinary
Binary/BinRef/SubBits pair on the normal offheap chain. It allocates exact byte
capacity rather than applying append growth or writable flags. The existing
native wrapper is shared with immutable input/literal construction, not copied
into a second implementation. Provenance remains in `otp/writable-sources.json`,
now also pinning the fresh-storage branch in native `emu/bs_instrs.tab`.

Offheap payload allocation and initialization precede the final fallible heap
reservation. A detached backing contains no heap pointers and can be released
if reservation fails. After successful collection, publication is infallible.
The result reserves `max(extra_words, pending heap credit)` free words without
changing the pending credit counter. Arithmetic overflow is rejected before
allocation; old roots, heap, chains and outputs survive allocation failures.

All bytes, including heap-word slack, start initialized. The writer borrows an
exclusive construction span: finish exactly the requested bit count before
sharing or publishing to guest code, and never write afterward. Collection ends
the borrow. Successful reservation can move X/Y operands, so callers must reload
them; a future instruction must also defer destination-register publication until
all segment operands have been read and construction has succeeded. An abandoned
partial result remains a valid collectible object, not an untracked allocation.

Tests cover eleven real allocation-failure prefixes with exact balance and
same-process retry, allocation-free heap construction, the 512/513-bit transition,
zero/non-byte-sized results, initialized slack and untouched padding, pending
credit/extra reservation, X/Y operand relocation and alias preservation, native
identity execution with moving GC, copied results surviving process destruction,
and the actual 64-MiB boundary. A real writer failure followed by error-payload
OOM/retry proves retirement of an unpublished partial backing during collection.

This helper does **not** implement aggregate segment sizing, native descriptor
fixups, reduction accounting, virtual-binary-heap collection policy, private-source
consumption proof, overlapping-source handling or exception/dispatch integration.
Those remain whole-instruction admission requirements; there is still no new
opcode/BIF or stateful acceptance claim.

Final evidence: `/tmp/libbeam-begin-commit-reviewed/summary.json` (19 passing
Debug/Release CTests, actual selected-profile execution, UBSan and stable source
hashes) and `/tmp/libbeam-begin-tooling-final/summary.json` (29 tooling tests).
Earlier passing integration is retained at
`/tmp/libbeam-begin-integrated-first/summary.json`. The unchanged stateful example
still reports unsupported BEAM profile/import; neither acceptance source changes.

## Follow-on: shared-byte boundaries in binary segments

Baseline: `53a56c88`. A valid unaligned append can read an old prefix and write
new bits in the same physical byte without overlapping the prefix's logical bit
range. Rejecting every touched-byte overlap is too conservative for that native
case. `lb_bits_copy` now compares normalized byte-address/bit-offset endpoints,
after checking spans and address overflow, and accepts disjoint bit intervals.
No pointer is multiplied by eight. The native forward kernel is unchanged: its
edge masks preserve the source bits, and its full-byte memcpy interiors remain
byte-disjoint. An explicit identity no-op avoids invoking memcpy on itself.

All other overlapping **bit** intervals remain refused, unchanged, in either
address order. This is not a general memmove or reverse-copy implementation.
Destination-outside bits and source bits survive; other bits in a shared source
byte can legitimately be destination bits. Binary/string writers inherit that
rule and advance their cursor on an exact self-copy. Input metadata, numeric
storage and writer-control storage remain protected by their existing contract;
this does not grant permission to overwrite an immutable term or to publish an
unfinished constructor span.

An independent bit-at-a-time oracle reads a saved source snapshot and checks
both address orders, exact identity, genuine-overlap refusal, adjacent ranges,
small gaps, differently based views, and long shared-edge copies. Existing exact
payload guard-page checks remain. Binary/string component checks cover identity
and unchanged true-overlap refusals. C-owned normal/private append consumes old
volatile prefixes for all lengths 1..257, including backing growth. Those values
execute actual identity BEAM with moving GC, then remain readable from a copied
result after process destruction. A stock-OTP-only oracle adds 514 matching
compound-result vectors; it proves values, not which optimization OTP selected.

This supersedes only the earlier touched-byte refusal policy. Opcode admission,
aggregate sizing/fixups, work/virtual-heap accounting, private-source consumption
proof and complete exception integration remain outstanding. No M3/M4 completion
or successful stateful acceptance is inferred from these helper witnesses.

Final evidence: `/tmp/libbeam-bit-alias-commit-reviewed/summary.json` (19 passing
Debug/Release CTests, 514 additional stock-OTP vectors, actual selected-profile
execution, UBSan and stable source hashes) and
`/tmp/libbeam-bit-alias-tooling-final/summary.json` (29 tooling tests). Earlier
passing integration is retained at
`/tmp/libbeam-bit-alias-integrated-first/summary.json`. Both acceptance sources
remain unchanged and the stateful example is still refused.

## Follow-on: complete native loader generator

Baseline: `92b50536`. `project_transform.py` now retains the complete native
`create_bin` generator and `bs_create_bin/6` transformation case. No segment-kind
branch is dropped: append/private append, binary-all, dynamic/fixed binary,
float/integer, string and UTF descriptors are all translated by the generated
native body. `otp/binary-loader-sources.json` records the sources and adaptations.
This is preparation of native loader IR, **not executable opcode admission**.

`beam_transform.c` establishes structural preconditions before any generator
allocation: nonempty count/arity agreement, bounded counts, fixed fields,
canonical register numbers, unit/segment ranges, valid literal lookups, proper
atom flag lists and string spans within retained StrT. Append kinds must be first;
normal append's Live leaves a temporary register available. These conditions do
not establish must-definition, Y-frame validity, private-source consumption or
uniqueness. The executable verifier still refuses the opcode before transforming
it, and interpreter support remains absent independently.

The native multiplication helper uses unsigned wrap and quotient checking. Fixed
sizes preserve the native 56-bit ceiling; overflowing or negative constants keep
the dynamic descriptor rather than becoming truncated lengths. Native-endian
flags are resolved before packing the segment number. The original generator's
assignment inside ASSERT is corrected to equality after preflight, eliminating
a Debug-only input mutation. Bounded arena allocation replaces fatal operation
and argument-array allocation; OOM is never an optimization miss. Failed temporary
prefixes remain owned by the program and retire through its ordinary destructor.

Tests cover all non-string kind variants directly, overflow/fixed-size boundaries,
flag/endian handling, 65,532 input segment operands, malformed descriptors without
allocation, and both generator allocation failures with exact retirement and
same-space retry. A freshly compiled `binary_create_slice.erl` exercises actual
source operations, including string offsets and bounds. Its executable verifier
refuses `bs_create_bin`; eager loading also independently refuses unresolved
imports when present. Nothing is published as executable code on either path.
Full emission/fixups, executable proof, runtime sizing, work/virtual-heap policy,
exception integration and complete generated dispatch remain outstanding.

Final evidence: `/tmp/libbeam-create-bin-generator-commit-reviewed/summary.json`
(20 passing Debug/Release CTests, compiled-fixture transformation, actual existing
selected-profile execution, UBSan and stable source hashes), plus
`/tmp/libbeam-create-bin-generator-tooling-final/summary.json` (29 tooling tests).
Earlier passing integration is retained at
`/tmp/libbeam-create-bin-generator-integrated-first/summary.json`. Both acceptance
sources remain unchanged; the stateful example still refuses unsupported code.
