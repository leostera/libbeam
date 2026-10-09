<!--
%CopyrightBegin%

SPDX-License-Identifier: Apache-2.0

Copyright 2026 Leandro Ostera <leandro@ostera.io>

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

%CopyrightEnd%
-->

# Next five: W1-05, W1-06, W1-08, W1-09, W1-10

This batch advances the five earliest unfinished tasks. It does not claim five
completed tasks, runtime isolation, or permission to deploy hostile code.
The native runtime is unchanged; this work adds regression evidence, measurement
analysis, native discovery, explicit design-review records and CI result checking.

## W1-05 — Broader regression discovery

New serialized optimized-JIT runs finish these complete suites without skips:

| Suite | Passed | Failed | Skipped |
| --- | ---: | ---: | ---: |
| `ets_SUITE` | 162 | 0 | 0 |
| `monitor_SUITE` | 25 | 0 | 0 |
| `timer_bif_SUITE` | 23 | 0 | 0 |
| `register_SUITE` | 1 | 0 | 0 |

Evidence: `/tmp/beam-realms-next5/{opt-ets,opt-monitor-timer-register}/summary.json`.
The runner deliberately reports `needs_review` for broad profiles; the table above
is the reviewed count evidence, not a reinterpretation of that exit status.
The earlier full debug ETS run still timed out and is not called a debug pass.

Together with the prior broad process, signal, distribution, code-loading,
trace, NIF/dirty-NIF and port runs, this completes **discovery and classification**
for W1-05. It does not complete regression clearance or W1-G. Outstanding results
remain release blockers rather than waived baseline failures:

- Signal `parallel_signal_enqueue_race_2`: 120-second timetrap on this branch and
  unchanged upstream runtime; root cause unresolved.
- Distribution `async_dist_proc_dctrlr`: 240-second timetrap on both runtimes;
  root cause unresolved. Four large-message/exit variants skipped for insufficient
  memory remain uncovered.
- Optimized NIF `consume_timeslice`: traced process exit with `{badmatch,1}` at
  `consume_timeslice_test:3469` on both runtimes. Debug excludes this case;
  `select_error` requires Linux. Neither is cleared by the other 85 debug passes.
- Trace `suspend_system_limit`: two intentionally disabled long-running stress
  cases remain unexecuted. Optimized process-table cases now pass separately.
- The missing NIF helper, released emulator-flavor mismatch and live-script-edit
  failures were test-harness failures, repaired/retried—not production bypasses
  to be suppressed. The final optimized ETS run now covers all 162 cases.

The previously skipped `spawn_against_old_node` also passes separately on optimized
and debug JIT against installed OTP 28.4.1. CT's release discovery inherited
source `-emu_flavor jit` options; installed OTP 28 only has `smp`, so its version
probe exited 1 before discovery. The checked-in
`scripts/realm-old-release-wrapper.sh` removes implicit Erlang flags/code paths for
that installed executable only; the source runtime still uses the requested
JIT/debug variant. No handshake checks or production code changed.

Reproduce after stopping other OTP validation jobs (use a fresh wrapper directory):

```sh
mkdir /tmp/realm-old-bin
ln -s "$PWD/scripts/realm-old-release-wrapper.sh" /tmp/realm-old-bin/erl
ln -s "$PWD/scripts/realm-old-release-wrapper.sh" /tmp/realm-old-bin/erlc
REALM_OLD_RELEASE_BIN=/absolute/installed/otp28/bin \
PATH="/tmp/realm-old-bin:$PATH" \
python3 scripts/realm-validation.py --output /tmp/realm-old-results \
  --variants opt-jit debug-jit --profile broad --broad-suites process_SUITE \
  --cases spawn_against_old_node --skip-build
```

Evidence: `/tmp/beam-realms-next5/otp28-interop-adapter/summary.json`; the exact
installed bin used was `/opt/homebrew/Cellar/erlang/28.4.1/lib/erlang/bin`.
This validates the host compatibility case, not distributed Realms or language
compatibility. Direct installed-runtime discovery under source flags remains an
upstream test-harness issue; the adapter is explicitly a local test fixture.

Detailed historical counts, paths and skip reasons remain in
[the validation record](0001-validation.md). No test timetrap or production guard
was relaxed for this batch.

## W1-06 — Strict CI matrix aggregation, still unexecuted

The manual workflow now has a final job that downloads all cell summaries and
runs `scripts/realm-ci-report.py`. It requires all eight Linux ARM64/x86-64 ×
optimized/debug JIT/interpreter cells, each with 139 Realm/focused cases and five
public API cases. Missing artifacts, duplicate/missing steps, failed/skipped or
reduced counts, wrong architecture/variant/revision, a different validation runner,
and source changes beyond generated preloaded BEAMs fail the aggregate.

Both profiles must finish; an API-only success cannot hide a failed core run.
Only summary artifacts are uploaded, not raw CT logs containing node cookies.
`runtime_matrix_passed` means the declared functional matrix passed, **not** W1-G,
performance clearance, completeness of the platform matrix or security approval.
The checker assumes trusted CI artifacts; it is not cryptographic attestation.

Local tests exercise complete synthetic matrices and missing/malformed cases.
Running against absent hosted artifacts correctly reports all eight cells as
`incomplete_or_failed`. This is a checker test, not hosted execution evidence.
Nothing was pushed and no hosted workflow was dispatched. W1-06 remains open.

## W1-08 — Matched measurements in both source orders

The existing harness ran 5000 samples × ten fresh-VM repetitions per mode, twice:
upstream host → current host/restricted, then current host/restricted → upstream
host. Within current runs the mode order alternates. This controls source-order
direction across batches but is not randomized per-sample pairing or an isolated
hardware performance laboratory. Both use optimized JIT and two normal schedulers,
one dirty CPU scheduler and one dirty I/O scheduler on the same ARM64 macOS host.

`scripts/realm-benchmark-compare.py` validates raw repetition/workload counts,
configuration, fixture/driver digests, runtime architecture and recomputed
aggregates before comparing. Matching recorded metadata does not attest identical
CPU hardware, thermal/power state or background load; operators must control those
separately. Unsupported workloads remain visible. Ratios of
observed extrema are **not confidence intervals**. Successful output is
`compared_not_accepted`, with `thresholds: unagreed`.

| Observation | Upstream → current | Current → upstream |
| --- | ---: | ---: |
| Host spawn p50 ratio | 1.001 | 1.000 |
| Restricted spawn p50 ratio | 1.039 | 1.039 |
| Host spawn p99 ratio | 1.784 | 1.000 |
| Restricted spawn p99 ratio | 2.033 | 2.100 |
| Host 64-KiB round-trip p50 ratio | 1.099 | 1.099 |
| Restricted 64-KiB round-trip p50 ratio | 1.092 | 1.092 |

These results retain rather than hide tails and potential overhead. In the first
order, host spawn p99 varied from 1209 to 15000 ns across repetitions. Restricted
spawn p99 remained around twice the upstream median in both orders; that needs
investigation rather than a made-up passing threshold. The 100-worker process
memory sums remain 269600 upstream versus 272800 on this branch in both modes,
not a complete native/RSS/Realm memory model.

Durable dispersion and ratios: [0001-benchmark-comparison.tsv](0001-benchmark-comparison.tsv).
Raw reports: `/tmp/beam-realms-next5/bench-{upstream,current}{,-reverse}/summary.json`;
comparisons: `/tmp/beam-realms-next5/benchmark-comparison{,-reverse}.json`.
The prior [methodology and limitations](0001-benchmarks.md) still apply.
W1-08 remains open pending diagnosis, broader measurements and maintainer agreement
on thresholds; no numeric budget is silently approved.

## W1-09 — Additional native surfaces

`scripts/realm-native-surfaces.py` discovers tracked production sources under
`erts/emulator/nifs` and `lib/*/c_src`, plus `erl_driver.h`:

- 281 NIF export initializer rows, including conditional alternatives.
- 68 NIF lifecycle callback slots (including NULL/disabled slots).
- 113 driver API declarations and one driver global variable.
- 18 driver callback declarations, including both platform `start` signatures
  and the reserved unused callback slot.
- One unresolved table: `crypto_callback.c:dummy_funcv[1]`, an uninitialized dummy
  table used for Windows NIF API access, not an approved native export.

The [482-row source registry](0001-native-surfaces.tsv) records symbols, owners,
flags/signatures, source locations and whole-file hashes. This is an unpreprocessed
union, not 482 executable operations on every platform. Unsupported initializer
or callback grammar fails rather than silently disappearing. Literal/comment
spellings do not count as registrations. File hashes detect body changes even
when declarations and locations stay fixed; helpers in other files are not
implicitly covered by the hash.

These rows supplement—not replace—the 570 BIF and 180 NIF API declarations.
Generated/external native libraries, resource callbacks, preloaded/internal
wrappers, optimized instruction paths, services and deferred producers still
need exhaustive enumeration. W1-09 remains open.

## W1-10 — Initial design records and concrete negative witnesses

[0001-operation-reviews.tsv](0001-operation-reviews.tsv) contains ten explicit
source/design review records covering 25 BIF and seven NIF API entries. They state
context, effects, targets, planned disposition, rejection/absence behavior,
ownership/charging, present implementation gaps, evidence, remaining tests and
accountable workstreams. All records say `DESIGN_REVIEWED_ENFORCEMENT_OPEN`.
Another 718 entries in the two declaration inventories lack these design records;
new native registrations/callbacks require their own reviews as well.

`scripts/realm-review.py --check` verifies references, required fields, unique
coverage and hashes of declared source dependencies. A helper/body change in a
listed file invalidates its review even if the BIF declaration did not change.
`--refresh-fingerprints` is explicit and must follow re-review; it never changes
a disposition into enforcement approval. Dependencies are manually identified,
not an automatically proven transitive call graph. Independent review remains open.

A notable reviewed alternate path: the `load_nif_2` BIF rejects dynamic `apply`,
but `erts_load_nif` is reached directly through `emu/bif_instrs.tab` and
`jit/beam_jit_common.cpp`. Denying the listed BIF alone would not deny native loading;
loading permission would still not mediate already-loaded native effects.

`scripts/realm_effects_probe.erl` is a **negative isolation witness**, run only in a
disposable VM. Trusted bootstrap deliberately supplies a foreign atomics handle
and a persistent-term key. It confirms on optimized JIT, debug JIT and debug
interpreter that a restricted root can read/write that atomics array and the
global persistent-term entry. An ordinary host send is rejected, but a host-created
timer still delivers into that root. Results are reported as
`shared_effects_bypasses_observed`, `security_acceptance => not_met`.
The witness does not perform file/network operations or load native libraries;
only in-VM shared state and deferred message delivery are exercised.

Evidence: `/tmp/beam-realms-next5/effects-probe/summary.json` and per-variant logs.
The fixture asserts non-host identity, cancels its timer, stops the Realm and erases
its unique persistent key. It is not a passing boundary test; after mediation is
implemented its expected observations must be replaced by positive/negative policy
tests. Runtime fixes belong to W3-11–13 and W4-04/05/07–10, with accounting in W6.

Validation tooling: 51 Python tests pass, including scanner grammar/body drift,
review invalidation, complete/missing CI matrices, comparison rejection and the
installed-release adapter. All four inventory/review checks pass, as do shell/YAML
syntax, RFD links/task counts and diff checks. License scan: 24 changed/new files,
zero file warnings. These checks remain distinct from runtime acceptance.

W1-10 remains open: declaring desired dispositions is not implementation,
complete per-entry review, or hostile-code approval. Untrusted deployment remains
**no-go**.
