<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 Leandro Ostera <leandro@ostera.io> -->

# Local calls, saved Y roots and private-entry yields

Status: **Selected instruction-profile growth toward M3; stateful acceptance still red.**

Baseline: `4875a087`, following [public Isolate integration](0003-public-isolates.md).
The unchanged stateful fixture needs ordinary local recursion and saved stack
registers as well as the still-missing process/closure/mailbox machinery. This
slice admits those call/frame consumers through the existing loader/interpreter;
it does not synthesize persistent state or replace the fixture.

## Admission and execution

The generic verifier now admits `call/2`, `call_only/2`, `call_last/3` and `swap/2`.
Local call destinations must name actual function-entry labels with matching
arity, not arbitrary labels or the `func_info` error path. A normal call requires
a frame for its continuation; tail calls must have no frame or pop exactly the
verified frame. The same caller root/liveness and return-register rules apply
to local and external calls. Swap requires both initialized register sources and
valid register destinations. Every function is still checked before publication.

The dispatch projection now contains **69 whole generated cases**, up from 37:
local-call/move-call families, Y moves, swaps, move/deallocate/return, external-tail
variants, and Y initialization. Existing exact upstream transform cases select
these words; no generic-operation evaluator, alternate bytecode or disabled
optimization rules were introduced. The selected profile still rejects generated
cases/helpers that have no admitted consumer.

Source/provenance remains the pinned `beam_makeops` inputs plus the existing
execution manifest's `code_ix.h` and native code-header layout. `yb`/`Qb` follow
the native register/stack addressing convention. Newly reached `call_dest` relative
operands receive the same explicit signed recovery as earlier jump operands;
packed and full-word negative offsets must not undergo unsigned C pointer
arithmetic. Whole generated bodies preserve read-before-pop ordering for Y-valued
tail calls and returns.

A reduction yield can occur at a private, non-exported function. Export-only MFA
lookup would misclassify that valid PC as malformed. The executor now also uses
the emitted function directory and native header to find the exact private entry.
It copies the MFA into process-owned storage with `memcpy`, avoiding a struct alias
into instruction-word storage. Header offsets derive from the admitted C layout.
This is a scheduling identity for real code, not an invented export or lookup stub.

Local frames remain in the existing physical stack and collector root walk.
Continuation slots and initialized Y roots survive copying GC; admission still
rejects logical reads before initialization. These calls do not add a new allocator,
process type, scheduler, native capability, global table or lifetime graph.

## Executable evidence

`libbeam/tests/fixtures/local_slice.erl` is ordinary compiler input. It exercises:

- tail calls to a private identity function;
- a saved binary Y root across a private call and real module-info copying/GC;
- reading a Y argument before popping a tail-call frame;
- swaps and tuple construction around nested calls;
- infinite recursion alternating exported/private entries, with bounded reductions.

`core_local_test.c` executes with an instruction budget of one and forces GC after
each yielded dispatch. Results include the actual copied 65-byte binary and a
tuple containing two binary roots. A separate suspended-frame failure test refuses
tospace allocation while a local continuation and binary Y root are live, checks
that old heap/roots/status are unchanged, retries collection, and completes.

Four malformed compiler-bytecode mutations test arity mismatch, a `func_info`
label as call target, a normal call without a frame and a mismatched tail-pop size.
Each refuses admission, then the unchanged image loads successfully. **89 loading
allocation-failure prefixes** restore allocation balance and allow retry.
The private recursion test repeatedly exhausts reductions and observes the real
private MFA, rather than accidentally succeeding only through instruction-budget
yields that avoid the scheduling lookup.

The existing public C++ Isolate test also loads this module and invokes its identity,
GC and tail paths on the actual worker, verifying copied binary results. Stock OTP
runs the same value assertions separately as reference evidence. Existing Engine,
world, binary, exception, failure, retirement and unchanged-example checks remain.

Initial integrated evidence: `/tmp/libbeam-local-first/summary.json`.
Final integrated evidence: `/tmp/libbeam-local-commit-reviewed/summary.json`.
Tooling: `/tmp/libbeam-local-tooling/summary.json`.
The final run includes Debug/Release, UBSan, forced-GC/rollback tests, reproducible
projection and source hashes under the shared validation lock. Fixture BEAMs are
external only. Neither acceptance example source changes.

Missing maps, closures, PIDs/spawn, references/monitors, message queues, registration,
timers, binary construction and numeric/native breadth remain explicit. These
instructions are a dependency of M3, not completion of M3 or the later Midgard
pure-Elixir application experiment. No new sanitizer/platform/performance claim.
