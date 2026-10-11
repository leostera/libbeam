# RFD 0003 — complete native binary construction

Follow-on to [owned binaries](0003-owned-binaries.md), the loader/flow proof,
owned virtual binary heap, and executable writable initializer. Baseline:
`0f02c92e`. This advances M3; it does **not** satisfy M3/M4 or the unchanged
stateful-Isolate acceptance contract.

## Implementation boundary

`project_dispatch.py` admits the entire native `i_bs_create_bin_jIWdW` case.
It hashes the original generated case and counts dependency substitutions;
unreviewed changes fail generation. Complete sizing/allocation phases delegate
to the previously reviewed C transplants. The complete native writer switch,
all nine extended-error branches, and relative guard destinations remain.
This is standard BEAM transformed/emitted as native words, not another bytecode,
fixture evaluator, or a whole-ERTS fallback.

`binary_build.c` coordinates explicit process ownership:

- Perform the native ordered sizing pass before storage allocation. Check the
  whole descriptor span and all register operands. A later guest sizing error
  still precedes an earlier aggregate host-size ceiling. Deferred writer errors
  remain deferred; a host ceiling may stop construction before those writes.
- Retire dead physical X aliases using the proven `Live` bound, reserve the real
  term heap, and reload X/Y source operands after any collection. A Y destination
  is resolved again only at final publication. Successful `Alloc` heap credit
  remains available for subsequent tuple/list constructors.
- Cover fresh heap/refcounted binaries, normal append, zero extension, private
  in-place/growth and frozen fallback through the owned storage APIs. Private
  producer/consumption proof is separate from dynamic uniqueness. A stored alias
  refuses execution before mutation; it never becomes guest badarg.
- Keep the result local while writing; publish only after exact cursor completion.
  On error, expire the writer and temporary result before payload allocation can
  collect. Abandoned backing remains process-owned and can be swept immediately.
  No writer, register pointer, or partial result survives an instruction yield.
- Explicitly translate sizing classification atoms to bare native exception
  codes for error preparation. Preserve native segment/operation/detail/value,
  including the width value and `invalid` detail for invalid float widths.
  Follow the actual guard label or existing terminal exception expansion.

The scalar native reason/flags and extended map are real. SAVETRACE and other
pending action flags are **not** claims of completed `handle_error`, trace
capture, catch/unwind, exit signaling, or native logging.

## Infrastructure and scheduling

Execution OOM and owned size ceilings remain terminal infrastructure outcomes.
Ownership/span refusal and floating-point environment failure now also have
separate process/task/world outcomes. Public messages are:

- `execution outside admitted ownership profile`
- `floating-point environment failure`
- `runtime resource limit exceeded`
- `transport limit exceeded` (the existing, separate copied-result limit)

No infrastructure failure becomes a false guard or a guest error payload.
Methods remain worker-only under Engine serialization; the C++ layer retains
C handles, not a separate runtime graph.

Work charges conservatively cover descriptor passes, dead-X clearing, intended writer bytes,
actual initialized/copied storage bytes, bignum-limb and private-alias scans,
and successful collection cost. Storage operations report byte work separately
from reduction charging. A success or guard exit can yield at its next real PC,
with the result or failure roots already stable; resumption does not replay the
constructor. A bounded 64 MiB write, allocator callback, or full collection may
still overrun a slice. This is not hard fairness, latency, or an aggregate quota.

## Evidence

`core_binary_build_test` loads fresh `binary_construct_slice.erl` and standard
OTP-validated `binary_control_slice.S`. It consumes all constructor kinds through
real dispatch: dynamic/fixed integer, float and binary fields, binary-all,
strings, UTF8/16/32, literal bignums, normal/private/frozen append, and disjoint
prefix copies sharing a boundary byte. Other checks include:

- Moving Y roots/destination and following tuple allocation credit.
- Native error maps, system-limit sizing, actual guard failure and unchanged
  incoming destination, and the pinned interpreter's UTF8 error order.
- Both fresh-backing and tospace allocation failures with fresh-process retry in
  the same code space and exact resource balance.
- A real partially written offheap result followed by a float error: error-map
  collection sweeps abandoned backing; injected error-payload OOM never publishes
  the destination and destruction releases both input and partial backing.
- Alias refusal without source mutation, no-allocation host limit, and a large
  constructor yielding after publication/PC advance with no allocation replay.
- Actual collection and physical process/code/Engine retirement.

`api_binary_build_test` exercises the real worker and copied public transport:
nonempty constructor results, private append, guard failure, ownership refusal,
guest error, distinct runtime/transport limits, peer progress while a constructor
loop runs, cancellation/stop, peer execution afterward, replacement admission,
and a copied reply surviving physical retirement.

The stock-only driver compares **683 result vectors**. The error-order comparison
is separate and deliberately not normalized:

| Execution | Invalid UTF8 followed by negative integer size |
|---|---|
| Pinned interpreter semantics, actual owned dispatch | `2:integer:size` |
| Installed OTP 28 ARM JIT | `1:utf8:type` |

Final evidence: `/tmp/libbeam-build-final-reviewed/summary.json` and
`/tmp/libbeam-build-tooling-reviewed/summary.json`. The fresh locked runner checks
source hashes before/after, Debug/Release CTests, fixture execution/public tests,
UBSan, stock vectors, source provenance and production symbol boundaries.
Generated `.beam` files are not committed. Earlier integrated evidence is at
`/tmp/libbeam-build-integrated-first/summary.json`.

## Remaining boundary

Binary matching contexts/instructions, broad map/fun representations, closures,
process identities, mailboxes, monitors, registry, timers, and full exception
handling remain absent. Conservative interprocedural append inference is still
not provided. The broader comprehension fixture remains independently rejected
for matching. The unchanged `two_isolates.cpp` remains red for unsupported
profile/import; lifecycle acceptance remains green. M5 Midgard compilation and
real-Isolate execution must still follow M0–M4. No pinned rebuilt interpreter,
Linux/platform, ASan/TSan, security or performance acceptance is inferred here.
