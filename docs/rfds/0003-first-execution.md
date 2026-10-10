<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 Leandro Ostera <leandro@ostera.io> -->

# First additive BEAM-word execution

Baseline: `1c47d6c3`. This continues the [loader checkpoint](0003-loader-program.md),
not the frozen subtractive experiment. See the [inventory](0003-additive-runtime-inventory.md)
for the complete remaining contract.

## Scope: execution now exists; the full request is not finished

The C core now executes the ordinary compiler-produced `first_slice.erl`, including
its generated `module_info/0,1`, through generated transformations, emitted specific
BEAM instructions and generated interpreter bodies. This is the first G2 execution
witness, not an empty Engine handle, generic-op evaluator, hardcoded MFA executor,
helper VM, or legacy-ERTS fallback.

**Do not describe all A03–A07 as complete.** A03 generation is integrated with a
selected dispatch profile; A04 feeds real loading; A05 has runtime consumers for
its admitted literal subset; A06/A07 now transform, emit, resolve, publish and retire
real executable code for that profile. Full transform/helper coverage, maps,
offheap binaries, fun/catch consumers, source-line instrumentation and broader
language behavior remain outstanding. Missing imports/instructions refuse loading,
including when the affected function would not be invoked.

The C++ factory, C Engine/Isolate ownership graph, scheduler and B-cluster stateful
behavior remain unimplemented. `engine_lifecycle` still exits 1. The unchanged
`two_isolates.cpp` does not pass. Neither example nor earlier lifecycle edits was
changed. A bounded internal executor does not implement the public asynchronous
binary-call contract.

## Source and representation

The existing pinned generator inputs and `loader-sources.json` remain authoritative.
New source hashes are in `libbeam/core/otp/execution-sources.json`:

| Source | Admitted use / adaptation |
|---|---|
| Unchanged `beam_makeops` output | Whole transformation cases and actual instruction bodies; numbers, masks, signatures and packing remain generated |
| `beam_load.c:load_code` | Transform/retry/select/emit sequencing over owned operations |
| `emu/emu_load.c:beam_load_emit_op`, `finish_emit` | Signature emission, packing stack, label/literal/import fixups and function-header layout; checked allocation replaces fatal allocation |
| `code_ix.h`, `export.h` | MFA/function-header and export-dispatch shapes; private directory replaces singleton active-code lookup |
| `emu/beam_emu.c:process_main` | Explicit register/heap/stack swaps, continuation/reduction and native-call boundaries; no scheduler/global startup |
| `erl_gc.h:move_cons/move_boxed`, `erl_gc.c:sweep` | Forwarding and Cheney traversal for admitted object shapes; explicit process roots and fallible tospace construction |
| `erl_bif_info.c:get_module_info`, `module_info_0/1` | Actual module, export/function, attribute, compile, checksum and native-profile metadata |
| `erl_md5.c/.h` | Digest algorithm, private symbol aliases and portable includes; unsigned promotion fixes the portable little-endian read's signed-shift UB |

This is the upstream **64-bit `NO_JUMP_TABLE` specific-instruction word ABI** with
one-word continuations, not JIT machine code. Function headers occupy five words.
Register offsets, loader X/Y tags, packed operands and relative labels follow the
selected upstream layouts. Signed relative offsets are recovered before C pointer
arithmetic. The immutable host-return instruction is the real `normal_exit` opcode.
No dispatch slot contains a fake function address.

`project_transform.py` extracts complete generated cases. Missing helper dependencies
produce an explicit unsupported status, never `TE_FAIL` pretending a predicate did
not match. Pure helpers are copied from the generated output. Allocation/init
operation generators use fallible owned allocation. The small owner-dependent
predicate set consults actual imports/native entries and label metadata. The
original `never` predicate remains the original deliberate rejection predicate,
not a replacement for an unimplemented helper. Narrow allocation/index checks are
recorded in the projection code; all other rule bodies and their priority remain.

`project_dispatch.py` projects complete instruction bodies and the whole shared
`deallocate_return` group. It removes instrumentation absent from the profile,
replaces actual GC/context boundaries, and rejects unadapted `erts_*` dependencies.
There is one dispatch profile, not a legacy/new execution selector. The current
37 generated cases are joined by real native-call, completion and function-clause
boundaries in `process.c`. Broader cases must be admitted with their real helpers.

## Atomic admission and physical ownership

1. `lb_code_space_create` constructs a private atom table, code directory and the
   positive native catalog. Its caller-supplied allocation domain outlives it.
2. An internal program preparation leaves atom identities **provisional**. The
   atom table admits one serialized writer transaction; ordinary reads see only
   committed names, while other writes/destruction refuse busy.
3. The loader resolves every import, including compiler-generated metadata imports.
   Imports target a published private export, a real self export, or a positive
   native implementation. There are no lazy stubs or implicit service processes.
   This eager single-module policy also refuses unresolved mutually recursive
   module batches; it does not invent forward exports.
4. Profile dataflow checks must-defined X/Y registers, frame shape, live roots,
   heap credit and both conditional edges. Exact generated signature checks still
   run during emission. Unsupported metadata/instructions fail before publication.
5. Transforms, word-buffer growth, packing, fixups, entry construction and checksum
   complete before commit. Atom commit plus code-directory publication is
   allocation-free and serialized. Failure restores atom contents **and backing**.
6. Entry handles and process contexts retain actual modules. Module imports retain
   target modules. A caller's code therefore keeps foreign literal results alive
   even after returning from the callee. Self imports do not create immortal cycles.
7. Unload refuses live entries/processes/importers without mutation. After unlink,
   code/literal/preparation storage is physically freed before imported-module
   dependencies are released. The dependency ledger is independent of the freed
   code arena; it does not dereference retired self-export entries.
8. Space destruction refuses modules or namespace borrowers. No final lease release
   auto-destroys an owner, no bound guard is removed, and there is no global claim
   or retained mutable runtime singleton in these components.

The standalone `lb_beam_program_prepare` API retains its original non-executable
contract and commits its names before returning. The provisional path is internal
to executable construction. Bare atom words still carry no namespace provenance.
All interfaces here are serialized C-internal APIs, not thread-safe public handles.

Preparation arenas currently remain with published code, including unused decoded
operations and old growth backing. This is finite owned retention with physical
unload, not a density optimization or a claim of minimal retained module size.

## Process roots, safepoints and builtins

`LbProcess` owns a contiguous heap/stack, X registers, continuation, reductions,
exception roots and its actual entry-module lease. The dispatch loop executes
emitted words using the generated C bodies. An instruction budget bounds progress
independently of BEAM reductions; tail-recursive self-import calls genuinely yield.

The collector allocates adequate tospace before its first forwarding write.
Allocation failure leaves old heap/stack/registers intact and explicit collection
can be retried. Roots include live registers, the physical stack, result and error
terms. Module literals remain tagged, immutable and retained by code dependencies.
The selected representation's owned empty tuple keeps its two-word read-ahead space.

Host instruction-budget yields introduce safepoints beyond ordinary scheduler
boundaries. Thus even plain allocate instructions initialize physical Y slots to
NIL (logical uninitialized reads still fail validation), and explicit collection
preserves the outstanding `test_heap` reservation. A 64-field tuple is collected
between every dispatched instruction to exercise this requirement.

The only native imports are `erlang:get_module_info/1,2`. These implementations are
registered as **heavy** because they may collect the owned heap; this intentionally
differs from upstream's implementation/factory contract. Metadata is genuinely
constructed, attributes/compile terms are copied into the process, and MD5 follows
`beam_file.c` chunk order and normalized lambda uniq fields. Unrecognized selectors
raise badarg. There are no successful placeholders for unused `module_info` bodies.

The literal/GC subset is still smalls, bignums, atoms, tuples/lists, finite floats
and heap bitstrings up to 64 bytes. No fake binary refcount, offheap chain, closure,
PID, reference or map is supplied. Attributes and compile ETF are decoded only on
the executable path. `Line`, `Dbgi` and `Meta` remain passive data; executable `DbgB`
and `Recs` payloads are rejected. No line/debug/NIF/native-extension feature is enabled.
Exception reasons and roots are real, but Erlang catch/unwind/stacktrace breadth is
not admitted by this profile. Host initial arguments are immediate terms only;
public binary marshalling remains an A12/B09 dependency.

## Validation and remaining gates

Run `python3 -B libbeam/tools/run_additive_slice.py --output <fresh-directory>`.
The runner uses the user-wide lock, hashes inputs, retains failure logs and keeps
compiler outputs outside the repository. It separates reference OTP behavior,
component execution, UBSan, CTests and the unchanged red public acceptance target.

First reviewed execution evidence: `/tmp/libbeam-first-execution-reviewed/summary.json`.
It records six CTests in Debug/Release, real first-slice and linked-peer execution
in Debug/Release/UBSan, 80 first-slice load allocation-failure prefixes, 384 image
mutations, atom/backing rollback, process-construction failures, runtime OOM/retry,
forced collection, packed conditional-label fixups, every-dispatch safepoints,
self-import retirement and peer survival. Executed `module_info(md5)` matches the
separate stock OTP result. Full opcode generation and execution projections match
independent Debug/Release builds. Existing image/program/term tests also pass.

Final expanded evidence: `/tmp/libbeam-first-execution-commit-reviewed/summary.json`.
A fresh ordinary 130-function Erlang module emits **1,055 words**, executes all
128 explicit functions, and passes **472** load-allocation failure prefixes plus
retry in Debug/Release/UBSan. This crosses actual word-buffer growth boundaries.
Both stateful `probe` fixtures are explicitly refused without atom/backing leakage.
A malformed extended operand list on non-variadic `call_ext_only` is rejected at
arity admission rather than silently discarded by transformation. The earlier
expanded run is retained at `/tmp/libbeam-first-execution-expanded-reviewed/`; the arity-regression run is
`/tmp/libbeam-first-execution-final-fixed/`. Duplicate publication also refuses
without resource mutation or a misleading successful error record.
All 29 existing tooling tests passed under the lock; logs and hashes are at
`/tmp/libbeam-first-execution-tooling/`.

Retained development logs: `/tmp/libbeam-native-code-{first,build2,execute3,execute4,execute5,execute6,execute7,linked8}/`.
Early compiler diagnostics identified signed-comparison issues in generated bodies;
initial load refusals exposed the real heavy-BIF allocate and fused-move dependencies,
which were then admitted instead of bypassed. Review also fixed self-import
retirement ordering, unsafe additional host GC safepoints, and non-variadic operand
arity; regression tests remain. `/tmp/libbeam-first-execution-final-reviewed/`
retains a regression-test failure: its initial byte used the old-inline-float
extension (0) instead of the operand-list extension (1). Correcting the test to
`0x17` exercises the intended arity refusal; the final fixed run above passes.

Reference compiler is installed **OTP 28 / ERTS 16.3**, not a fresh pinned OTP 30
compiler. Linux CI is configured separately, not locally proven. No ASan pass is
claimed: the host's earlier empty-main control timed out. Security, quotas,
performance, density, JIT and other platforms remain separate gates.
