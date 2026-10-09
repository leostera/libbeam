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

# Reproducing Realm validation

This guide supports W1-01–04 in the [work checklist](0001-work-plan.md).
The runner is [scripts/realm-validation.py](../../beam/scripts/realm-validation.py).
It uses Python 3.9+ standard-library facilities and POSIX process groups/locking;
Windows support is not claimed. GNU Make and the normal OTP build/test prerequisites
are required. See [OTP development](../../beam/HOWTO/DEVELOPMENT.md) and
[OTP testing](../../beam/HOWTO/TESTING.md).

## Fresh build

Use a separate checkout/worktree, not a destructive reset of active work. For
example, run the following from the existing repository, choosing a new path:

```sh
git worktree add --detach /tmp/otp-realms-clean 18708c2d4d
cd /tmp/otp-realms-clean
export ERL_TOP="$PWD"
export PATH="$PWD/bin:$PATH"
unset ERL_FLAGS ERL_AFLAGS ERL_ZFLAGS ERL_LIBS

# This SSL path is specific to the ARM64 macOS development host.
./otp_build configure --with-ssl=/opt/homebrew/opt/openssl@3
make -j4

# The source snapshot excludes locally regenerated preloaded BEAM artifacts.
# Bootstrap first, then regenerate them with this checkout's compiler.
./otp_build update_preloaded --no-commit
make -j4
make -j4 TYPE=debug FLAVOR=jit
make -j4 TYPE=debug FLAVOR=emu
```

Use the appropriate SSL/toolchain paths on other machines. Record configure output
and unavailable optional applications (wx/ODBC on the current host). Regeneration
can change several tracked preloaded binaries; these are build artifacts, not
additional source changes to include in a PR. Do not use the installed OTP's
compiler/runtime as a substitute for this checkout's tools.

The original snapshot predates the runner; invoke the runner from the working
checkout with `--root /tmp/otp-realms-clean`. Build/test commands run with that
root as their working directory. Detached worktrees retain their pinned source
revision and make upstream/snapshot comparison possible without modifying active
implementation work. Budget disk space before adding build trees; retain or
remove them deliberately after gathering evidence.

## Runner commands

From a configured checkout with a bootstrapped `bin/erl`:

```sh
# Default: build, verify runtime identity, and run both Realm suites plus the
# focused regression selections on all three variants.
python3 scripts/realm-validation.py --output /tmp/realms-full --jobs 4

# Focused debug iteration: 64 Realm cases plus 84 selected OTP regressions.
python3 scripts/realm-validation.py --output /tmp/realms-debug \
    --variants debug-jit --jobs 4

# Realm cases only on the other two variants.
python3 scripts/realm-validation.py --output /tmp/realms-other \
    --variants opt-jit debug-emu --profile realms --skip-build

# The historical C-node reproducer, separate from the passing focused selection.
python3 scripts/realm-validation.py --output /tmp/realms-c-node \
    --variants debug-jit --profile c-node --skip-build

# Run the same C-node selection against a configured/built upstream worktree.
python3 scripts/realm-validation.py --root /tmp/otp-upstream-clean \
    --output /tmp/upstream-c-node --variants debug-jit --profile c-node --skip-build

# Broad discovery: all cases in selected suites, never silently accept skips.
python3 scripts/realm-validation.py --output /tmp/realms-broad \
    --variants debug-jit --profile broad --broad-suites process_SUITE code_SUITE \
    --skip-build

# Isolate a failure on upstream with the same runtime variant.
python3 scripts/realm-validation.py --root /tmp/otp-upstream-clean \
    --output /tmp/upstream-signal-race --variants debug-jit --profile broad \
    --broad-suites signal_SUITE --cases parallel_signal_enqueue_race_2 --skip-build

# Inspect exact commands without running or creating output.
python3 scripts/realm-validation.py --output /tmp/unused --dry-run

# Test result validation, command failures, timeout descendant cleanup, and plans.
python3 -B -m unittest discover -s scripts -p 'test_realm_*.py' -v
```

`--profile public-api` runs the five `realm_api_SUITE` wrapper-contract cases.
`--profile resources` runs nine Realm resource cases plus complete atomics (7),
counters (6) and persistent-term (22) suites: exactly 44 cases, no skipped passes.
`realms` now selects 64 cases; `all` selects those plus 84 focused regressions.
`--variants opt-emu` explicitly adds optimized-interpreter coverage; the default
remains the historical three variants. The manual runtime CI workflow declares
all four variants on Linux x86-64 and ARM64, but no hosted run is claimed.

`--profile focused` selects only the 84 focused cases for each selected variant.
`all` does **not** mean all OTP tests: it means both Realm suites and the focused
selections encoded in the runner. The C-node case remains a separate profile for comparison with unpatched upstream
and snapshot fixtures; failures must not disappear from results or become accepted
skips. Update expected suite counts when deliberately adding cases;
a count mismatch must not be hidden by accepting arbitrary nonzero totals.

`--profile broad` runs full selected suites (including the stdlib target for ETS).
`--cases` explicitly narrows exactly one broad suite for diagnosis and is recorded
as such. Counts are discovered, not pre-approved: even a zero-failure run finishes
`needs_review` with a nonzero exit until its totals and skips are reviewed in the
evidence ledger. Completed failing suites retain their observed counts and the
runner continues to other broad suites; build/runtime failures, timeouts, and
interruptions still stop execution. Broad results are not silently promoted into
the strict known-count profiles. The CI inventory workflow tests the tooling, not
this full OTP matrix.

Output directories must be new. `--skip-build` omits explicit build steps; OTP's
`emulator_test` target can still build dependencies/test fixtures. Do not run other
build/test jobs concurrently or edit test/build inputs during a run. Shell scripts
can read changed contents partway through execution, invalidating a run even when
the final file passes a syntax check. OTP's test-data compiler uses the fixed node name
`test`, even across different worktrees. The runner therefore serializes its
instances using a user-wide lock in the user's temporary directory. It cannot
coordinate arbitrary external `make` jobs, other users, or different temporary
namespaces; those still require operator/CI coordination.

## Failure handling and evidence

- Each step records its argument-vector command, timeout, log path, duration,
  return code, status, expected case counts, and parsed successful CT results.
- Nonzero command exits, missing/duplicate/unexpected CT summaries, failures,
  skips, and reduced case counts fail strict profiles. Later steps remain `not_run`.
  Broad discovery preserves failures/skips for classification as described above.
- `--build-timeout` and `--test-timeout` default to 1800 seconds per step. On timeout
  or handled interruption, the runner terminates its child process group, then
  kills remaining members. It does not kill unrelated or intentionally detached
  services such as a pre-existing EPMD. OS-level SIGKILL cannot be handled; an
  incomplete `running` report is never evidence of success.
- `summary.json` is replaced atomically after each transition, including failure.
  It records revision, worktree status, tracked diff digest, runner digest, host
  platform, and timestamps. Untracked files are listed, not fully content-hashed;
  a dirty-tree report must accompany a source snapshot to be reproducible.
- A runtime probe verifies actual build type/flavor and records OTP/ERTS version
  and architecture before tests. `ERL_TOP` and `PATH` select the requested tree;
  implicit Erlang flag/library-path overrides are removed and their names recorded.
- The runner uses distinct CT node names. OTP HTML/detail logs remain under the
  selected checkout's `erts/emulator/make_test_dir/ct_logs`; command logs include
  their locations. Archive these as controlled CI artifacts when needed.
- Raw OTP logs may contain test cookies and host paths. Do not commit them blindly.
  Keep sanitized durable result summaries and reproducing commands in this record;
  transient `/tmp` logs are supplementary, not the only evidence.

## Current W1 evidence

Development platform: ARM64 macOS (Darwin 25.6.0), Apple Clang 17.0.0
(`clang-1700.6.3.2`), GNU Make 4.4.1, OpenSSL 3.6.1, Python 3.14.3.
The baseline source is `cca4e72510a97cfca6427602d3da8a22d5ff7a33`; the Realm source
snapshot is `18708c2d4d`. The following fresh-worktree evidence is separate from
incremental current-branch results; raw logs remain supplementary.

### Runner and current-branch checks

- Tooling unit tests: 51 pass across eight tooling suites, including
  rejection of failed/skipped/empty/duplicate results, zero-exit commands without
  evidence, broad failure retention/continuation, cross-worktree lock contention,
  and killing a TERM-resistant descendant after its leader exits.
- Debug JIT runner: build/runtime probe, 55 Realm cases, and 84 focused regression
  cases pass. Local supplementary logs: `/tmp/beam-realms-w1/debug`.
- Optimized JIT and debug interpreter: 55 Realm cases pass on each through the
  runner. Supplementary logs: `/tmp/beam-realms-w1/other-realms`.
- Fresh upstream and snapshot: configure, initial optimized build, preloaded
  regeneration, optimized rebuild, and debug JIT build all pass using the fresh
  build sequence above (four make jobs, same SSL configuration). Optional wx/ODBC
  remain unavailable. Supplementary logs: `/tmp/beam-realms-w1/build-{upstream,snapshot}`.
- Fresh snapshot: 55 cases pass on optimized JIT and 55 on debug JIT. The first
  debug attempt failed **before tests ran**, because a concurrent upstream test-data
  compiler already held `test@MacBookPro`. The runner's new user-wide lock prevents
  this runner-versus-runner collision; the serialized debug retry passes.
  Logs: `/tmp/beam-realms-w1/fresh-realms` and `fresh-debug-retry`.
- Fresh debug-interpreter builds and x86-64/CI coverage were not performed in this
  batch. Existing incremental interpreter evidence is not presented as fresh-build
  or cross-platform evidence.

### C-node diagnosis and fixture repair

The original test assumed that the C node would use the BEAM node's hostname
spelling. On this host the values differ:

```text
OS gethostname:      MacBookPro.localdomain
BEAM short hostname: MacBookPro
EI hostname:        macbookpro
```

`ei_connect_init()` resolves/canonicalizes the hostname. The C helper's challenge
therefore advertises `Name@macbookpro`, while `start_ei_node/1` requested
`Name@MacBookPro`. `dist_util:recv_challenge_new/2` requires exact node-name equality
and closes the connection before sending the challenge reply. EI consequently
reports a socket-read failure from `recv_challenge_reply`; that symptom alone
was not evidence of a Realm permission or cookie failure.

A standalone C probe linked against this checkout's `libei.a` confirmed the EI
name, alongside `inet:gethostname/0` in BEAM. The original case was reproduced
with the runner's `c-node` profile before changing the fixture.

The repair is test-only: `process_SUITE_data/fwd_node.c` announces its actual
`ei_thisnodename()` in a newline-delimited readiness message. `process_SUITE`
uses that exact name, enables line framing to handle split port output, and
bounds readiness waiting with linked helper/port cleanup on timeout. No production
handshake check or Realm enforcement is weakened.

The matched comparison uses separate fresh worktrees, the same toolchain/SSL
configuration and OS account/cookie environment, debug JIT, the same runner
selection, and the same network/hostname configuration. Only the fixture repair
is applied for the final upstream rerun; its production runtime is unchanged.

| Source / fixture | Result |
| --- | --- |
| Fresh upstream `cca4e72510`, original fixture | 0 passed, 1 failed: `ei_accept; 5` |
| Fresh snapshot `18708c2d4d`, original fixture | 0 passed, 1 failed: `ei_accept; 5` |
| Current branch, original fixture before repair | Same failure |
| Current branch, repaired fixture | 1 passed on each of debug JIT, optimized JIT, debug interpreter |
| Fresh upstream runtime, only the two-file fixture repair applied | 1 passed on debug JIT |

Thus the historical failure reproduces without Realms and is resolved by the
fixture repair, not by relaxing production handshake or authority checks. The
subsequent full process run reports 101 passed, zero failed, and three explicitly
classified skips (below), not an unqualified 104-case pass. W1's broader regression,
performance, inventory, and CI gates remain
open. Logs: `/tmp/beam-realms-w1/{upstream-c-node,snapshot-c-node,c-node-before,c-node-after,upstream-c-node-fixed}`.

### Minimal initial-handshake reproducer

[scripts/realm_ei_probe.erl](../../beam/scripts/realm_ei_probe.erl) exercises only initial
connection and echo, without Common Test or remote spawn behavior. First build
the repaired C helper through the runner's `c-node` profile. Then:

```sh
mkdir -p /tmp/realm-ei-probe
bin/erlc -o /tmp/realm-ei-probe scripts/realm_ei_probe.erl
bin/erl -emu_type debug -sname realm_ei_minimal \
  -setcookie realm_ei_test_cookie -noshell -pa /tmp/realm-ei-probe -eval '
    Exe="erts/emulator/make_test_dir/emulator_test/process_SUITE_data/fwd_node",
    Guessed=realm_ei_probe:run(Exe,guessed),
    Actual=realm_ei_probe:run(Exe,actual),
    io:format("guessed=~p actual=~p~n",[Guessed,Actual]),
    case Actual of ok -> halt(0); _ -> halt(1) end.'
```

On the affected host, `guessed` returns `{error,{c_node_exit,1}}` with `ei_accept; 5`,
while `actual` returns `ok`. On a machine whose hostname spelling already matches,
both can succeed; do not assert that every platform must reproduce the mismatch.
The cookie in this command is an explicit disposable test value. The helper's
reported name is trusted fixture data, not a new untrusted endpoint protocol.

## W1-05 broad regression discovery (partial)

Completed debug-JIT full-suite runs, after the EI fixture repair:

| Suite | Passed | Failed | Skipped | Classification |
| --- | ---: | ---: | ---: | --- |
| `process_SUITE` | 101 | 0 | 3 | Repaired EI case passes in the full run; skips listed below |
| `code_SUITE` | 29 | 0 | 0 | All reported cases pass |
| `multi_load_SUITE` | 3 | 0 | 0 | All reported cases pass |
| `code_parallel_load_SUITE` | 2 | 0 | 0 | All reported cases pass |
| `signal_SUITE` | 38 | 1 | 0 | `parallel_signal_enqueue_race_2` exceeds its 120-second timetrap |
| `trace_SUITE` | 76 | 0 | 2 | One explicitly disabled stress case in each trace group |
| `distribution_SUITE` | 51 | 1 | 4 | `async_dist_proc_dctrlr` exceeds its 240-second timetrap; reproduced on upstream |
| `dirty_nif_SUITE` | 35 | 0 | 0 | All reported cases pass |
| `nif_SUITE`, first attempt | 0 | 87 | 0 | Cleanup cannot call uncompiled `driver_SUITE:check_io_debug/0`; not 87 established NIF implementation defects |
| `nif_SUITE`, helper compiled | 85 | 0 | 2 | All non-skipped cases pass; skips classified below |
| `port_bif_SUITE` | 9 | 0 | 0 | All reported cases pass |

Every skip in these runs has been inspected in Common Test `suite.log`:

- `process_SUITE:spawn_against_old_node`: `"No OTP 28 available"` to the
  Common Test release lookup in the original run. Follow-up discovery isolated
  inherited source emulator flags; the explicit installed-release adapter now
  gives one pass on each of optimized/debug JIT against OTP 28.4.1.
- `process_SUITE:processes_default_tab` and `processes_this_tab`:
  `"Don't run in debug/valgrind"`. These require optimized-runtime evidence.
- `trace_SUITE:suspend_system_limit`, legacy and dynamic groups:
  `"Takes too long time for normal testing"`. The suite explicitly skips the
  stress case; neither group provides saturation-limit coverage.
- `distribution_SUITE` group `message_latency`: `"Not enough memory"` from
  `init_per_group`, skipping `message_latency_large_message`,
  `message_latency_large_link_exit`, `message_latency_large_monitor_exit`, and
  `message_latency_large_exit2`. These four large-payload cases remain uncovered.
- `nif_SUITE:select_error`: `"not Linux"`; requires Linux evidence.
- `nif_SUITE:consume_timeslice`: `"Debug compiled"`; requires optimized evidence.

The signal timeout is reproduced on the clean upstream debug-JIT runtime,
isolating the same case: zero passed, one failed, with the identical
`{timetrap_timeout,120000}` at `signal_SUITE.erl:1462`. Upstream has no Realm runtime
changes; its only test-source change is the unrelated EI fixture repair. This
classifies the failure as **upstream-reproducing on this host**, not as a cleared
case or proof that Realms has no timing overhead. No timetrap was relaxed and no
production signal implementation was changed. Timing/race diagnosis and any
extended-budget comparison remain outstanding.

Local raw evidence: `/tmp/beam-realms-w1-broad/first`, `signals-tracing`, and
`upstream-signal-race`, each with `summary.json` and per-step logs. The first report
is intentionally `needs_review`; the other two are `failed`. The additional batch
is recorded in `/tmp/beam-realms-w1-broad/resources-distribution` and is `failed`.
W1-05 and W1-G remain open.

Two test-harness issues were discovered rather than hidden:

1. The first ETS attempt failed **before Common Test started**, with no suite
   results. `stdlib_test` installs a release whose emulator is named
   `beam[.debug].smp`; forwarding the source-build flavor `jit` could not select
   that executable. `make/test_target_script.sh` now preserves source-build flags
   for test-data compilation and selects the installed `smp` name only for released
   Common Test. A direct released-runtime probe with the override reports
   `{debug,jit}`: the name change does not switch to the interpreter. Shell syntax
   passes; full ETS retry evidence is still required. Windows is not verified.
2. `nif_SUITE:testcase_cleanup/0` calls `driver_SUITE:check_io_debug/0`. A selected
   NIF suite does not automatically compile that other suite, so all 87 cases
   failed cleanup with `{check_io_debug,error,undef}`. The runner now explicitly
   compiles this helper into the Common Test staging directory before NIF runs.
   A plan regression test covers the dependency; the runtime retry reports
   85 passed, zero failed, and the two classified skips above.

The repaired-harness ETS/NIF retry is recorded under
`/tmp/beam-realms-w1-broad/ets-nif-retry`. Its ETS invocation was invalidated by an
edit to the shell script while release preparation was running, producing a parser
error before CT; the final file passes `sh -n`. NIF completed as recorded above.
The stable-source ETS retry under `/tmp/beam-realms-w1-broad/ets-final` reached the
runner's 1800-second limit while executing `update_counter`. CT reported 162 planned
cases but no final summary; 93 `ok` records in `suite.log` include setup/teardown
and must not be reported as 93 passing testcases. The run is `timed_out`, not a
suite pass. Its dependent queued jobs stopped rather than continuing blindly.

Subsequent isolated runs retain every failure:

- Upstream debug `distribution_SUITE:async_dist_proc_dctrlr`: zero passed, one
  failed at the identical 240-second timetrap/line 3543. Together with the prior
  signal comparison, both timeouts reproduce without Realm runtime changes.
- Optimized `processes_default_tab` and `processes_this_tab`: two passed, no skips;
  these cover the two debug-excluded process-table cases.
- Optimized `nif_SUITE:consume_timeslice`: zero passed, one failed at
  `consume_timeslice_test`, line 3469, with a traced process exiting on
  `{badmatch,1}`. The isolated upstream optimized run reproduces the same failure.
  This is not a cleared timing/accounting test or evidence of no Realm overhead.
- Optimized `ets_SUITE:update_counter`: one passed both on this branch and on
  upstream. This does not complete the timed-out full debug ETS suite. Upstream's
  released-test flavor fixture repair was applied for this test only; native
  upstream runtime code was unchanged.

Raw records: `/tmp/beam-realms-w1-broad/{upstream-distribution,opt-process-tables,
opt-nif-timeslice,upstream-opt-nif-timeslice,opt-ets-counter,upstream-opt-ets-counter}`.
At that checkpoint, old-release availability and full ETS completion were still
outstanding. The next-five follow-up below supplies installed OTP 28 compatibility
and full optimized ETS evidence; Linux-only NIF coverage, disabled stress cases,
full debug ETS completion and timing-test diagnoses remain outstanding. No production checks or
case timetraps were relaxed.

## W1-09 source inventory seed (partial)

The [discovery ledger](0001-native-inventory.md) and generated
[BIF table](0001-bif-inventory.tsv) account for 570 declared entries, C aliases,
candidate definitions and normal/dirty dispatch configurations. All rows remain
unreviewed. `python3 scripts/realm-inventory.py --check` passes; deliberately stale
inventory input returns nonzero. The same inventory also matches the independent snapshot
worktree, and all candidate locations refer to tracked sources. Six inventory
unit tests cover aliases, quoted
atoms, dispatch modes, unmatched annotations, duplicate/unsupported input,
source locations and drift. Two additional tests cover the 180-entry NIF C API
registry's signature/order/context hints and malformed/duplicate/empty input.
The lightweight GitHub workflow and the eight-cell native runtime workflow are
prepared but have not run on hosted CI; W1-06 acceptance remains open.

## Next-five regression and tooling follow-up

[Detailed evidence](0001-next-five-evidence.md) now includes full optimized-JIT
ETS (162/0/0), monitor (25/0/0), timer (23/0/0) and registration (1/0/0) runs.
The installed-release adapter gives `spawn_against_old_node` one pass/no skips on
each of optimized and debug JIT against OTP 28.4.1. This closes W1-05's discovery/
classification requirement, not W1-G regression clearance: signal/distribution/
NIF timing failures, debug ETS timeout and stress/Linux coverage remain explicit.

A source-bound native inventory adds 482 records and ten design reviews for 32
BIF/NIF API entries. The effects witness confirms three bypasses on three variants,
not isolation. Ten-repetition comparisons in both source orders retain the
restricted spawn-p99 increase; thresholds remain unagreed. The CI aggregator
rejects missing/incomplete cells but hosted execution remains absent. All 51
Python tests pass; they do not supply missing runtime/platform/security evidence.

## Shared-resource runtime follow-up

The [enforcement evidence](0001-shared-resource-enforcement.md) now records actual
atomics/counter ownership checks and restricted persistent-term denial. Resources
(44), default regressions (148) and public API (5) cases each pass on all four local
optimized/debug JIT/interpreter variants. The optimized interpreter was built and
probed in this checkout, not a fresh worktree or hosted CI environment. The updated
witness confirms the two new denials and still observes the timer bypass on all
four variants. The 52 tooling tests pass; inventory fingerprints were re-reviewed.

Matched ten-repeat native-resource microbenchmarks exist, but their 42-ns p50s are
at the clock-resolution floor; no zero-cost or performance acceptance claim is made.
Logs: `/tmp/beam-realms-resources`. Earlier evidence/counts below are historical.

## Public API and code witness evidence

`realm_api_SUITE` reports five passed, no failures/skips on each of optimized JIT,
debug JIT and debug interpreter. It covers all version-0 exports, endpoint rights,
closed/stale/wrong-kind handles, and denied self/sibling management through public
wrappers with no target closure. Kernel builds successfully with the new module.
Logs: `/tmp/beam-realms-w1-broad/public-api`.

Final batch regression reruns also pass: 55 lifecycle/process cases on each of
optimized JIT, debug JIT and debug interpreter, plus all 84 focused debug-JIT
regressions, no failures/skips. Together with the public API cases this is 60
Realm cases per variant plus 84 focused regressions. Evidence:
`/tmp/beam-realms-batch20/{debug-regressions,other-realms}/summary.json`.
The 28 Python tooling tests, both inventory drift checks, RFD links/task counts,
license scan (27 changed/new files, zero file warnings) and diff check pass.

The shared-code witness reports its expected `shared_code_only` result on all
three variants: current static/dynamic/external-fun calls see the second global
version in both Realms, while retained local funs see the first. It explicitly
reports private-environment acceptance as `not_met`, not a successful loader spike.
Logs: `/tmp/beam-realms-w2/code-probe`.

## Performance and CI status

The [benchmark record](0001-benchmarks.md) contains reproducible commands, workload
semantics, raw-result provenance and durable aggregate dispersion from matched
upstream/current host and restricted optimized-JIT runs. W1-07 has a measured
harness; W1-08 threshold agreement and broader performance clearance remain open.

`.github/workflows/realms-runtime.yaml` is manual-only and declares optimized/debug
JIT/interpreter jobs on `ubuntu-24.04` and `ubuntu-24.04-arm`, with bootstrap,
preloaded regeneration, strict Realm/focused/public-API tests and summary artifacts.
All eight hosted cells are currently **unexecuted**. Local ARM64 macOS optimized
JIT/debug JIT/debug interpreter results are not Linux CI or x86-64 evidence.
Local optimized-interpreter evidence now exists as described above. Windows,
hosted Linux/x86-64, sanitizers, OOM injection and independent security review
remain without acceptance evidence.
