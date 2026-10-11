<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 Leandro Ostera <leandro@ostera.io> -->

# Exact term and bitstring comparisons

Status: **Selected-profile growth toward M3; no stateful acceptance claim.**

Baseline `446ada50`, after [local calls/Y roots](0003-local-calls.md). The stateful
fixture compares binary commands and arbitrary message fields. The runtime now
uses native-derived exact equality over every currently admitted user-term family,
not a fixture-string recognizer. No new BIF or native capability is granted.

## Native algorithms and explicit failure

`core/term_compare.c/.h` retain the selected `utils.c:eq` algorithms: identity fast
paths, list traversal, tuple term arrays, bignum sign/arity/digits, exact binary64
words, and bitstring views. Arrays and pending list tails use the native tagged-
word work-stack layout. The traversal is nonrecursive; a deep term cannot consume
an unbounded C call stack. A 16-word local stack grows through the explicit domain
with exact release. Growth is bounded to a 64-MiB backing allocation, not a claim
of a total guest/Engine memory quota or peak-allocation budget.

The comparison does not mutate terms, collect, yield, retain binary references or
introduce another term format. Operands must already be valid owned terms in one
namespace. Bare words are not host capabilities or a hostile-pointer validation
interface. Fun/map/ref/record/external-term cases remain unadmitted. New producers
for those types must admit their comparison, GC and copying consumers together;
unknown internal layouts must not silently become inequality.

Scratch allocation failure returns a distinct status and leaves the boolean output
untouched. The generated instruction projection extracts each native `eq`/`EQ`
comparison and checks that status **before** evaluating its existing branch. Literal
cases preserve the immediate-source short circuit. An allocation refusal therefore
cannot turn `=:=` into false or `=/=` into true. Runtime execution reports the real
OOM terminal status; all scratch prefixes are released before returning.

The dispatch now admits **82 whole generated cases** (formerly 69), adding exact
literal/register comparisons, inequality and nil tests. The generic verifier checks
both source registers and same-function failure labels for both branch successors.
Same-function `func_info` failure labels are terminal `function_clause` edges,
not reentry into the normal function-argument flow. Normal local calls still reject
those labels. This permits ordinary single-clause head patterns such as `empty(<<>>)`;
a mismatch executes the existing native function-clause boundary. Cross-function
failure branches remain invalid. The generator and word emitter remain the only
bytecode path.

Exact float comparison preserves native word semantics: `0.0 =:= -0.0` is false,
and small integer zero is not float zero. This is not loose numeric `==` or general
term ordering. Neither operation is claimed by this slice.

## Exact-sized bit storage

The bit comparator transplants the `erl_bits.h` aligned wrapper and
`erl_bits.c:erts_cmp_bits__` algorithm. Both bit offsets and partial final bytes
matter; heap and reference-backed storage can represent equal values.

The native loop eagerly loads a subsequent byte while assembling a full byte.
After aligning equal nonzero offsets, its final iteration can have zero shift and
no remaining bits: that lookahead contributes nothing but crosses an exact-sized
payload boundary. The adaptation skips that one unused read. It does not add
padding to every binary, change binary layouts, or compare unused bits.

`otp/term-compare-sources.json` pins `utils.c`, word-stack definitions, comparison
macros and both bit sources, and records the selected cases/ownership adaptations.
These are modified OTP reference inputs, not asserted pristine whole-file admission.

## Evidence

`core_compare_test.c` checks immediate/type distinctions, empty tuples, improper
and deeply nested lists, deep tuples, bignums, signed zero, literal-pointer tagging,
and equal heap/reference-backed bitstrings. Five scratch-growth failure prefixes
leave output/resources intact and permit retry.

The independent bit construction oracle covers 16 offsets on each side and all
lengths 0–257, with both equality and a flipped significant bit. Different padding
outside the views is ignored. Exact 80-byte buffers end at protected guard pages;
offset-alignment and final-byte comparisons must not read either following page.
This is focused bounds evidence, not an ASan pass.

`compare_slice.erl` is ordinary compiler output. Generated `=:=` and `=/=` cases
execute against distinct deep process-heap terms after real copying GC. Every
scratch failure prefix is exercised for both operators: the process reports OOM,
not either successful answer. A fresh invocation then succeeds. Literal exact and
inequality cases also exercise scratch OOM/retry and immediate-source short
circuiting; nil testing runs through its generated case.

The public C++ Isolate test executes both binary-command comparisons on the worker
and checks copied results for matching and nonmatching inputs. Empty-binary head
matching succeeds; a mismatch produces an actual `function_clause` exception,
checked directly by the C test and as a public exception completion. Stock OTP runs
separate value assertions, including signed zero; it is reference evidence rather
than the implementation under test.

Initial validation: `/tmp/libbeam-compare-first/summary.json`.
Expanded coverage: `/tmp/libbeam-compare-expanded/summary.json`.
Function-clause edges: `/tmp/libbeam-compare-heads-reviewed/summary.json`.
Final integrated validation: `/tmp/libbeam-compare-commit-reviewed/summary.json`.
Tooling: `/tmp/libbeam-compare-tooling/summary.json`.

The integrated runner retains provenance/input hashes, compiler identity and logs
under the shared lock, with twelve Debug/Release CTests and real comparison/C++
fixture execution under both builds and UBSan. Generated BEAMs stay external.
Both acceptance examples remain unchanged: Engine lifecycle passes; stateful
module admission still refuses its missing imports/instructions.

A comparison may traverse a whole term or binary within one instruction. Instruction
and reduction bounds are not hard latency/preemption guarantees. No security,
performance, Linux, ASan/TSan, full Erlang/Elixir compatibility or M3/M4 completion
claim follows from these tests. Persistent processes, closures, references,
mailboxes, monitors, registration, timers and numeric/binary construction remain.
