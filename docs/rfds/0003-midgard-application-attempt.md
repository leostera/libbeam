<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 Leandro Ostera <leandro@ostera.io> -->

# RFD 0003: Midgard whole-application attempt

Status: **Compilation demonstrated; real-Isolate admission refused. No application execution success.**

The original attempt below is historical. See [Current batch retry](#current-batch-retry)
for the latest runner, profile, checks and concrete refusal.

This is an implementation snapshot for [RFD 0003](0003-additive-runtime-construction.md),
not an expansion of the positive runtime profile. M3–M4 acceptance was completed
before this attempt. The unchanged lifecycle and two-Isolate examples pass Debug,
Release and available UBSan; persistent workers, moving GC, allocation prefixes,
32 replacements, 4096 correlated requests, duplicate-bootstrap exceptions, queued
retirement, detached replies, peer progress and exact final allocation balance are
recorded in `/tmp/libbeam-monitor-errors-integrated-final/summary.json`.
After the application-boundary regression below, fresh full validation passes at
`/tmp/libbeam-midgard-boundary-integrated-final/summary.json` as well.

## Application and compiler identity

The existing sibling Midgard checkout is
`/Users/leostera/Developer/github.com/leostera/midgard`, revision
`eeea8607d0e63efc0f436e3bd90cec35e0940b0f`. No tracked Midgard sources were changed.
The selected whole application is `examples/hello_worker`, not a reduced replacement
or renamed fixture: two project modules, the three-module Midgard SDK and all
27 Jason dependency modules. The artifact contains 32 exact-name BEAMs, 356048
bytes total, built with Elixir 1.19.5 and installed OTP 28/ERTS 16.3.

The trusted compiler/tooling VM is distinct from libbeam's pinned interpreter
source. It performs local Mix compilation, static inspection and wire-reference
construction only. It is not a runtime fallback. Existing dependencies were
available; no dependency or application code was replaced with successful stubs.

Artifact: `/tmp/libbeam-midgard-hello-attempt/hello_worker.midgard`.
Manifest SHA-256:
`9952155ebfee13529cea032b9d8db6273285e35d58618059a34f6d3de83c13d7`.
The manifest retains compiler identities, every module checksum/size and dependency
ownership. All file sizes/checksums were checked before handing bytes to libbeam;
Midgard's inspector also checked structure/internal module identities. Generated
BEAMs are retained externally, not committed. Recompiling elsewhere may change
compiler metadata and hashes; compare the resulting manifest, not only its name.

## Reproducible compilation

Set paths to the sibling checkout and a fresh external output directory:

```sh
midgard=/Users/leostera/Developer/github.com/leostera/midgard
out=/tmp/libbeam-midgard-hello-attempt
cd "$midgard/examples/hello_worker"
export ERL_FLAGS='+S 1:1 +SDcpu 1:1 +SDio 1 +A 0'
export MIX_BUILD_PATH="$out/mix-build"
mix --version
mix test
mix midgard.build --output "$out/hello_worker.midgard"
mix packer.inspect "$out/hello_worker.midgard"
mix packer.inspect "$out/hello_worker.midgard" --target current-core
```

Native validation and these tooling commands were serialized using
`/tmp/otp-realm-validation-<uid>.lock`. Compilation used a fresh build directory,
not the checkout's existing packaged artifact. Logs and exact command/environment
records are in `compilation-summary.json` and adjacent named logs under `$out`.
Application tests: **4 tests, 0 failures**. Build and structural inspection: **0**.
Static `current-core` inspection: **1**, reporting 49 known unsupported findings
and 154 unresolved imports. That static profile is conservative and includes
`make_fun3`, which the additive runtime now admits in a bounded local-fun profile;
it is not the authoritative runtime positive list or an execution test.

The declared entrypoint is
`Elixir.HelloWorker:__midgard_call__/1`, protocol `midgard.bytes.v1`.
The trusted SDK constructed a POST `/echo` request with body
`real Midgard application` and checked the expected status-200 response on ordinary
OTP. Exact generation commands and scope are retained in
`wire-reference-command.json` / `wire-reference.log`; `request.bin` and
`expected-response.bin` are separate host-owned wire files. That reference result
is **not** reported as a libbeam result.

## Actual Isolate attempt

The existing `libbeam/tests/api_world_test.cpp` now has a generic
`--application` mode. It is an admission/execution witness, not another runtime,
a manifest parser or a fixture evaluator. It creates the actual public Engine and
Isolate, loads each supplied BEAM without changing its internal name, attempts the
declared binary arity-one entrypoint, checks a returned binary against independently
supplied expected bytes if one is returned, and physically reclaims/shuts down.
The normal regression harness still uses its original mode. A supported
`async_slice:identity/1` invocation tests this generic path on the real shared worker.

After a full build, invoke the artifact using all verified files:

```sh
build=/tmp/libbeam-midgard-boundary-integrated-final/build
"$build/api_world_test" --application \
  Elixir.HelloWorker __midgard_call__ \
  "$out/request.bin" "$out/expected-response.bin" \
  "$out/hello_worker.midgard/modules/"*.beam
```

Result in fresh **Debug, Release and UBSan** binaries: exit **1**, all 32 module
loads refused, no application module published. The actual `start` attempt returns
`unknown arity-one MFA`; no guest application result exists. Each attempt ends with
`loaded=0 refused=32 returned_binary=false expected_match=false reclaimed=true
engine_shutdown=true`. There is no serving OTP VM, module renaming or invented
reply. The supported-mode check exits 0, proving this is not a control-only facade.

Final exact commands/results are in `$out/final-isolate-summary.json` and
`real-isolate-final-{build,release,engine-ubsan}.log`. Component loader diagnostics
are separate from public-Isolate evidence:

- `Elixir.HelloWorker`: status 9, `link/admission`, offset 702.
- `Elixir.Midgard.Protocol`: status 9, `link/admission`, offset 2110.
- Both component rejection tests verify unchanged atom/allocation state.

## Concrete capability gaps

`concrete-capabilities.log` records imports and ordinary OTP disassembly from the
exact artifact. The application entry requires `get_map_elements`, `put_map_assoc`
and `try`/`try_case`/`try_end`, which are outside the admitted map/exception profile.
It imports unavailable `Elixir.Map:get/3`, `Elixir.URI:decode_query/1` and
`elixir_erl_pass:no_parens_remote/2`, as well as packaged modules which cannot yet
publish because their own required profiles/imports are unsupported.

The SDK requires `bs_start_match4`, match-position operations and binary extraction
beyond the current bounded integer/equality matching profile. It also imports
`Elixir.Enum`, `Elixir.String`, `binary:match/2`, `re:import/1` and unavailable native
services such as `byte_size/1`, `length/1`, `iolist_to_binary/1` and subtraction.
Jason adds larger map literals/operations, dynamic protocol dispatch and wider
binary/numeric/exception requirements. Alphabetical manifest order is not a load
plan; the current eager linker also lacks an unresolved multi-module batch protocol.
No general Elixir/OTP admission follows from local-fun or small-flatmap support.
These are concrete retained failures, not a reason to add unrelated opcode breadth
under this acceptance goal.

## Boundary regression found and fixed

The first actual artifact load aborted in Debug while decoding an empty `MAP_EXT`:
the already-admitted zero-entry map path called the native positive-arity tuple
header constructor for its empty key tuple. This was an internal loader defect,
not an acceptable unsupported-profile response. `beam_literals.c` now uses the
native zero-arity header, just like the existing empty-tuple producer; no larger-map
capability was added. A worker error/3/monitor fixture with empty error-info metadata
covers literal admission, copying, trace delivery and every allocation prefix.
The same trace shape passes the ordinary OTP reference under the explicit
no-line-info policy.

First abort logs/command records were preserved separately with `-first` suffixes.
After the fix the artifact consistently refuses through the normal API, reclaims
cleanly and leaves no falsely successful execution result. Full fresh additive
Debug/Release/UBSan validation passes after both the regression and generic harness
changes. No Linux, JIT, security, performance, density or quota claims are made.

## Current batch retry

The unchanged artifact still has the manifest hash recorded above. The retry
verifies every artifact BEAM's hash and byte size before calling the public
`run_beam` runner. This runner uses `Isolate::load_modules` once per group, not
sequential loading or the historical `api_world_test --application` mode.
POST `/echo` uses the original independent `request.bin` and
`expected-response.bin` reference files; no application result is fabricated.

Two fresh attempts after terminal `raise/3` support:

| Group | Stage | Module index/name | Refused import |
| --- | --- | --- | --- |
| All 32 artifact BEAMs | unresolved import | 0 / `Elixir.Enumerable.Jason.OrderedObject` | `Elixir.Enumerable.List:reduce/3`, import index 1 |
| Artifact plus recursive runtime imports, 377 BEAMs | unresolved import | 31 / `Elixir.Access` | `erlang:max/2`, import index 26 |

Exact commands, dependency checksums and runner output are retained in
`/tmp/libbeam-midgard-echo-artifact/` and `/tmp/libbeam-midgard-echo-closure/`.
The runner orders dependencies before its main module; diagnostic indices refer
to that actual submitted order, not alphabetical manifest order. Both report
`success=false reclaimed=true engine_shutdown=true`; `start` reports unknown
arity-one MFA. There are no returned guest application bytes. Import indices,
rather than an instruction offset, identify these failures.

Before those retries, a 377-module attempt exceeded its host timeout during
rollback. A process sample in `/tmp/libbeam-midgard-after-raise/sample.txt`
showed allocator ownership-list scanning beneath `lb_beam_program_destroy`,
not application execution or a worker deadlock. The orphaned diagnostic process
was terminated; that killed attempt proves no native reclamation outcome.
Batch rollback now aborts private atom staging before releasing preparation
arenas in reverse order. Small allocation-prefix tests still pass; the fresh
377-module refusal returns promptly and reclaims. No CPU-time guarantee follows.

Bounded maps, external functions and supplied terminal traces have component and
execution coverage, but remain local work pending a reviewed commit. Terminal
`raise/3` preserves supplied traces through GC and monitor delivery, returns
`badarg` on invalid arguments, and promotes uncaught throws to `{nocatch,Value}`.
Native catch/unwind and the application's remaining dependencies are not yet
admitted. HAMT full-hash collisions and keys outside immediates/proper byte lists
remain refused; this is not general OTP map support.

Fresh full validation after the rollback-order optimization and 256-module
rollback regression is recorded at `/tmp/libbeam-batch-cleanup-integrated/summary.json`
(the preceding terminal-trace gate is `/tmp/libbeam-raise-terminal-integrated/summary.json`):
`passed=true`, 213 commands,
50 Debug and 50 Release CTests, 31 existing UBSan tests plus seven profile tests.
Actual shared-worker trace tests include exact/one-below work budgets, a monitored
child, peer survival and private-world reuse. These fixture results do not prove
HelloWorker execution. The full application, remaining memory/failure boundaries,
repeat/independent-Isolate route acceptance and startup measurements remain open.

### Bounded max/2 checkpoint

The next native import, `erlang:max/2`, now uses the existing bounded ordering
profile and preserves OTP's first-operand choice on arithmetic equality,
including integer/float ties. Unsupported ordering terminates as a runtime
profile refusal, not guest `notsup` or a false guard. Non-GC BIF dispatch now
preserves scratch OOM, accounted memory denial and work exhaustion before any
guard-failure branch. `core_max_test` covers compiler-produced body/guard calls,
integer/atom/rank comparisons, tie representation, unsupported tuples, all work
credits through exact completion, scratch allocation failure and final balance.
`max_reference.erl` independently verifies the admitted answers on OTP and
confirms that refused tuple comparisons are valid OTP operations. This remains
bounded ordering, not full Erlang term ordering.

Fresh integrated evidence: `/tmp/libbeam-max-integrated/summary.json`,
`passed=true`, 215 commands, 51 Debug and 51 Release CTests, 31 existing plus
eight profile UBSan tests. Provenance: `libbeam/core/otp/max-sources.json`.

The subsequent unchanged 377-module POST `/echo` attempt is retained at
`/tmp/libbeam-midgard-after-max/`. Its next refusal is unresolved import
`erlang:setelement/3`, import index 37, module index 31 (`Elixir.Access`).
The runner still reports `success=false reclaimed=true engine_shutdown=true`.
There are still no application response bytes and no successful application
execution claim. These compatibility changes remain local, not yet committed.

### setelement/3 checkpoint

`erlang:setelement/3` now performs native argument checks and copies the source
tuple without mutating it. The heavy-BIF boundary requires moving X-register
operands and reloads both tuple and replacement after collection. Tuple-copy
work is charged before reservation; outstanding heap credit is preserved.
Accounted denial, callback OOM and work exhaustion remain distinct from badarg.

Tests cover compiler-produced calls and independent OTP results, first/middle/
last indices, invalid indices/types and empty tuples, shared nested values,
forced moving GC, every allocation-failure prefix, and every work-credit prefix
through exact completion both with and without collection. A separate real
shared-worker test verifies 32-KiB binary bytes, exact/one-below budgets, private
reuse, simultaneously live peers, reclamation and replacement. These are focused
fixtures, not application acceptance.

Fresh full gate: `/tmp/libbeam-setelement-integrated/summary.json`, `passed=true`,
217 commands, 53 Debug and 53 Release CTests, 31 existing plus ten profile UBSan
tests. Provenance: `libbeam/core/otp/setelement-sources.json`.

The unchanged 377-module POST `/echo` retry at
`/tmp/libbeam-midgard-after-setelement/` progresses to unresolved `erlang:*/2`,
import index 2, module index 35 (`Elixir.Calendar.ISO`). It reports
`success=false reclaimed=true engine_shutdown=true`. No application entrypoint
has executed successfully; compatibility work remains local pending review and
selective commits.

### Multiplication checkpoint

`erlang:*/2` and the complete generated native `i_times` instruction now execute
integer and mixed-float multiplication. Integer multiplication adapts the native
schoolbook base case with double-digit carries and accounted independent scratch;
no Karatsuba/performance claim follows. The full operand-digit product count is
charged before scratch allocation or multiplication. Native small normalization,
sign, badarith, guard behavior and the 65535-digit result ceiling are preserved.
Moving collection reloads interpreter aliases and recomputes X/Y destinations.
Float operations restore the worker's previous floating environment; failures
of environment APIs are infrastructure refusals, not guest errors or false guards.

Tests include independent OTP vectors, randomized double-digit reference products,
multi-limb carries, maximum-size success/overflow, default-budget denial before
quadratic work, all allocation and work-credit prefixes, body/guard outcomes,
forced moving GC with a compiler-produced Y destination, and shared-worker
binary results, exact/one-below budgets, private reuse and peers. The initial
integration exposed a stale test expecting guest `notsup` for injected environment
failure; it now requires `LB_PROCESS_ENVIRONMENT` and no published guest result
or exception value.

Fresh full gate: `/tmp/libbeam-times-integrated-second/summary.json`,
`passed=true`, 219 commands, 55 Debug and 55 Release CTests, 31 existing plus
12 profile UBSan tests. The UBSan build also exercises the native compiler-
overflow fast path. Provenance: `libbeam/core/otp/times-sources.json`.

The unchanged 377-module POST `/echo` retry at `/tmp/libbeam-midgard-after-times/`
now refuses unresolved `erlang:div/2`, import index 4, module index 35
(`Elixir.Calendar.ISO`). The runner reports
`success=false reclaimed=true engine_shutdown=true`; no guest application
response has been produced. The application goal remains incomplete.

### Integer division checkpoint

`erlang:div/2` and the complete native `i_int_div` case now execute. Integer
quotients truncate toward zero; joint preparation preserves dividend-signed
remainders and MIN_SMALL/-1 promotion. Noninteger operands and zero divisors
produce native badarith/body-or-guard outcomes. Single-digit division uses
indexed double-digit arithmetic; multi-digit division uses normalized Knuth-D
long division rather than a literal port of upstream's pointer-based `I_div`
or its Burnikel-Ziegler dispatcher. The bounded division rectangle and copy/
normalization spans are charged before scratch allocation and computation.
Both results are reserved and published together across GC. Neither rem/2 nor
floating division is admitted merely by this helper.

363 deterministic Python arbitrary-integer vectors are independently checked on
OTP and in the runtime, under all four sign combinations. Coverage includes
normalization and estimate-correction cases, signed remainders, guard failures,
native maximum-size quotients, forced Y-destination GC, joint output/root
preservation, every allocation and work-credit prefix, default-budget denial
before quadratic division, and shared-worker exact/one-below budgets, 32-KiB
responses, private reuse, peers and reclamation.

Fresh full gate: `/tmp/libbeam-div-integrated/summary.json`, `passed=true`,
221 commands, 57 Debug and 57 Release CTests, 31 existing plus 14 profile UBSan
tests. Provenance and algorithm adaptations: `libbeam/core/otp/div-sources.json`.

The unchanged 377-module POST `/echo` retry at `/tmp/libbeam-midgard-after-div/`
now refuses unresolved `erlang:abs/1`, import index 7, module index 35
(`Elixir.Calendar.ISO`). The runner reports
`success=false reclaimed=true engine_shutdown=true`. Application execution,
remaining compatibility work and selective reviewed commits remain outstanding.

### Absolute-value checkpoint

`erlang:abs/1` now preserves native small-integer promotion, positive bignum
identity, negative-bignum copying, positive float identity and signed-zero
normalization. Finite float sign handling is bit-exact and does not mutate the
floating environment. The verifier admits `gc_bif1` only for the actual abs
catalog entry. Its complete native body/guard groups permit moving heap changes
only for that positively linked function; other admitted BIFs retain their
nonallocating invariant. Negative payloads are staged in accounted independent
storage before collection, and destinations are recomputed afterward.

Independent OTP tests and native execution cover integer and float-bit vectors,
subnormal/finite extremes, both zeros, badarg/guards, original-source preservation,
maximum-size bignums, outstanding heap credit, forced Y-destination GC, all
allocation/work prefixes, and shared-worker exact/one-below budgets, 32-KiB
responses, private reuse, peers and reclamation.

Fresh full gate: `/tmp/libbeam-abs-integrated/summary.json`, `passed=true`,
223 commands, 59 Debug and 59 Release CTests, 31 existing plus 16 profile UBSan
tests. Provenance: `libbeam/core/otp/abs-sources.json`.

The unchanged 377-module POST `/echo` retry at `/tmp/libbeam-midgard-after-abs/`
now refuses unresolved `erlang:-/1`, import index 8, module index 35
(`Elixir.Calendar.ISO`). The runner reports
`success=false reclaimed=true engine_shutdown=true`. No successful application
execution or reviewed compatibility commit is claimed.

### Unary-negation checkpoint

Unary `erlang:-/1` now uses native zero-minus-integer semantics, including
MIN_SMALL promotion and normalization of its positive bignum counterpart back
to MIN_SMALL. Finite float negation flips the sign bit, including both signed
zeros, without changing the floating environment. The verifier positively
admits its gc_bif1 entry and projects the complete native constant/X/Y unary-
minus instruction group. Independent numeric staging precedes collection;
resource failures bypass arithmetic guards and destinations are recomputed.

Tests cover independent OTP integer/float-bit vectors, badarith/guard behavior,
source preservation, maximum-size bignums, outstanding heap credit, forced
Y-destination GC, every allocation/work prefix, and shared-worker exact/one-below
budgets, 32-KiB responses, private reuse, peers and reclamation.

Fresh full gate: `/tmp/libbeam-negate-integrated/summary.json`, `passed=true`,
225 commands, 61 Debug and 61 Release CTests, 31 existing plus 18 profile UBSan
tests. Provenance: `libbeam/core/otp/negate-sources.json`.

The unchanged 377-module POST `/echo` retry at `/tmp/libbeam-midgard-after-negate/`
now refuses unresolved `erlang:rem/2`, import index 14, module index 35
(`Elixir.Calendar.ISO`). The runner reports
`success=false reclaimed=true engine_shutdown=true`. Application execution and
reviewed compatibility commits remain outstanding.

### Remainder checkpoint

`erlang:rem/2` now reuses the joint quotient/remainder helper and executes through
the complete native `i_rem` group, including packed-X and general source fetching.
It preserves dividend-signed remainders, zero-divisor/noninteger badarith,
guard behavior and the tagged-small fast path. Moving collection restores
aliases and recomputes destinations; resource failures bypass guest guards.

The shared division/remainder test harness selects actual compiler-produced
entrypoints and independently checks the corresponding expected values. Both
operations pass 363 deterministic Python vectors under four sign combinations,
OTP reference checks, guard outcomes, forced Y-destination GC, joint result/root
preservation, allocation/work prefixes, default-budget denial before quadratic
work, and shared-worker exact/one-below budgets, 32-KiB responses, private reuse,
peers and reclamation.

Fresh full gate: `/tmp/libbeam-rem-integrated/summary.json`, `passed=true`,
227 commands, 63 Debug and 63 Release CTests, 31 existing plus 20 profile UBSan
tests. Provenance: `libbeam/core/otp/rem-sources.json`.

The unchanged 377-module POST `/echo` retry at `/tmp/libbeam-midgard-after-rem/`
now refuses unresolved `erlang:list_to_integer/1`, import index 19, module index
35 (`Elixir.Calendar.ISO`). The runner reports
`success=false reclaimed=true engine_shutdown=true`. Application execution and
selective reviewed compatibility commits remain open.

### List-to-integer checkpoint

`erlang:list_to_integer/1` now implements the ordinary wrapper's native decimal
semantics under that exact MFA. The small path preserves OTP's byte-cast behavior:
`[305,306]` and `[-207,-206]` parse as 12, while an 18-significant-digit fallback
validates the original list as bytes and rejects such heads. Signs must be exact
ASCII small integers; proper-list, syntax, leading-zero and small/big boundaries
are checked against an independent OTP oracle. Longer valid inputs use accounted
independent byte storage and the shared binary/list decimal parser, with charged
traversal/copying and growing-limb arithmetic. Capacity estimation allows a spare
limb before enforcing the actual normalized result ceiling; maximum-size success
is not claimed as an executed decimal-conversion test.

Failure tests exposed a pre-existing interpreter issue: call-setup memory denial
could reach `memory_failure` and be labeled OOM. That label now preserves the
original domain cause. BIF atomicity tests enter the real trampoline before fault
injection; separate tests cover call-setup denial. An explicit moving root, not
a dead X register that call setup may clear, witnesses source preservation.
Tests cover both list and binary paths, malformed/improper lists, native byte-cast
boundaries, long-input work denial, oversized result refusal, moving GC,
allocation/work prefixes and shared-worker exact/one-below budgets, peers, reuse
and reclamation.

The first full run stopped in an existing Python script because ambient
`python3` had changed to macOS Python 3.9.6 (`zip(strict=True)` is unavailable).
No runtime test was relaxed. Fresh validation selected `/opt/homebrew/bin/python3.14`
via `/tmp/libbeam-validation-python314/python3` at the front of the process PATH,
without changing system configuration.

Fresh full gate: `/tmp/libbeam-list-integer-integrated-python314/summary.json`,
`passed=true`, 229 commands, 65 Debug and 65 Release CTests, 31 existing plus 22
profile UBSan tests. Provenance: `libbeam/core/otp/list-integer-sources.json`.

The unchanged 377-module POST `/echo` retry at
`/tmp/libbeam-midgard-after-list-integer/` now refuses unresolved `erlang:min/2`,
import index 22, module index 35 (`Elixir.Calendar.ISO`). The runner reports
`success=false reclaimed=true engine_shutdown=true`. Application execution and
reviewed compatibility commits remain outstanding.

### Bounded min/2 checkpoint

`erlang:min/2` now shares max/2's charged, bounded ordering implementation,
preserving the first operand on arithmetic equality, including integer/float
ties. Unsupported comparisons remain explicit profile refusals, not false guards
or guest errors. The shared extrema harness tests both compiled operations,
independent OTP answers, scratch OOM/memory denial and every work prefix. A real
worker fixture verifies exact bytes, exact/one-below budgets, private reuse,
peer survival and reclamation. This is not general Erlang term ordering.

Fresh full gate: `/tmp/libbeam-min-integrated/summary.json`, `passed=true`,
231 commands, 67 Debug and 67 Release CTests, 31 existing plus 24 profile UBSan
tests, using the same Python 3.14 process-local PATH selection. Provenance:
`libbeam/core/otp/min-sources.json`.

The unchanged 377-module POST `/echo` retry at `/tmp/libbeam-midgard-after-min/`
now refuses unresolved `erlang:is_map_key/2`, import index 37, module index 36
(`Elixir.Calendar`). It reports `success=false reclaimed=true engine_shutdown=true`.
No application response or completion is claimed; compatibility changes remain
local pending selective review and commits.

### Bounded is_map_key/2 checkpoint

`erlang:is_map_key/2` shares checked flatmap/HAMT lookup with `map_get/2`.
Only a successful search returns true and only an actual miss returns false;
non-map inputs retain their native `{badmap, Value}` payload. Unsupported key
comparisons/hash classes remain profile refusals in body and guard paths.
This does not broaden the map representation or key-comparison profile.

Independent OTP and native fixtures cover flatmap/HAMT hits and misses, empty
maps, badmap payloads, guards and nested tuple keys. Binary HAMT queries remain
explicitly refused although valid in OTP. Nested flatmap lookup is tested with
a full memory quota and an allocator denying every allocation: it succeeds
without allocating or moving the heap. These tests do **not** claim OOM injection
into that allocation-free path. Work-prefix tests cover flatmap/HAMT hits and
misses and HAMT guards. Shared-worker tests preserve exact bytes, exact/one-below
budgets, private reuse, peer survival and physical reclamation.

Fresh full gate: `/tmp/libbeam-map-key-integrated/summary.json`, `passed=true`,
233 commands, 69 Debug and 69 Release CTests, 31 existing plus 26 profile UBSan
tests. Provenance: `libbeam/core/otp/map-key-sources.json`.

The unchanged 377-module POST `/echo` retry at
`/tmp/libbeam-midgard-after-map-key/` now refuses unresolved `erlang:map_size/1`,
import index 50, module index 37 (`Elixir.Code.Formatter`). The runner reports
`success=false reclaimed=true engine_shutdown=true`. Application execution,
remaining acceptance evidence and selective reviewed commits remain outstanding.

### Map cardinality checkpoint

`erlang:map_size/1` now reads the cardinality word of a validated flatmap or
HAMT root and returns a tagged small. It charges the constant-time map check,
does not traverse keys or move/allocate the heap, and preserves native badmap
payloads for non-map operands. The checked-map boundary is shared with the two
existing lookup BIFs; unsupported shapes remain explicit refusals.

Compiler-produced `gc_bif1 map_size` is positively admitted into the existing
complete native BIF groups without enabling heap movement for this BIF. The
initial greater-than guard fixture required unrelated unadmitted `is_ge`; the
focused fixture instead checks exact cardinality 33. No additional ordering
instruction is admitted or claimed. Disassembly evidence is retained at
`/tmp/libbeam-map-size-disasm/`.

The existing map harness, OTP reference and shared-worker fixture are extended
rather than duplicated. They cover empty/flat/HAMT sizes, badmap payloads,
exact-size guards, allocation-free nested-map inspection under denial, every
work prefix and exact bytes/budgets/reuse/peer survival/reclamation. Fresh full
gate: `/tmp/libbeam-map-size-integrated/summary.json`, `passed=true`, 233 commands,
69 Debug and 69 Release CTests, 31 existing plus 26 profile UBSan tests.
Provenance: `libbeam/core/otp/map-size-sources.json`.

The unchanged 377-module POST `/echo` retry at
`/tmp/libbeam-midgard-after-map-size/` now refuses unresolved `erlang:is_integer/1`,
import index 58, module index 37 (`Elixir.Code.Formatter`), with
`success=false reclaimed=true engine_shutdown=true`. Application execution and
remaining acceptance evidence are still outstanding; changes remain local.

### Integer predicate checkpoint

`erlang:is_integer/1` now shares the native small-or-bignum tag predicate with
complete X/Y and fused integer-guard/allocation instruction cases. The BIF is
allocation-free and charges one bounded native step. The fused allocation case
uses charged, fallible GC and initializes physical Y slots before any host-budget
safepoint; logical uninitialized reads remain rejected by the verifier. Failed
integer guards skip allocation, including with a full heap and denied allocator.

Native literal lowering overwrites scratch x1023 before either branch. The
verifier now discards its old shape facts on both edges. OTP's assembler rejects
explicit x1023 use, so stale-type acceptance/refusal is tested in the existing
normalized-operation proof harness, not claimed as compiler-produced execution.
Separate standard assembly fixtures execute integer/float literal guards, with
independent OTP answers. Ordinary compiler fixtures exercise actual body, X/Y
and fused guard cases, including a test that checks the fused opcode itself.

Tests distinguish smalls and positive/negative bignums from floats, atoms,
lists, tuples, maps, binaries, PIDs, references and local/external funs. Forced
collection covers every allocation prefix, distinct memory denial/callback OOM,
source-value survival and every work prefix. Shared-worker tests retain exact
bytes, exact/one-below budgets, reuse, peer survival and reclamation.

Fresh full gate: `/tmp/libbeam-integer-predicate-integrated/summary.json`,
`passed=true`, 235 commands, 71 Debug and 71 Release CTests, 31 existing plus 28
profile UBSan tests. Provenance: `libbeam/core/otp/integer-predicate-sources.json`.

The unchanged 377-module POST `/echo` retry at
`/tmp/libbeam-midgard-after-integer-predicate/` now refuses unresolved
`erlang:hd/1`, import index 65, module index 37 (`Elixir.Code.Formatter`). It reports
`success=false reclaimed=true engine_shutdown=true`. No application execution
or completion is claimed; remaining compatibility, acceptance and reviewed
commit work remains open.

### Cons head/tail checkpoint

`erlang:hd/1` and `erlang:tl/1` now share a charged nonempty-cons check with
explicit native head/tail field selectors. They return existing owned children
without copying or collecting. Improper tails are valid results; nil/non-list
operands produce badarg. Existing complete native guard/extraction cases handle
X-source fusion, while generic BIF groups handle body and Y-source guards.

Independent OTP and native fixtures cover proper/improper lists, tuple
heads/tails, guest errors and successful/failed X/Y guards. Tests check exact
source identity/immutability, returned boxed children through moving GC, and
every work prefix including error paths. Allocation-denial tests preserve the
mandatory VM return reserve: an initial test filled that reserve and correctly
hit the existing runtime invariant, so the setup—not the invariant—was fixed.
Y-guard injection begins at the actual BIF, after caller frame setup. Shared
worker tests recover exact binary bytes via both primitives and retain
exact/one-below budgets, reuse, peer survival and reclamation.

Fresh full gate: `/tmp/libbeam-list-parts-integrated/summary.json`, `passed=true`,
237 commands, 73 Debug and 73 Release CTests, 31 existing plus 30 profile UBSan
tests. Provenance: `libbeam/core/otp/list-parts-sources.json`.

The unchanged 377-module POST `/echo` retry at
`/tmp/libbeam-midgard-after-list-parts/` now refuses unresolved `erlang:is_list/1`,
import index 71, module index 37 (`Elixir.Code.Formatter`). It reports
`success=false reclaimed=true engine_shutdown=true`. Application execution and
remaining acceptance/reviewed-commit work remain outstanding.

### List predicate checkpoint

`erlang:is_list/1` now preserves native NIL-or-cons semantics, including true for
improper lists, without traversing tails. Complete X/Y guard cases are projected.
The positive generic-BIF whitelist is expressed per arity, retaining previous
entries and adding only this predicate. Literal guards account for x1023's
scratch overwrite on both branches. A successful list predicate does **not**
prove nonempty-cons access; normalized verifier tests reject unproven `get_hd`
while preserving an independent nonempty-list proof.

The shared predicate harness now runs separate integer and list modes, including
proper/improper/NIL cases, other term classes, compiler-produced body/X/Y guards,
standard assembly literal guards and independent OTP answers. List-mode GC is
forced in the allocation following its guard, not falsely described as a fused
list/allocation instruction. Tests retain the mandatory VM reserve and cover
allocation prefixes, callback OOM versus memory denial, every work prefix for
true/false/NIL paths, value survival, shared-worker exact bytes/budgets, reuse,
peer survival and reclamation. Existing integer-mode coverage also runs afresh.

Fresh full gate: `/tmp/libbeam-list-predicate-integrated/summary.json`,
`passed=true`, 239 commands, 75 Debug and 75 Release CTests, 31 existing plus 32
profile UBSan tests. Provenance: `libbeam/core/otp/list-predicate-sources.json`.

The unchanged 377-module POST `/echo` retry at
`/tmp/libbeam-midgard-after-list-predicate/` now refuses unresolved `erlang:not/1`,
import index 73, module index 37 (`Elixir.Code.Formatter`). It reports
`success=false reclaimed=true engine_shutdown=true`. Application execution and
remaining acceptance/reviewed-commit work are still open.

### Strict boolean not checkpoint

`erlang:not/1` now reverses exactly the true/false atoms and raises badarg for
other operands, without truthiness conversion. The charged, allocation-free
BIF uses the existing complete native BIF groups and positive unary whitelist.
A standard-assembly Y-source/Y-destination guard fixture exercises the actual
BIF, rather than relying on compiler-optimized comparisons. Independent OTP
answers cover both boolean outputs, body errors and guard failure.

Tests cover every work prefix for valid/invalid body and guard inputs and
allocation denial at the actual BIF after caller frame setup, retaining the
mandatory VM reserve. Shared-worker tests preserve exact bytes, exact/one-below
budgets, reuse, peer survival and reclamation.

Fresh full gate: `/tmp/libbeam-not-integrated/summary.json`, `passed=true`,
241 commands, 77 Debug and 77 Release CTests, 31 existing plus 34 profile UBSan
tests. Provenance: `libbeam/core/otp/not-sources.json`.

The unchanged 377-module POST `/echo` retry at `/tmp/libbeam-midgard-after-not/`
now refuses unresolved `erlang:is_atom/1`, import index 78, module index 37
(`Elixir.Code.Formatter`). It reports `success=false reclaimed=true engine_shutdown=true`.
Application execution and the remaining acceptance/reviewed-commit work are
still outstanding.

### Atom predicate checkpoint

`erlang:is_atom/1` now uses the native atom-tag predicate with one charged native
step and no allocation or collection. Booleans and ordinary atoms are accepted;
NIL is not an atom. The exact BIF joins the positive unary whitelist, reusing
already-admitted X/Y atom guards and native constant-literal lowering.

An explicit atom mode extends the shared predicate harness, while integer/list
modes run again with additional ordinary/false-atom samples. Independent OTP
answers cover body/X/Y/literal guards and other term classes. Tests force GC in
the allocation after an accepted guard—not in the predicate itself—and retain
allocation/work prefixes, distinct memory/OOM outcomes, value preservation,
shared-worker exact bytes/budgets, reuse, peer survival and reclamation.

Fresh full gate: `/tmp/libbeam-atom-predicate-integrated/summary.json`,
`passed=true`, 243 commands, 79 Debug and 79 Release CTests, 31 existing plus 36
profile UBSan tests. Provenance: `libbeam/core/otp/atom-predicate-sources.json`.

The unchanged 377-module POST `/echo` retry at
`/tmp/libbeam-midgard-after-atom-predicate/` now refuses unresolved
`erlang:atom_to_list/1`, import index 86, module index 37 (`Elixir.Code.Formatter`).
It reports `success=false reclaimed=true engine_shutdown=true`. Application
execution and the remaining acceptance/reviewed-commit work remain open.

### Unicode atom-to-list checkpoint

`erlang:atom_to_list/1` now materializes Unicode scalar values, not UTF-8 bytes.
It relies on the namespace's established canonical UTF-8/255-character invariant,
charges bounded decoding/construction work, reserves final cons storage while
preserving heap credit, and publishes only after construction. Namespace-owned
name bytes are immutable and nonmoving; no guest heap pointer or dynamic scratch
crosses collection. Empty names return NIL without allocation. The trusted-name
decoder is also reused by the existing Latin-1 atom-to-binary path.

Independent OTP/native vectors cover empty and embedded-NUL names, ASCII,
two/three/four-byte characters, scalar boundaries, and a maximum 255-character
name occupying 1020 encoded bytes. Tests check binary-conversion regression,
non-atom badarg, unchanged source/namespace, moving GC and heap credit. Direct
helper and actual heavy-BIF trampoline tests cover allocation prefixes, distinct
OOM/memory denial, and work prefixes with/without primitive collection for mixed
and maximum names. Shared-worker exact-byte/budget/reuse/peer/reclamation tests
also pass.

Fresh full gate: `/tmp/libbeam-atom-list-integrated/summary.json`, `passed=true`,
245 commands, 81 Debug and 81 Release CTests, 31 existing plus 38 profile UBSan
tests. Provenance: `libbeam/core/otp/atom-list-sources.json`.

The unchanged 377-module POST `/echo` retry at `/tmp/libbeam-midgard-after-atom-list/`
now refuses unresolved `erlang:is_binary/1`, import index 89, module index 37
(`Elixir.Code.Formatter`), with `success=false reclaimed=true engine_shutdown=true`.
Application execution and remaining acceptance/reviewed-commit work are still
outstanding.

### Binary predicate checkpoint

`erlang:is_binary/1` and complete native X/Y binary guards now use the owned
read-only bitstring metadata predicate. Byte length, not starting-bit alignment,
determines the result: an eight-bit sub-binary at offset three is a binary; a
seven-bit bitstring is not. The predicate neither reads payload bytes nor
allocates, collects, pins, or changes writable/volatile metadata. Literal guard
lowering clears x1023 shape facts on both verifier edges.

Independent OTP/native fixtures cover empty/heap/reference binaries, unaligned
sub-binaries, partial bitstrings, other term classes, compiler body/X/Y guards,
allocating continuations and assembly literals. The expanded shared predicate
harness checks moving GC, allocation/work prefixes, metadata/refcount preservation,
output survival and final balance. Collection occurs in the following allocation,
not in the binary predicate. Shared-worker exact-byte/budget/lifecycle tests pass.

Fresh full gate: `/tmp/libbeam-binary-predicate-integrated/summary.json`,
`passed=true`, 247 commands, 83 Debug and 83 Release CTests, 31 existing plus 40
profile UBSan tests. Provenance: `libbeam/core/otp/binary-predicate-sources.json`.

The unchanged 377-module POST `/echo` retry at
`/tmp/libbeam-midgard-after-binary-predicate/` advances to unresolved
`erlang:apply/3`, import index 3, module index 43 (`Elixir.Code`), and reports
`success=false reclaimed=true engine_shutdown=true`. This remains admission
progress, not successful application execution.

### Apply control-transfer checkpoint

`erlang:apply/3` is now a code-space-owned **instruction export**, not a fake
value-returning BIF. Complete native `i_apply`, `i_apply_last`, `i_apply_only`,
`apply`, and `apply_last` cases retain their continuation/deallocation behavior;
the compiler-required `swap2_xxx` case is also projected. External fun calls can
reach the real `i_apply_only` export trampoline. The BIF count remains 70; the
additional native instruction entry is included in dispatch-validation work.

`libbeam/core/apply.c` parses proper argument lists, flattens exact nested apply/3
triples iteratively, resolves only already-published exact exports, and acquires
a process code lease before publishing arguments. All fallible work precedes
argument installation. Fixed-arity badarg paths reserve and construct their
argument lists with charged moving GC, reload boxed invalid module/function
values, and preserve heap credit. Missing targets are profile refusals: no
autoload, lazy publication, error-handler dispatch or fabricated undef. Full
catch/unwind and stacktrace reconstruction remain outside this checkpoint.

Native tests cover all five actual compiler-produced cases, the external
instruction export, loaded/native targets, nested apply, argument/error bounds,
current-PC and stack-continuation code leases, moving roots, allocator and work
prefixes, and final balance. Verifier tests reject missing MFA operands, excessive
arity, mismatched frames and stale post-call registers. Shared-worker tests cover
exact bytes, work boundaries, safe reuse, peer survival and reclamation.

**Reference difference:** installed OTP 28 / ERTS 16.3 parses 256 and 257 argument
lists (a proper list reaches undef; an improper one yields badarg). Authoritative
`beam/erts/emulator/beam/erl_vm.h` at the pinned source declares `MAX_ARG=255`,
and its `beam_common.c` apply loop yields system_limit on the 256th cons. Native
tests preserve the pinned behavior; the independent oracle records rather than
hides this difference. `/tmp/libbeam-apply-otp-boundary/0.log` retains the probe.

The first fresh integration also exposed the decimal transport witness reaching
the default work limit before producing its 78,914-byte result. That dedicated
64 KiB-output-boundary witness now uses a separate World with explicit
`UINT64_MAX` work credit; default Worlds, production policy and work-budget tests
remain unchanged. Diagnostics retain the distinct outcomes. A subsequent direct
UBSan linker failure was fixed by adding `apply.c` to the standalone source list.

Fresh full gate: `/tmp/libbeam-apply-integrated-final/summary.json`, `passed=true`,
249 commands, 85 Debug and 85 Release CTests, 31 existing plus 42 profile UBSan
tests. Provenance: `libbeam/core/otp/apply-sources.json`.

The unchanged 377-module POST `/echo` retry at `/tmp/libbeam-midgard-after-apply/`
now refuses unresolved `erlang:function_exported/3`, import index 6, module index
43 (`Elixir.Code`), with `success=false reclaimed=true engine_shutdown=true`.
Application response bytes and remaining acceptance work are still outstanding.

### Published-export query checkpoint

`erlang:function_exported/3` now queries the Isolate's actual published export
namespace, including admitted native and instruction exports. Private/nonexistent
exports and absent modules return false without loading anything. Queries neither
allocate/collect nor create code leases or atoms; each visited export is charged.
The result describes available capabilities, not an invented complete OTP catalog.

The atom/atom/small boundary preserves native badarg. Explicit unsigned narrowing
preserves OTP's 32-bit arity behavior: small integers `4294967297` and
`-4294967295` query arity 1, while a bignum is rejected. Independent evidence is
retained at `/tmp/libbeam-function-exported-oracle/0.log` and in the integrated
OTP reference log. Tests cover loaded/native/instruction hits, private/missing
misses, type errors, all work prefixes, allocator/quota denial at the real BIF,
unchanged heap/source/namespace and code-user counts, separate live World scopes,
and shared-worker byte/budget/lifecycle behavior.

Fresh full gate: `/tmp/libbeam-function-exported-integrated/summary.json`,
`passed=true`, 251 commands, 87 Debug and 87 Release CTests, 31 existing plus 44
profile UBSan tests. Provenance: `libbeam/core/otp/function-exported-sources.json`.

The unchanged 377-module POST `/echo` retry at
`/tmp/libbeam-midgard-after-function-exported/` now refuses unresolved
`erlang:process_info/2`, import index 9, module index 43 (`Elixir.Code`), with
`success=false reclaimed=true engine_shutdown=true`. No application response
bytes have been produced yet.

### Error-handler process information checkpoint

`erlang:process_info/2` now supports the concrete `error_handler` selector used
by `Elixir.Code.can_await_module_compilation?/0`, including proper selector lists
and duplicates. Each owned process has its actual atom-only setting initialized
to the native default. Queries read the requested process through the World-owned
PID table under Engine serialization; missing/terminal targets return undefined.
Selectors are parsed before PID resolution. Other atom/tuple selectors remain
explicit profile refusals, not fabricated statistics. Custom error-handler
execution and `process_flag` have not been admitted.

Parsing, lookup and construction work are charged. Checked final heap reservation
preserves outstanding credit; the immediate handler value is read before any
caller collection. No borrowed target heap pointer crosses GC. Native tests cover
self/other/dead PIDs, singleton/list/empty shapes, actual nondefault private target
state, malformed inputs and unsupported selectors, moving GC, direct/actual-BIF
allocation prefixes, work prefixes, output survival and balance. Independent OTP
and shared-worker byte/budget/reuse/peer/reclamation checks pass.

Fresh full gate: `/tmp/libbeam-process-info-integrated/summary.json`, `passed=true`,
253 commands, 89 Debug and 89 Release CTests, 31 existing plus 46 profile UBSan
tests. Provenance: `libbeam/core/otp/process-info-sources.json`.

The unchanged 377-module POST `/echo` retry at
`/tmp/libbeam-midgard-after-process-info/` now refuses unresolved
`erlang:binary_to_term/1`, import index 44, module index 43 (`Elixir.Code`), with
`success=false reclaimed=true engine_shutdown=true`. No application execution
success is claimed.

### Temporary ETF arena preparation checkpoint

The shared native-derived ETF parser now distinguishes literal and temporary
storage explicitly. Temporary tuples, lists, bignums, binaries and HAMT nodes
carry no literal-pointer tags. It reports one-term consumption through the
bounded reader; existing literal callers still require the entire buffer.
Parsing/node and sized construction/validation loops now charge work. Atom
writes and external-fun bindings remain deferred, and partial arenas are never
safe to traverse or publish.

The new component test copies a temporary composite and HAMT into a process,
destroys the preparation arena, then checks moving-GC survival and absence of
literal tags, including HAMT descendants. Tests also cover deferred atoms without
interning, consumed length, every truncated prefix, allocator/quota and work
prefixes, and final balance. Independent OTP confirms scalar values, signed-zero
bits, offheap bytes, maps, trailing-byte consumption and truncation errors.

This is **preparation, not binary_to_term admission**. The runtime adapter still
needs atom transaction/publication, compressed-ETF handling, postprocessing work,
code leases and rollback-cost coverage. No value stub or alternate VM is added.

Fresh full gate: `/tmp/libbeam-etf-arena-integrated/summary.json`, `passed=true`,
255 commands, 90 Debug and 90 Release CTests, 31 existing plus 47 profile UBSan
tests. Provenance: `libbeam/core/otp/etf-arena-sources.json`.

The fresh unchanged-closure retry at `/tmp/libbeam-midgard-after-etf-arena/`
correctly still refuses `erlang:binary_to_term/1`, import 44, module 43
(`Elixir.Code`), with `success=false reclaimed=true engine_shutdown=true`.
Application acceptance remains open.

### Runtime ETF data decoding checkpoint

`erlang:binary_to_term/1` now uses that shared parser for a bounded **uncompressed
ETF data profile**. It snapshots byte-length binary input (including unaligned
sub-binaries), stages atoms, validates/orders maps, reserves process storage with
moving GC and preserved heap credit, and copies the native graph before committing
new atom identities. No fallible operation follows publication. Trailing bytes
are accepted; malformed admitted encodings and unknown tags produce `badarg`.
Known unadmitted encodings refuse explicitly: compression, legacy float encoding,
local/external funs, record/cache/distribution encodings and PID/port/reference
identities. Existing size and HAMT key/collision bounds remain.

Allocation registries now maintain an exact live-record count. Preparation
allocations incrementally reserve conservative non-LIFO teardown work. Atom
staging reserves hash-chain, rehash and interleaved-record cleanup work; runtime
flatmap comparisons charge traversal, and HAMT construction reserves bounded
hash/sort/build work. These conservative work units are not a hard CPU-time or
performance claim. Defaults remain 256 MiB and 10,000,000 invocation units.

`core_external_decode_test.c` covers actual compiled calls, every tested truncated
prefix, invalid/nonbinary/partial-bit input, unaligned binaries, maps, duplicate
keys, arena destruction followed by moving GC, heap credit, allocator/quota
prefixes, work prefixes and atom-publication atomicity. The shared-worker witness
checks exact/one-below thresholds, reuse, peer survival and reclamation. Fresh-atom
work prefixes are measured at the **BIF boundary**: a separate real-interpreter
witness confirms that later return-path budget denial retains already-committed
atoms. There is no whole-invocation state rollback guarantee.

Native catalog growth moved an existing causal-transfer witness outside its
old 4,000-unit window: the sender exhausted before replying. Only that dedicated
witness now uses 5,000 units; it still requires sender completion followed by
sender-attributed exhaustion during the receiver's 32-KiB host copy, world
containment and peer survival. Defaults and measured exact-threshold tests are
unchanged. Diagnostics and intermediate evidence are retained under
`/tmp/libbeam-external-decode-work-observed/` and
`/tmp/libbeam-external-decode-causal-witness/`.

Fresh full validation: `/tmp/libbeam-external-decode-integrated-final/summary.json`,
`passed=true`, 257 commands, 92 Debug / 92 Release CTests, 31 existing plus 49
profile UBSan tests. Independent OTP reference confirms admitted data/atom/map
values, unaligned input, trailing consumption and malformed-input errors.
Provenance: `libbeam/core/otp/external-sources.json`. Native BIF count is 73,
plus the separate `apply/3` instruction export.

The fresh unchanged 377-module attempt at
`/tmp/libbeam-midgard-after-external-decode/` now reaches
`erlang:module_loaded/1`, import 47, module 43 (`Elixir.Code`), and reports
`success=false reclaimed=true engine_shutdown=true`. No application response
bytes or application acceptance are claimed.

### Published-module query checkpoint

`erlang:module_loaded/1` now queries actual published code in the current Isolate,
not the export table. It requires an atom, charges the boundary and each visited
module, and returns immediate booleans without loading, interning, allocating,
collecting or acquiring leases. The real preloaded `erlang` module is visible;
missing modules are false and non-atoms raise `badarg`. Work denial remains a
resource outcome. The `on_load` opcode is still unadmitted: a compiled fixture
proves failed admission leaves no published module or namespace/memory residue.

Tests cover direct/native and compiled calls, full-heap allocator/quota denial,
all work prefixes, unchanged inputs and namespace state, separate live World
visibility, unload/reload without query-held leases, exact final balance, and
shared-worker threshold/reuse/peer/reclamation behavior. Independent OTP confirms
loaded/preloaded/missing, no-autoload, unload/reload and badarg cases. The reference
initially failed because its target fixture had not yet been compiled into the
reference directory; the driver now explicitly compiles that dependency.

Fresh full gate: `/tmp/libbeam-module-loaded-integrated-final/summary.json`,
`passed=true`, 259 commands, 94 Debug / 94 Release CTests, 31 existing plus 51
profile UBSan tests. Provenance: `libbeam/core/otp/module-loaded-sources.json`.
Native BIF count is 74 plus the separate `apply/3` instruction export.

The unchanged 377-module retry at `/tmp/libbeam-midgard-after-module-loaded/`
now refuses `erlang:get/1`, import 69, module 43 (`Elixir.Code`), with
`success=false reclaimed=true engine_shutdown=true`. No application execution
or response-byte acceptance is claimed.

### Owned process dictionary checkpoint

`get/1`, `put/2` and `erase/1` now use actual per-process state. The profile
retains native `ProcDict` prefix fields and NIL/direct-pair/collision-list buckets,
with eight fixed hash buckets rather than incremental rehashing. Atom and small
integer keys use native fast hashes; other key classes explicitly refuse, even
on an empty dictionary. `get/0`, `erase/0` and `get_keys` remain unadmitted.
Missing keys and prior values follow native semantics, including stored
`undefined`. No dictionary state is inherited by a spawned process.

Mutation is charged and publish-last. Immediate replacement is allocation-free;
other updates preserve heap credit and reload arguments/buckets after collection.
Allocation, quota and work denial preserve dictionary contents. Erase collapses
singleton collision lists. Metadata is independently accounted and reclaimed;
its slots join collector roots, code-marking bounds and alias checks. Tests prove
that dictionary-only binary values survive moving GC, and an external fun keeps
its defining module alive until erase plus GC permits actual unload.

Compiler `get/1` is a plain nonallocating BIF lowered to native `i_get_sd` or
`i_get_hash_cWd`, not an Erlang guard. Both complete cases are projected, cached
fast hashes checked, destination writes delayed until success, and lookup cost
included before schedule-out. Verification accepts no exceptional guard edge.
Explicit X/Y assembly, compiled child-isolation/repeated-request fixtures, all
mutation allocation/quota and primitive-work prefixes, heap-credit and final
balance checks accompany the real shared-worker lifecycle witness. Independent
OTP confirms the admitted dictionary behavior and register-location fixtures.

Fresh integrated evidence: `/tmp/libbeam-dict-integrated/summary.json`,
`passed=true`, 261 commands, 96 Debug / 96 Release CTests, 31 existing plus 53
profile UBSan tests. Provenance: `libbeam/core/otp/process-dict-sources.json`.
Native BIF count is 77 plus the separate `apply/3` instruction export.

The fresh unchanged-closure attempt at `/tmp/libbeam-midgard-after-dict/` advances
to `erlang:error/2`, import 17, module 51 (`Elixir.Enum`), with
`success=false reclaimed=true engine_shutdown=true`. Application execution,
response-byte acceptance and correct catch/unwind remain open.

### Terminal error/2 checkpoint

`erlang:error/2` now implements the native three-word `{Reason,Args}` wrapper
and `EXC_ERROR_2` flags. It does not validate Args as a proper list: NIL and cons
arguments (including improper lists) appear in the caller frame, while other
terms retain the caller's arity. Only Reason becomes the terminal exception
value. The raw wrapper remains rooted for monitor-reason trace construction.

The allocating primitive charges and reserves before publication, reloads its
actual X-register operands after collection, preserves primitive heap credit,
and distinguishes work/quota denial from allocator OOM. Tests separately account
for the compiled-call boundary's normal credit reset; forcing GC with stale
caller credit would not be a valid witness. Compiled terminal calls, arbitrary
argument terms, moving roots, allocation/quota/work prefixes, exact balance and
shared-worker spawn-monitor delivery/reuse are covered. Independent OTP confirms
the reason, improper arguments and caller-frame behavior. Catch/try and general
unwind are still unadmitted.

Fresh full gate: `/tmp/libbeam-error2-integrated-final/summary.json`, `passed=true`,
263 commands, 98 Debug / 98 Release CTests, 31 existing plus 55 profile UBSan
tests. Provenance: `libbeam/core/otp/error2-sources.json`. Native BIF count is 78
plus the separate `apply/3` instruction export.

The unchanged-closure retry at `/tmp/libbeam-midgard-after-error2/` now reaches
`erlang:make_tuple/2`, import 56, module 51 (`Elixir.Enum`), and still
reports `success=false reclaimed=true engine_shutdown=true`. No application
execution or response-byte acceptance is claimed.

### Native tuple construction checkpoint

`erlang:make_tuple/2` now checks the native small-integer range
`0..MAX_ARITYVAL`. Invalid counts are `badarg`; a natively valid count can still
hit runtime heap, accounted-memory or work limits. No successful maximum-sized
tuple allocation is claimed. Positive sizes reserve construction work and heap
credit, reload the fill value after GC, and publish the completed tuple with
repeated references rather than deep copies.

Zero returns an immutable, namespace-free Engine-owned empty tuple with native
two-word read-ahead storage, without allocation or GC. Engine retention by live
Worlds/processes supplies its lifetime. Tests retain this singleton through a
peer's collection after reclaiming the originating World. Other tests cover
heap/refcounted binary values, repeated-reference identity, moving GC, invalid
counts, maximum-plus-one badarg versus valid-maximum resource refusal,
allocator/quota/work prefixes, primitive credit/source preservation, final
balance and shared-worker lifecycle behavior. Independent OTP confirms empty,
filled and nested-value semantics and invalid-size behavior.

Fresh full gate: `/tmp/libbeam-make-tuple-integrated/summary.json`, `passed=true`,
265 commands, 100 Debug / 100 Release CTests, 31 existing plus 57 profile UBSan
tests. Provenance: `libbeam/core/otp/make-tuple-sources.json`. Native BIF count is
79 plus the separate `apply/3` instruction export.

The unchanged closure at `/tmp/libbeam-midgard-after-make-tuple/` now refuses
`erlang:tuple_to_list/1`, import 57, module 51 (`Elixir.Enum`), with
`success=false reclaimed=true engine_shutdown=true`. Application acceptance
remains open.

### Tuple-to-list checkpoint

`erlang:tuple_to_list/1` now follows the native backwards-cons construction.
It rejects non-tuples with `badarg`, returns NIL for an empty tuple without
allocation/collection, precharges and reserves two words per element plus
primitive heap credit, reloads the source after moving GC, and publishes the
completed list last. Element order and shared references are preserved; the
source tuple and its elements are not modified or deep-copied.

Compiled fixtures, independent OTP results and component tests cover empty and
mixed tuples, repeated heap/refcounted binary elements, ordering, source retention
in an explicit moving root, forced GC, badarg, allocation/quota/work prefixes,
heap credit and final balance. Shared-worker threshold/reuse/peer/reclamation
coverage is retained. Runtime limits can reject conversion of a natively valid
large tuple; no unbounded-size success or performance claim is made.

Fresh gate: `/tmp/libbeam-tuple-list-integrated/summary.json`, `passed=true`,
267 commands, 102 Debug / 102 Release CTests, 31 existing plus 59 profile UBSan
tests. Provenance: `libbeam/core/otp/tuple-list-sources.json`. Native BIF count is
80 plus the separate `apply/3` instruction export.

The unchanged closure at `/tmp/libbeam-midgard-after-tuple-list/` now refuses
`erlang:list_to_tuple/1`, import 62, module 51 (`Elixir.Enum`), with
`success=false reclaimed=true engine_shutdown=true`. Whole-application execution
and response acceptance remain open.

### List-to-tuple and forwarding-return proof checkpoint

`erlang:list_to_tuple/1` now charges a proper-list walk, rejects improper/non-list
inputs with `badarg`, and returns the Engine-owned empty tuple for NIL without
allocation. Nonempty conversion precharges/reserves output plus primitive heap
credit, reloads the input after GC, and copies references in order before
publication. Source cells remain unchanged. Work denial while discovering an
improper tail is distinct from the eventual guest `badarg`; a 65-cell malformed
prefix exercises every credit through that boundary. No maximum-length-list
success is claimed.

The compiler-produced fixture exposed a real verifier gap: it omitted `is_tuple`
after a local wrapper that tail-calls this tuple-returning native BIF. Rather
than rewrite the fixture or trust a compiler hint, the verifier now recognizes
reviewed native tuple-return contracts for direct calls and structurally exact
branchless native-tail-call forwarding bodies. Labels/line metadata may precede
the native call; branches, other operations, recursion and unknown returns do
not gain a proof. Every callee still undergoes full verification. Tuple-ness does
not prove any element index, and call clobbers remain intact. Normalized positive
and negative tests cover these boundaries.

The shared bidirectional tuple/list harness now covers each direction's ordering,
source/alias preservation, heap/refcounted binary elements, moving roots,
allocation/quota/work prefixes, primitive credit and final balance. Independent
OTP fixtures and real shared-worker threshold/reuse/peer/reclamation tests pass.

Fresh integrated evidence: `/tmp/libbeam-list-tuple-integrated-final/summary.json`,
`passed=true`, 269 commands, 104 Debug / 104 Release CTests, 31 existing plus 61
profile UBSan tests. Provenance: `libbeam/core/otp/list-tuple-sources.json`.
Native BIF count is 81 plus the separate `apply/3` instruction export.

The unchanged closure at `/tmp/libbeam-midgard-after-list-tuple/` now refuses
`erlang:floor/1`, import 69, module 51 (`Elixir.Enum`), with
`success=false reclaimed=true engine_shutdown=true`. Application execution and
response-byte acceptance remain unproved.

### Floor checkpoint and exact dependency audit

`erlang:floor/1` now implements native integer identity, nonnumeric `badarg`, and
finite binary64 floor followed by native `double_to_integer` digit construction.
The at-most-seventeen-word payload is staged in bounded independent storage,
with conservative work precharge, before GC and publication. Primitive heap
credit and source roots survive collection. Conversion runs under `FE_DFL_ENV`
and restores host rounding and exception flags before allocation. Infrastructure
work/quota/OOM/environment failures bypass guest guards; native guard/body
projection allows collection and recomputes destination addresses.

The pinned helper has a strict-boundary representation quirk: floor of float
`-2^59` returns a boxed negative integer, not the small literal. Both print the
same decimal but native exact equality is false. This is deliberately preserved,
not normalized away. Installed OTP independently confirms it in
`/tmp/libbeam-floor-oracle/0.log`; the compiled reference covers 743 boundary and
sign/exponent/mantissa vectors. Component tests use an independent integer/bit
oracle, body/guard/Y paths, maximum finite values, subnormals/signed zero, integer
identity under allocator/quota denial, all four rounding modes plus exception
flags, forced moving GC, allocation/work prefixes (including actual guards),
source/credit preservation and exact balance.

Fresh full evidence: `/tmp/libbeam-floor-integrated/summary.json`, `passed=true`,
271 commands, 106 Debug / 106 Release CTests, 31 existing plus 63 profile UBSan
tests. Provenance: `libbeam/core/otp/floor-sources.json`. Native BIF count is 82
plus the separate `apply/3` instruction export.

The unchanged 377-module closure at `/tmp/libbeam-midgard-after-floor/` now
refuses `"Dict":update/4`, import 71, module 51, with
`success=false reclaimed=true engine_shutdown=true`. An independent OTP
`beam_lib`/disassembly check in `/tmp/libbeam-enum-imports-confirmed/0.log` proves
this is an exact, non-namespaced `Dict` import in
`Elixir.Enum:'-group_by/3-fun-2-'/3`; installed OTP reports `Dict` as
`non_existing`. It is not an atom-name truncation or an alias for `Elixir.Dict`.
No substitute module, error stub or admission relaxation has been introduced.
The missing dependency must be reconciled with strict unused-import admission;
application execution and response acceptance remain unproved.

This audit also corrects earlier prose: module index 51 was **Elixir.Enum**, not
Elixir.Kernel.Typespec. `run_beam` submits dependencies first and the main module
last; a sorted `inputs.json` is not submission order. The corresponding historic
MFA/refusal logs remain unchanged. Module 43 is Elixir.Code as previously stated.

A further independent structural audit at
`/tmp/libbeam-closure-reachable-import-audit/0.log` traverses imports from all 32
unchanged artifact modules. All 377 supplied modules are statically reachable;
the exact graph has three missing bindings: Enum → `Dict:update/4`, URI →
`Dict:put/3`, and IEx.Config → `prim_tty:npwcwidthstring/1` (missing export in the
supplied module). The artifact directly imports Enum from Midgard.Protocol,
Midgard.Request and multiple Jason modules. Thus simply dropping Enum or these
transitive branches would violate whole-module admission. This structural check
does not certify libbeam BIF/opcode support and excludes `erlang` native-catalog
coverage. The audit script is retained at `/tmp/libbeam-audit-imports.escript`;
no application/dependency bytes were modified.

### Absent-import policy decision pending

Further primary-source/reference checks confirm that these are not merely
missing libbeam implementations. Official Elixir v1.19.5 sources construct the
exact `Dict` atom in deprecated Enum/URI dictionary branches. Calling Enum's
branch on installed OTP produces `error:undef` with an actual
`{'Dict',update,Args,[]}` first frame. IEx's prompt-width branch explicitly tests
`function_exported?(:prim_tty, :npwcwidthstring, 1)` and falls back to
`shell:prompt_width/1`. Evidence is retained under
`/tmp/libbeam-missing-import-reference/` and
`/tmp/libbeam-absent-import-sources/`.

Durable primary evidence, reproducible audit, exact identities and alternatives
are committed locally in the private ai-research repository at
`personal/libbeam/absent-import-admission/`, commit `cbf6e0a`; nothing was pushed.

The implemented [batch admission contract](0004-eager-batch-admission.md#contract)
still rejects every unresolved import. Permitting genuine absent-target call
sites with supported native undefined-call failure behavior in a closed namespace
would change that invariant. It would also require clear separation from
known-but-unimplemented native capabilities, proper exception/handler semantics,
and correct pre-execution binding/lease rules. It must not silently become an
error stub or claim autoload/custom-handler parity. This is a proposed policy
choice requiring user authorization, **not implemented admission behavior**.
Strict rejection remains in force; no completion or application execution is
claimed.

### Strict policy confirmed; stock-version investigation

The user confirmed strict load-time resolution of imported modules and
function/arities. The absent-target/undefined-call admission proposal above was
not adopted. No missing-import checks were relaxed and no substitute Dict module
was supplied.

A version-only investigation examined unmodified official Elixir release bundles:
1.19.5 built for OTP 26, 1.19.5 built for OTP 27, and 1.20.0 built for OTP 28.
Each archive's SHA-256 was checked against the official release API digest before
extraction into external temporary directories. Trusted OTP metadata inspection
found 447 unique modules in each bundle, no exact-name Dict definition, and both
Enum's `Dict:update/4` and URI's `Dict:put/3` imports in every candidate. The 1.20.0
bundle removes the IEx npwcwidthstring import, but not the decisive missing Dict
bindings. Older Elixir releases below 1.19 are outside the unchanged project's
declared `~> 1.19` requirement.

Evidence: `/tmp/libbeam-dependency-candidates-audited/0.log`; packages and official
release metadata under `/tmp/libbeam-dependency-candidates/`. Durable scripts,
outputs and package identities are recorded in the private ai-research topic
`personal/libbeam/absent-import-admission/`, local commit `ec754a0`, not pushed.
This rules out these tested stock candidates, not every conceivable version.

The original application artifact and closure remain untouched. There is still
no demonstrated compatible stock closure. A dependency-source compatibility
strategy would need explicit approval beyond the current no-stripping/no-rewrite
constraints and fresh behavior evidence; none has been implemented. The paused
proposal was not permission to weaken strict admission.

The user subsequently reaffirmed strict resolution and took ownership of the
missing `Dict.beam` investigation. No dependency patches, synthetic provider or
undefined-call admission are authorized. Further stock-version searches for this
blocker are on hold pending that investigation. Resumption requires a concrete
provider or dependency plan to inspect; any candidate must define the exact
module atom `Dict` (not `Elixir.Dict`) and the required `update/4` and `put/3`
exports. It must still pass normal whole-group capability, ownership and resource
validation. Resolving Dict alone does not resolve the independently identified
`prim_tty:npwcwidthstring/1` import or establish application acceptance.

### User-supplied toolchain fix: dependency blocker resolved

The user supplied Elixir PR <https://github.com/elixir-lang/elixir/pull/15987>
and installed the corrected toolchain, then authorized continuation. Inspection
identifies Elixir `1.21.0-dev (59531b7)`, built with installed OTP 28/ERTS 16.3.
Enum and URI now import the exact module **Elixir.Dict**, whose real `update/4`
and `put/3` exports are present. The prior unqualified `Dict` references were a
namespace bug, not evidence that the library intentionally required undefined
calls. Earlier discussion of deliberately absent targets overstated intent.
No admission relaxation, synthetic provider or agent-authored dependency patch
was needed.

New closure discovery at `/tmp/libbeam-updated-elixir-closure/` and independent
OTP audit at `/tmp/libbeam-updated-elixir-closure-lock/` report **379 modules,
zero missing module/export bindings** (excluding native `erlang` capability
coverage). IEx no longer imports the missing prim_tty function. The original
32-module artifact and wire request/expected-response bytes remain unchanged;
the retry rechecks every artifact module hash and size. Old closure files and
receipts are retained. The retry script accepts an explicit closure-list path
and records `submission.json` with actual loader indices, paths and hashes.

Fresh existing-profile validation passed at
`/tmp/libbeam-updated-toolchain-integrated/summary.json`: 271 commands, 106 Debug
and 106 Release tests, 31 existing plus 63 profile UBSan tests. Real public-Isolate
admission then progressed to `erlang:node/0`, import 7, module 62
(Elixir.File.Stream), with clean reclamation. This is progress, not application
execution or full compatibility with the new toolchain.

### Unnamed local node checkpoint

`node/0` now reads the owned namespace's native-initialized node name
(`am_Noname`, `nonode@nohost`). The zero-argument BIF charges one work unit without
allocation, interning, collection or leases. Complete native `node_x`/`node_y`
cases replace only singleton state access and retain ordinary instruction work
charging. Node naming, distribution, remote identities, `node/1` and `nodes/0`
are not admitted by this slice. This is not a claim of unique node names per
Isolate.

Tests cover real compiled X/Y forms and dynamic apply, all compiled work
prefixes, primitive zero/exact/extra credit, full-heap allocator/quota denial,
source/credit/namespace preservation, post-return GC, live-peer survival and
replacement, independent OTP output and shared-worker lifecycle behavior.
Provenance: `libbeam/core/otp/node-sources.json`; native BIF count is 83 plus the
separate apply/3 instruction export.

Catalog growth also exposed a cancellation test exhausting its 4,096-unit budget
before cancellation: `/tmp/libbeam-node-work-observed/0.log` records escaped=1,
WORK_LIMIT and remaining=0. Only the registration/cancellation witness now uses
its own finite 1,000,000-unit World; existing 4,096-unit exhaustion worlds,
exact-threshold assertions, the causal-transfer witness and production defaults
remain unchanged. The cancellation still must close the escaped World and
physically reclaim it. Focused evidence: `/tmp/libbeam-node-cancellation-witness/`.

Fresh integrated validation at `/tmp/libbeam-node-integrated-final/summary.json`
passes 273 commands, **108 Debug / 108 Release**, and **31 + 65 UBSan** tests.
The unchanged artifact with the new closure at `/tmp/libbeam-midgard-after-node/`
now reaches `erlang:nodes/0`, import 8, module 65 (Elixir.GenServer), with
`success=false reclaimed=true engine_shutdown=true`. This is an ordinary internal
capability gap; the external Dict dependency blocker is resolved. Application
response-byte acceptance remains open.
