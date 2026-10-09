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

# W1 measurement harness and initial observations

W1-07 now has an executable, repeated measurement harness. W1-08 remains open:
these observations do not establish acceptable overhead, agreed latency limits,
fairness, complete memory cost, density improvement, or production readiness.
**No pass/fail performance threshold has been invented.**

## Reproduction

Use configured/built checkouts and the source runner from this branch. Do not run
other native test/build/benchmark jobs concurrently; the runner takes the same
user-wide lock as validation. Every sample repetition starts a fresh unnamed VM.

```sh
python3 scripts/realm-benchmark.py --output /tmp/realm-bench-current \
  --modes host restricted --variant opt-jit --iterations 1000 --repetitions 5
python3 scripts/realm-benchmark.py --root /tmp/otp-upstream-clean \
  --output /tmp/realm-bench-upstream --modes host --variant opt-jit \
  --iterations 1000 --repetitions 5
```

The program compiles the same `scripts/realm_bench.erl` with each checkout's
compiler. Baseline host measurements never require Realm APIs. Restricted
measurements execute inside a restricted Realm and return data only through a
bound byte endpoint. Host-only registration is measured; currently denied
restricted registration is explicitly unsupported, not timed as a successful
local operation. Private ETS measurements do **not** claim ETS isolation.

Each timed workload warms up for 20 operations, then measures individual elapsed
nanoseconds plus total loop time. Quantiles use nearest-rank p50/p99 of the
per-operation samples. The driver retains every repetition and aggregates median,
minimum, maximum and population standard deviation across repetition quantiles.
These are distributions of per-repetition quantiles, not a pooled global p99 or
confidence interval. The default is five repetitions; the first run below used
three. Host/restricted ordering alternates between repetitions.

Workloads cover clock-pair overhead, spawn/monitor/exit, small round trips,
64-KiB binary round trips, alias and priority-alias replies, register/lookup/remove,
private ETS insert/lookup/delete, 100 idle-process memory observations and Realm
create/root-ready/stop cycles. Timed operations include their documented setup
inside the loop (for example monitor creation and DOWN receipt for spawn/exit).
The large binary uses ordinary same-VM binary sharing; it is not a copy-bandwidth
benchmark or endpoint payload benchmark.

Realm activation includes prepared endpoint setup, root execution, pull-based
readiness polling and stop, with at most 100 measured cycles per repetition. The
polling interval and stop yielding dominate the observed roughly 2-ms result;
it is not intrinsic native spawn or loader latency. This is not deployment
activation with private code loading, application startup or edge routing.

Memory reports include `process_info(Pid, memory)` sums for ready idle processes
and host-observed whole-VM `erlang:memory(total)` before/after the workload batch.
They exclude a full accounting of shared binaries/code, native resources,
allocator fragmentation, RSS/peak residency and host headroom. VM deltas include
module loading and allocator noise; they are not per-Realm memory budgets.

`summary.json` records runtime/build/flavor/architecture, platform, scheduler
count, revision, working-diff digest, driver/fixture digests, commands, raw results
and aggregate dispersion. Implicit Erlang flags/code paths/compiler options are
removed. Runs request two normal schedulers and one dirty CPU/I/O scheduler;
normal scheduler count and runtime variant are verified. A missing, duplicate,
reduced or mismatched result fails. A successful measurement is named
`measured_not_accepted`, never a release acceptance status.

## Initial matched comparison

Source runtime: snapshot native implementation plus the version-0 public wrapper;
working HEAD `cbcd4242e3` before this batch's commit. Baseline native runtime:
`cca4e72510a97cfca6427602d3da8a22d5ff7a33`. Both optimized JIT, same compiler,
account and machine; two normal schedulers, 1000 samples, three fresh-VM repetitions.
Reported platform: `macOS-26.6.2-arm64-arm-64bit-Mach-O`.
Raw evidence: `/tmp/beam-realms-w1-bench/{current,upstream}/summary.json`.
Durable aggregate data: [0001-benchmark-results.tsv](0001-benchmark-results.tsv).

Median of each run's p50, in nanoseconds:

| Workload | Upstream host | Realm build host | Restricted Realm |
| --- | ---: | ---: | ---: |
| Clock pair | 42 | 42 | 42 |
| Spawn/monitor/exit | 833 | 875 | 916 |
| Small message round trip | 500 | 500 | 542 |
| 64-KiB binary round trip | 2292 | 2542 | 2459 |
| Alias round trip | 583 | 583 | 584 |
| Priority-alias round trip | 584 | 584 | 625 |
| Private ETS cycle | 125 | 125 | 125 |
| Registration cycle | 125 | 125 | Unsupported |
| Realm create/ready/stop | Unavailable | 1999042 | Host-managed operation |

Tail variability is material. Restricted priority-message p99 ranged from 1000
through 50500 ns across repetitions (median 4125); restricted alias p99 ranged
from 875 through 44458 ns. Even host spawn/exit p99 ranged from 1000 through
2833 ns. Do not turn the p50 ratios into accepted thresholds or hide the outliers.

The sum of process-reported memory for 100 ready idle processes was 269600 bytes
upstream and 272800 bytes in both host and restricted modes on the Realm build:
32 additional reported bytes per process in this fixture. This is not a complete
per-process or per-tenant VM-memory model. Host VM before/after snapshots ranged
roughly 44.7–45.3 MB and are preserved in raw results; no RSS or density claim is
made from those numbers.

## Follow-up matched comparisons

The [next-five evidence](0001-next-five-evidence.md) records 5000 samples × ten
repetitions per mode in both source orders, with
[durable dispersion and ratios](0001-benchmark-comparison.tsv). The new comparer
recomputes raw aggregates and rejects mismatched fixture/driver/configuration,
missing repetitions, unsupported/missing workload surprises and malformed metrics.
It reports `compared_not_accepted`, never a budget approval:

```sh
python3 scripts/realm-benchmark-compare.py \
  --baseline /tmp/realm-bench-upstream/summary.json \
  --candidate /tmp/realm-bench-current/summary.json \
  --output /tmp/realm-bench-comparison.json
```

Restricted spawn p50 is about 1.039× upstream in both orders, but p99 is about
2.03–2.10×. Host spawn p99 ranges from a 1.00× to 1.78× median ratio by order;
64-KiB round-trip p50 is around 1.09–1.10× in both orders. These are observations
requiring diagnosis and threshold agreement, not a claim of acceptable overhead.

## Owned-resource follow-up

The expanded fixture now includes atomics `add_get` and add/read operations for
both counter backends. [Enforcement-batch measurements](0001-shared-resource-enforcement.md)
and [their aggregates](0001-resource-benchmark-results.tsv) use 5000 samples × ten
repetitions with identical baseline/current fixtures. Creation is outside timed
loops. Native-resource p50s are at the roughly 42-ns clock-resolution floor; equal
observations cannot establish zero cost. Batched operations and allocation/final
release measurements remain necessary. Old reports retain their old fixture
semantics and are not interchangeable with the new strict workload inventory.

## Remaining W1-08 work

Repeat on quieter hosts, more repetitions, both architectures and relevant
scheduler counts; alternate source-build order across independent runs. Add
sustained contention/noisy-neighbor, GC/allocator/dirty-work and large retained
resource measurements as those features arrive. Establish full memory categories
and confidence/noise bounds before proposing p50/p99 and overhead budgets.
The maintainers must explicitly agree the thresholds; this run does not supply
that agreement. Native timing-test failures and broad regression gaps remain
separate blockers even when a microbenchmark completes.
