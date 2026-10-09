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

# Native operation inventory: discovery ledger

This is work toward W1-09–10 and W4-16 in the
[execution checklist](0001-work-plan.md). It is **not a completed operation audit,
allowlist, or security approval**. The [initial process policy](0001-operation-policy.md)
remains the contract for checks currently implemented.

## Declared BIF dispatch inventory

[0001-bif-inventory.tsv](0001-bif-inventory.tsv) records all 570 declarations in
`erts/emulator/beam/bif.tab`, including explicit C aliases, guard/heavy/ordinary
classification, production and dirty-test scheduler routing, declaration anchors,
and candidate native definition locations. Every row is deliberately marked
`UNREVIEWED`; the existing grouped process-policy tests do not establish a complete
per-export review.

Current source facts:

| Item | Count |
| --- | ---: |
| Ordinary `bif` declarations | 490 |
| Guard/operator `ubif` declarations | 73 |
| Heavy `hbif` declarations | 7 |
| Normal scheduler, production configuration | 564 |
| Dirty CPU, production configuration | 5 |
| Dirty I/O, production configuration | 1 |
| Normal scheduler, dirty-test configuration | 548 |
| Dirty CPU, dirty-test configuration | 21 |
| Dirty I/O, dirty-test configuration | 1 |
| Declarations with at least one candidate C definition | 570 |
| Unmatched dirty-test annotations | 5 |
| Exports accepted as fully policy-reviewed by this ledger | 0 |

The unmatched annotations currently refer to `erlang:display_string/1`,
`list_to_integer/1,2`, `string_to_float/1`, and `string_to_integer/1`. They are
reported explicitly rather than silently turning into phantom native exports.
Investigate wrappers and current scheduling before removing/changing annotations;
this inventory makes no claim that they create an authority bypass.

Scheduler interpretation follows `erts/emulator/utils/make_tables`: `ubif`
overrides dirty annotations; `dirty-*-test` changes dispatch only in the dirty-test
configuration. A guard classification is **not** evidence of purity, bounded work,
or no allocation. For example, atom creation still needs a policy and budget.

### Refresh and drift checking

```sh
python3 scripts/realm-inventory.py --check
python3 -B -m unittest discover -s scripts -p 'test_realm_*.py' -v

# Only after reviewing the source delta:
python3 scripts/realm-inventory.py --write
git diff -- rfd/0001-bif-inventory.tsv
```

`--check` returns nonzero for added/removed declarations, changes in kind, C alias,
scheduler annotations, or recorded locations. Unsupported grammar and duplicate
entries fail rather than being omitted. `--write` refreshes source facts only;
it never changes `UNREVIEWED` into an approval. Do not edit the generated file to
claim enforcement. Policy decisions and their evidence belong in individual
review records and the operation contract.

Limitations are deliberate and significant:

- Candidate definitions are found syntactically in emulator C source directories.
  They are starting points for review, not a C call graph or proof that every
  platform/configuration uses an identical implementation.
- Function-body or helper/header changes that leave the recorded signature and
  locations unchanged are **not detected** by this checker. Semantic review and
  broader change-to-review tracking are still required by W4-16.
- Generated dirty wrappers, JIT/interpreter inlining, preloaded Erlang functions,
  loaded NIF exports, native resource callbacks, drivers, and asynchronous work
  are not exhaustively enumerated by `bif.tab`.
- Drift-free does not mean safe. This tool is not a runtime policy mechanism and
  must never be used to enable an untrusted profile merely because it exits zero.

## NIF API declaration inventory (W1-09, partial)

[0001-nif-api-inventory.tsv](0001-nif-api-inventory.tsv) adds 180 declarations from
`erl_nif_api_funcs.h`, preserving header order, complete signatures/attributes,
conditional declarations and source anchors. Of these, 108 mention an `ErlNifEnv*`
in their parameter expression; 72 do not. This is a syntactic hint only:
`enif_alloc_env` returns an environment without receiving caller context, and
an environment parameter is not proof of a live process or retained authority.
Callbacks and unmanaged threads need explicit provenance and ownership review.

```sh
python3 scripts/realm-inventory.py --surface nif-api --check
# After reviewing a source delta:
python3 scripts/realm-inventory.py --surface nif-api --write
```

Unknown/multiline declaration grammar, duplicates and empty input fail closed.
Conditional declarations are retained even when a platform aliases them to other
functions; the tool does not evaluate the C preprocessor or prove ABI equivalence.
Both inventory workflows check this file. All 180 entries remain unreviewed.
This is the VM's C API surface, **not** an enumeration or approval of loaded NIF
exports, arbitrary native effects, or external C library calls. W1-09/10 and
W4-16 remain open; per-entry effects, targets and cleanup still require audit.

## Native registrations, driver interfaces and source-bound reviews

[0001-native-surfaces.tsv](0001-native-surfaces.tsv) adds 482 unpreprocessed source
records: 281 NIF exports, 68 lifecycle callback slots, 113 driver APIs, one driver
global, 18 driver callback declarations and one unresolved dummy NIF table.
Conditional alternatives and NULL/reserved slots are retained, not counted as
executable effects on every platform. Source files containing these declarations
are fingerprinted; changes to their bodies invalidate this registry too.
The [five-task evidence](0001-next-five-evidence.md) describes scope and limitations.

[0001-operation-reviews.tsv](0001-operation-reviews.tsv) now holds 11 source-bound design
records for 30 BIF and seven NIF API entries, with explicit context, effects,
targets, disposition, rejection, lifetime/accounting, implementation gaps, tests
and owning workstreams. The other 713 declaration entries lack such records.
**None is approved as fully enforced or independently reviewed.** In particular,
`load_nif_2` is a rejection stub; the specialized loader path is reviewed separately
in its record, not inferred from the listed BIF's behavior.

```sh
python3 scripts/realm-native-surfaces.py --check
python3 scripts/realm-review.py --check
# Explicitly after source/design re-review, never as automatic approval:
python3 scripts/realm-native-surfaces.py --write
python3 scripts/realm-review.py --refresh-fingerprints
```

The review checker verifies required fields, declared entry membership, unique
coverage and hashes of manually listed dependencies. It detects body/helper
changes in those files even when signatures/locations stay fixed. Neither tool
proves a transitive call graph; unlisted helper changes and unenumerated paths
remain gaps. Both workflows check these registries. The original effects witness demonstrated
three bypasses. The [shared-resource implementation](0001-shared-resource-enforcement.md)
now checks atomics/counter ownership and denies restricted persistent-term access;
the witness still reports the unresolved timer bypass. Neither current fingerprints
nor these tests constitute complete runtime or security acceptance.

## Remaining surface families

These are discovery starting points, not exhaustive operation lists. Expand each
into concrete entry/path records before claiming W1-09 complete. Source paths are
relative to the repository root; references to a module name grant no authority.

| Surface | Starting anchors | Required review / present gap |
| --- | --- | --- |
| BIF entry bodies and shared helpers | Generated TSV; `beam/bif.c`, `beam/erl_process.c`, `beam/erl_bif_info.c` under `erts/emulator` | Review each export's actual effects, targets, dynamic dispatch, allocation, exceptions, traps, replies, and helper paths; all 570 rows remain open |
| Preloaded wrappers and internal Erlang APIs | `erts/preloaded/src/erlang.erl`, `erts_internal.erl`, loader/purger/init modules | Enumerate exports and alternate backends, asynchronous requester arguments, claimed identities, and system-service messages; public-wrapper denial is insufficient |
| Optimized/JIT/interpreter paths | `erts/emulator/beam/jit/arm`, `jit/x86`, `bif_instrs.tab`, `beam_makeops` in `erts/emulator/utils` | Trace emitted instructions, inline guard operations, direct imports/caches and runtime fallbacks; ensure permission/charging cannot be optimized away |
| Signals and deferred delivery | `erts/emulator/beam/erl_proc_sig_queue.c`, `erl_message.c`, `erl_hl_timer.c`, `erl_process.c` | Enumerate every sender and reply producer; retain originating authority where needed, validate destination at the right time, preserve VM cleanup, handle creator exit/name reuse |
| NIF exports and API calls | `erts/emulator/beam/erl_nif.c`, `erl_nif_api_funcs.h`; native libraries under `lib` | Enumerate loaded exports, dirty jobs, `enif_send`, allocations, monitors, resources, destructors, callbacks, and unmanaged threads; missing caller context cannot become host privilege |
| Drivers and port effects | `erts/emulator/beam/erl_driver.h`, `erl_bif_port.c`; `erts/emulator/drivers`, `erts/emulator/sys` | Enumerate command/control/call/open/close/info paths, callbacks, OS descriptors, driver threads, async jobs, ownership transfer and direct sends |
| Debugging and VM-wide controls | `erts/emulator/beam/erl_debugger.c`, `erl_bif_trace.c`; `erts_debug` entries in TSV | Review debugger registration/instrumentation/breakpoints, stack/register inspection, internal-state mutation, halt/restart, profiling and scheduler controls; blanket tracing denial does not cover every debugger/internal-state export |
| Shared mutable state | `erts/emulator/beam/erl_db.c`, `erl_bif_persistent.c`, atomics/counters/atom handling | Enumerate names/IDs, transfer/heirs, continuations, enumeration, retained values and reclamation; term/resource possession is not use authority |
| Code environments and native loading | `erts/emulator/beam/beam_bif_load.c`, loader/export/fun/purge machinery; `lib/kernel/src/code_server.erl` | Enumerate global mutation, version/purge, loader callbacks and already-loaded effects; private resolution and initialization authority are not implemented |
| Distribution | `erts/emulator/beam/dist.c`, `external.c`; `lib/kernel/src/net_kernel.erl`, `dist_util.erl` and transports | Review activation/pre-existing node state, incoming packets, controllers, decoding, native ingress and remote work; outgoing BIF denial alone is insufficient |
| Host services and brokers | `lib/kernel/src`, `lib/stdlib/src`, application/configuration/I/O/logging services | Enumerate privileged mailboxes and host-effect requests, generic MFA/eval interfaces, returned handles, deadlines and cancellation; authenticate actual Realm/grant context, not supplied sender fields |
| Resource/accounting lifecycle | Allocators, scheduler/GC/dirty work, resource-specific teardown paths | Define reservations, rollback, ancestor charges, bounded overshoot, close versus physical release, and callbacks after caller/Realm exit; no general budget enforcement exists yet |

## Per-operation review record

Each concrete operation or distinct asynchronous/optimized path needs the following
fields. Shared implementation aliases may share evidence, but each exposed entry
must remain accounted for. Record a policy decision separately from its enforcement.

1. **Entry and source:** MFA/native API/callback/opcode, all relevant arities and
   configurations, source anchors, aliases, wrappers, helper/callback chain.
2. **Execution context:** real process, dirty shadow, scheduler housekeeping,
   native thread, callback, or no live caller; how provenance is retained/validated.
3. **Effects:** reads, mutation, communication, code execution, OS work, allocation,
   scheduling, diagnostics, and potential retention after return.
4. **Targets/resources:** direct and resolved PIDs/names/aliases, destinations of
   replies, native handles, tables, code environments, endpoints, and shared state.
5. **Disposition:** Realm-local, explicit management, brokered rights, or denied;
   applicability to host, self, ancestors/descendants, siblings, and unrelated Realms.
6. **Rejection:** synchronous error or asynchronous result, malformed/missing/stale
   inputs, no unauthorized side effects, and accepted existence/timing exposure.
7. **Lifetime/accounting:** owner, admission/reservation, transfer, retained context,
   close/revoke/cancel races, rollback, final release, and CPU/memory charging.
8. **Implementation:** actual checks and remaining bypasses; do not mark a planned
   disposition enforced because a higher-level wrapper rejects one tested route.
9. **Evidence:** local positive, foreign negative, malformed/stale/forged/revoked
   handles, indirect/internal call, race/cleanup and budget tests; configurations,
   source revision, review findings, and remaining blockers.

Prioritize dangerous mutation/inspection and communication paths first: debugger
and internal-state exports, direct loader/native effects, asynchronous replies,
timers, ETS, ports, and incoming distribution. Pure-looking functions must still
be reviewed for atom creation, allocation, unbounded work, or resource decoding.

## Acceptance tracking

W1-09 and W1-10 remain **open**. Several declaration families and 37 source-bound
design dispositions now have evidence, but coverage is not exhaustive. No new
export has been approved or restricted by generating this ledger. W4-16 also remains open because a complete native/internal operation
inventory and semantic change-to-review mechanism are not implemented.
