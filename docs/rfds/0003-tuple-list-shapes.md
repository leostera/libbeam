<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 Leandro Ostera <leandro@ostera.io> -->

# Tuple/list consumers and shape proofs

Status: **Implemented selected-profile growth toward M3, not stateful acceptance.**
Baseline: `e19d9d2c`, after [exact comparisons](0003-exact-comparison.md).

Message matching needs ordinary tuple/list tests and extraction. This slice admits
those consumers, cons construction and tuple-arity dispatch through the existing
native loader and interpreter. It adds no BIF, alternate executor or process/mailbox
implementation. The native capability catalog still contains only module-info BIFs.

## Prove before executing unchecked native reads

`beam_verify.c` extends the existing must-definition/frame/heap-credit analysis.
Each X/Y register has either no shape fact, a nonempty-list fact, or a tuple fact
with a minimum arity. Facts come from real constructors, literal representations and
successful runtime tests—not compiler hints. An unchecked tuple extraction requires
a proven tuple with an in-bounds element index; list extraction requires a proven
nonempty list. `test_arity` itself requires a tuple proof before its unchecked header
read. Tagged-tuple tests check a positive arity and an atom tag.

Moves and swaps preserve the source fact, including aliasing moves. Overwrites clear
it. Calls invalidate X facts but preserve initialized Y roots. Allocation/deallocation
and liveness trimming clear discarded facts. Join points retain only common facts;
tuple minima meet at the smaller bound. A weakened fact requeues the node even when
its defined-register bitmap did not change. Failed tests propagate **incoming** facts,
never their success refinements. Backedges participate in the same fixed point.

Every tuple-arity selector target has its own success refinement. Default/error
edges retain the incoming facts. Same-function `func_info` labels remain terminal
exceptions, not reentry into normal argument flow. Duplicate arities and foreign or
undefined targets refuse admission. All functions are checked, including unused ones.
Private entry arguments are conservatively unknown; compiler-inferred private-call
preconditions are not trusted or inferred interprocedurally. Some otherwise valid
compiler outputs can therefore still be refused by this selected profile.

### Hint canonicalization

After proof succeeds, executable X/Y operands lose their numeric type-hint fields.
This matters beyond type specialization: a native adjacency predicate comparing
raw operands could mistake `x1023` followed by hinted `x0` (numeric descriptor 1024)
for a contiguous two-register destination and write past the register file. The
canonical descriptors are 1023 and 0, which cannot select that contiguous operation.
Register identity/semantics remain unchanged. Raw image/preparation metadata remains
available; only the private executable operation stream is normalized. A regression
covers this boundary. Future type-specialized helpers require independently proven
facts, not renewed trust in file hints.

### Workspace ownership and limits

The proof uses one checked, zeroed domain allocation for nodes, labels and worklist.
It is physically released on every success/refusal/OOM path, before transformation
or publication. Large fact arrays are not retained in each loaded module. Workspace
is limited to 64 MiB; a selector admits at most 256 distinct arities. These are finite
admission limits, not total Engine quotas, density claims or full BEAM compatibility.

## Native instruction and table paths

Dispatch grows from **82 to 163 whole generated cases**: tuple/list predicates,
combined tests, X/Y extraction, consecutive tuple-field reads, cons/update-list forms
and the shared native tuple-selector groups. Their shared value-selector aliases do
not admit generic `select_val`; that loader capability remains unsupported.

The whole generated `select_tuple_arity` helper now uses fallible owned operation and
argument allocations. Operand/label pairs use the native `beam_transform_helpers.c`
comparison and `qsort`; both specialized two-arity and general sentinel-table paths
execute. Temporary helper arrays remain in the existing preparation arena. No rules
are disabled or replaced by successful-looking optimization misses.

Packed labels retain the native two-signed-32-bit-offsets-per-word layout. Interpreter
reads use `memcpy`, avoiding a `Sint32 *` alias into `BeamInstr[]`; signed values are
recovered before pointer arithmetic. Tests execute a real backward packed-table
jump to a function-clause boundary as well as reordered arity keys through native
sorting. No fabricated dispatch table answers are used.

Cons instructions debit two words of verified heap credit and check physical space.
Combined nonempty-list/allocate cases initialize physical Y roots before any host
instruction-budget safepoint, while logical reads still require initialization.
`execution-sources.json` pins the added source-helper and operand-decoding references.

## Evidence and remaining scope

`core_shape_verify_test.c` tests the normalized-operation proof boundary: false edges,
late weaker predecessors, tuple-bound meets, backedges, overwrites, moves/swaps,
constructor facts, call clobbers, Y retention and hinted register boundaries. These
are component tests, not guest execution evidence.

`shape_slice.erl` is ordinary compiler output. `core_shape_test.c` executes tagged
and untagged tuples, proper/improper lists, construction and both selector forms,
including empty tuples, misses, saved Y values and metadata calls. Collection is
forced after each yielded dispatch. Three malformed image mutations reject unproved
or out-of-bounds extraction and then permit valid-image retry. **127** loading
allocation-failure prefixes roll back exactly and permit retry, including proof
workspace, selector operation/argument arrays and emission/fixup storage.

The public C++ Isolate test runs tuple/list construction and extraction on the real
worker with copied binary inputs/results. Stock OTP assertions are recorded separately.
The acceptance sources are unchanged: real Engine lifecycle passes; the stateful
module still refuses missing process/language imports or instructions.

Evidence:

- `/tmp/libbeam-shape-first/summary.json`: first component/fixture execution.
- `/tmp/libbeam-shape-integrated-first/summary.json`: integrated execution.
- `/tmp/libbeam-shape-expanded/summary.json`: table/shape/hint regressions.
- `/tmp/libbeam-shape-commit-reviewed/summary.json`: final Debug/Release, 13 CTests,
  direct fixture execution and UBSan under the shared validation lock.
- `/tmp/libbeam-shape-tooling/summary.json`: tooling suite.

M3 remains incomplete. Persistent processes, PIDs, closures, references, mailboxes,
monitors, registration, timers and required numeric/binary construction still need
real implementations and retirement. There is no security, hard fairness, performance,
Linux, ASan/TSan, full Erlang/Elixir or M3/M4 completion claim in this record.
