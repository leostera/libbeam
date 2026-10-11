<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 Leandro Ostera <leandro@ostera.io> -->

# Native numeric addition with owned scratch

Status: **Implemented selected-profile growth toward M3, not stateful acceptance.**
Baseline: `f94dcefd`, after [tuple/list consumers](0003-tuple-list-shapes.md).

The stateful counter needs arithmetic, not a hardcoded counter command. This slice
admits ordinary `erlang:'+'/2`, including small integers, signed bignums and finite
binary64 operands, through the existing generated BEAM execution path. It does not
implement integer formatting, bit-syntax construction or persistent processes.

## Native algorithms and publication

`core/big.c` transplants the selected flat-64 digit comparison, carry/borrow,
signed addition, normalization and integer-to-double algorithms from OTP `big.c`.
The output normalizes back to a small when possible; arithmetic never silently
wraps at the small boundary. The native 65,535-digit result ceiling produces
`system_limit`. Unsigned 128-bit compiler arithmetic implements the native
double-digit carry path. Integer-to-double conversion preserves nearest/ties-even
rounding, including lower-limb sticky bits and overflow refusal.

`core/bif_numeric.c` adapts `erts_mixed_plus` to explicit process/domain ownership.
Small overflow uses local output words; compound integer arithmetic uses fallible,
independent domain scratch. Computation finishes **before** any copying collection.
Publication reserves result space plus outstanding heap credit, retains physically
initialized X/Y roots, copies the completed representation and releases scratch on
every exit. There is no ERTS heap-fragment singleton or second numeric representation.
Existing copying GC, exact comparison and physical process destruction consume the
newly produced ordinary bignum/float terms.

The alternative of small-only addition would either reject legitimate overflow or
silently change Erlang semantics. Retaining native ERTS fragments would require
another ownership/GC graph. Independent temporary storage instead keeps one owned
heap graph, at the cost of additional allocation/copying and conservative X roots.
It is not a performance or density optimization.

## Generated execution and admission

Dispatch grows from **163 to 174 whole generated cases**: the shared plus and
increment groups, native frame trim and the two-register move window. The complete
native `increment` transform generator uses the existing fallible operation arena.
No rules are disabled and no unsupported helper becomes an optimization miss.

Arithmetic boundary projection synchronizes heap/stack state before calling the
owned helper, reloads it afterward, and recomputes destination pointers. This is
essential when a Y destination moves during collection. Native tagged-increment
shifting uses an unsigned shift before conversion back to the signed word, avoiding
negative signed-shift UB in the compiler-overflow fast path. Both native fast-path
configurations are tested. Digit work uses the native 16-words-per-reduction cost;
actual collection cost also reaches the existing scheduler budget. A single
arithmetic instruction can still do substantial work: no hard fairness is claimed.

The Engine's positive catalog now contains **three** implementations:
`get_module_info/1,2` and `+/2`. All are heavy because these owned adapters may
collect. Numeric BIF identifiers alone remain insufficient authority. `gc_bif2`
admission checks the actual linked positive catalog entry, source definitions,
live roots, destination and failure label. Success clears the destination's old
shape; failure preserves incoming facts except discarded X registers. Frame trim
checks dropped/remaining counts and shifts Y definitions and shape facts together.
Compiler register hints remain stripped before native adjacency transformations.

Bad arithmetic and native size overflow retain their body-exception/guard-failure
behavior. Scratch or collection OOM is checked **before** guard branching and is a
terminal execution failure, never a false guard. A failed collection leaves the
old heap and roots intact; the failed invocation is terminal, and a fresh invocation
can succeed. No allocation remains after failed publication.

## Floating environment

Mixed floating arithmetic saves the worker's floating environment, installs the
standard default IEEE environment, computes, and restores the prior environment
before allocation. Successful arithmetic does not inherit a host's non-default
rounding mode. Signed zero follows native addition semantics; nonfinite conversion
or addition raises `badarith`. Binary64 representation is checked at compile time;
fast-math/finite-only compilation is refused rather than admitting altered semantics.

Failure of a floating-environment API produces terminal `notsup`, not an arithmetic
guard answer. Restoration is attempted after setup failure; failed restoration
cannot promise that the worker environment was restored. Guest execution remains
worker-only, so the host control thread's environment is not modified by arithmetic.
Tests interpose the real environment APIs only to inject their failures; successful
calls still delegate to the platform implementation.

## Evidence and remaining scope

`numeric-sources.json` records source hashes, selected algorithms and adaptations.
The integrated runner verifies this provenance and source stability under the shared
validation lock. Compiler-produced artifacts remain outside the repository.

- `core_big_test.c`: 10,000 signed addition cases against an independent 128-bit
  oracle; input immutability, output canaries, normalization, full carry/borrow
  chains and the native size limit. Another 10,000 integer-to-double comparisons,
  limb-crossing ties/sticky bits of both signs and the largest finite boundary.
  These are component proofs, not guest execution.
- `add_slice.erl` / `core_add_test.c`: actual compiler-output addition/increment,
  negative increments, carry/cancellation, signed zero, finite overflow, badarith,
  body/guard size limits, saved Y results, frame trim and collection at dispatch
  boundaries. Heap pressure forces collection at the generated Y-to-Y arithmetic
  instruction; its relocated destination must contain the real sum. Pending heap
  credit survives arithmetic publication.
- Two scratch/collection allocation-failure prefixes are tested for body and guard
  operations, with unchanged old roots/backing and fresh-call retry. Three injected
  floating-environment failures remain terminal rather than guard false.
- A BEAM call-target mutation redirects real self-call sites to the already-linked
  `+/2` import, separately executing the native BIF trampoline, its values, badarith
  and allocation failures. Five malformed image mutations reject bad roots,
  destinations, foreign failure labels and an unapproved `gc_bif2` target, then
  permit valid-image retry. **117** complete loading allocation-failure prefixes
  roll back exactly and permit same-space retry.
- Public C++ calls execute a real numeric recursion and floating-rounding-dependent
  binary return on the shared worker. The worker is started with a non-default
  inherited host rounding mode; the host control-thread mode remains unchanged.
  Stock OTP value assertions are recorded separately.

Evidence: `/tmp/libbeam-add-native-fixed/summary.json` and the final
`/tmp/libbeam-add-commit-reviewed/summary.json`: Debug/Release **14 CTests**, direct
fixture/public execution, UBSan (including compiler-overflow fast paths), physical
retirement and unchanged Engine lifecycle. Tooling evidence is
`/tmp/libbeam-add-tooling/summary.json`.

The unchanged stateful example still refuses unsupported imports/instructions.
M3/M4 remain incomplete: persistent processes, closures, identities, mailboxes,
monitors, registration, timers, integer-to-binary conversion and bit-syntax creation
are still required. No new catch/stacktrace, loose comparison, ordering, other
arithmetic, full Erlang/Elixir, pinned-OTP30 compiler, Linux, ASan/TSan, security,
quota or performance acceptance is claimed.
