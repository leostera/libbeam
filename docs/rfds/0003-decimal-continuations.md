<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 Leandro Ostera <leandro@ostera.io> -->

# Decimal formatting and owned native continuations

Status: **Implemented selected-profile growth toward M3, not stateful acceptance.**
Baseline: `1d1a7241`, after [numeric addition](0003-numeric-addition.md).

The counter's integer conversion now executes a real `integer_to_binary/1` BIF.
Small and canonical native bignum inputs produce ordinary owned binary terms;
non-integers raise `badarg`. No decimal fixture answer, helper VM, format-specific
term representation or host-side guest execution is used. The positive catalog
has four implementations: module-info `/1,2`, addition `/2` and this formatter.
`integer_to_binary/2` and other numeric/string BIFs remain unadmitted.

## One native algorithm, explicit tradeoff

`decimal.c` selects OTP's `write_big_simple` schoolbook renderer and `D_div`
algorithm directly. The flat-64 decimal table entries are 17 digits per pass and
divisor 10^17. The quotient/remainder algorithm is unchanged; bounded indices
replace the original pointers decremented before the allocation at loop end.
`Sint_to_buf` supplies the small-value path, with unsigned magnitude avoiding
signed-negation overflow at `INT64_MIN`.

This is **not** partial admission of the reference `write_big` dispatcher. Its
D&C/Barrett/Karatsuba branches are not copied, disabled behind false predicates or
claimed implemented. There is one selected schoolbook algorithm for all canonical
native integers, preserving results but not the reference's large-number
performance. Porting those algorithms and their allocation graph is deferred;
refusing bignums or silently truncating at a small boundary was not acceptable.
No generated opcode or transformation case is added by this slice.

## Ownership and scheduling

One checked, fallible domain allocation holds the continuation, a copy of the
integer's limbs and output storage. A conservative integer-only bound of 20 bytes
per 64-bit limb plus sign avoids floating-environment-dependent sizing. Native
arity remains at most 65,535 limbs. The continuation retains no borrowed term,
heap, literal or code pointers; its process independently retains its module.

Formatting resumes only between complete division passes. Each BIF invocation
uses a 4,096-limb work budget, with at most one additional limb vector's work at a
pass boundary. Actual division work feeds the reduction count. Initial copying,
final binary publication and collection are not split by this budget. Neither a
hard latency bound nor efficient asymptotic conversion is claimed.

`LbProcess.decimal` is a typed, process-owned native continuation. Only the real
`integer_to_binary/1` trampoline may resume it. An incomplete conversion returns
to the existing Engine worker's scheduler at that same PC; no fake guest return
or exception is published. Other runnable tasks can progress. Host polling does
not resume the conversion. This is not general trapping-BIF, dirty-scheduler,
catch/unwind or suspended-Isolate support.

When complete, independent scratch survives possible copying collection and the
existing owned binary constructor copies its bytes into heap or refcounted storage.
Scratch is freed on success, output/collection OOM and physical process destruction.
Thus cancellation and world stop also release partial conversions. A failed
explicit collection during a pending conversion preserves the old roots and
continuation and can be retried. Execution OOM remains terminal; fresh invocations
can retry. Offheap allocation failure after a successful collection need not undo
that collection, but leaves no scratch or payload leak after process retirement.

## Evidence

`decimal-sources.json` pins the selected native sources and records these
adaptations. `core_decimal_test.c` separates component, real execution and worker
lifetime evidence:

- 10,000 signed 128-bit formatting comparisons; independent decimal-digit
  doubling through 8,192 bits; copied-input lifetime, allocation failure/retry,
  work bounds and partial retirement at the maximum native limb count.
- Ordinary compiled `decimal_slice.erl`: smalls, addition-to-formatting, negative
  bignums, 64/65-byte heap/offheap boundaries and a 2,467-byte result. Collection
  across native continuation yields, failed collection with retry, busy code
  retirement, and destruction while a maximum-width conversion is pending.
- Small-output collection OOM plus all three big-output allocation prefixes:
  scratch, copying space and refcounted output. Fresh-call retries succeed and
  all allocations balance. All **106** loading allocation-failure prefixes also
  roll back exactly and permit same-space retry.
- The actual shared worker allocates the large continuation; a peer world returns
  while conversion remains pending. Stop cancels it and physically releases that
  exact allocation before reclamation succeeds. The peer survives, and a later
  78,914-byte result correctly exceeds the public 64-KiB transport limit.
- Public C++ copied calls, timeout without cancellation, peer progress and explicit
  stop during formatting. Separate stock OTP value/length assertions remain
  reference evidence, not execution in this runtime.

Evidence directories: `/tmp/libbeam-decimal-first`,
`/tmp/libbeam-decimal-worker`, `/tmp/libbeam-decimal-integrated-first`, and
`/tmp/libbeam-decimal-commit-reviewed` (final Debug/Release **15 CTests**, direct
fixture/public execution, UBSan and physical retirement). Tooling evidence:
`/tmp/libbeam-decimal-tooling`. All runs use the user-wide lock; generated BEAM
artifacts stay outside source control.

Both acceptance sources remain unchanged. Engine lifecycle passes; stateful
Isolate acceptance still refuses unsupported features. M3/M4 remain incomplete:
closures, process identities, persistent processes, references, mailboxes,
monitors, registration, timers and required binary construction still need real
implementations and retirement. No additional platform, pinned compiler,
ASan/TSan, security, quota, throughput or full Erlang/Elixir claim is made.
