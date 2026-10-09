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

# Shared-resource enforcement: W4-04/05 implementation slices

This follows the [negative witnesses and source review](0001-next-five-evidence.md).
It implements ordinary-use boundaries, not a complete hostile-code sandbox,
resource budget, private code environment or supported OTP application profile.
The previous batch remains committed; generated preloaded BEAMs remain excluded.

## Atomics and counters: immutable owner, checked at use

`AtomicsRef` and write-concurrency `CountersRef` now contain an immutable,
strongly retained `ErtsRealm *` owner. Constructors bind **actual process context**;
there is no supplied Realm identity/handle argument that can choose another owner.
Missing context fails before allocation. The owner reference is acquired before
publishing the magic reference and released by its existing binary destructor.

Every getter, updater, exchange/compare-exchange and info operation validates the
magic-reference kind and compares the actual caller Realm with the retained owner
before reading values, mutating cells or allocating an info result. Counter
wrappers are not the boundary: direct `erts_internal:counters_*` calls are checked,
and the atomics-backed counter implementation uses the atomics checks.
`sub`/`sub_get` wrappers reach checked native operations as well.

- Same-Realm processes can share resources, including after the creating process
  exits; ownership is **not** creator-PID ownership.
- Foreign access returns `badarg`. Host/ancestor management rights over processes
  do not implicitly grant ordinary access to their mutable arrays.
- This resource ownership rule also applies to the legacy unrestricted process
  profile. `restrict_process_access => false` is not a mutable-resource grant.
- Leaking a handle through trusted bootstrap, manager inspection, persistent
  state, or an unrelated term-transfer bypass does not authorize native use.
- Type/index/value errors retain their ordinary behavior. Unauthorized calls
  reject before mutation; successful local arithmetic semantics are unchanged.

The hot use check is an immutable pointer comparison, not a subtree-lock operation.
Creation/final destruction retain/release Realm metadata. Host metadata is immortal;
non-host final release follows the existing Realm reference-count/tree-lock path.
No resource operation changes ownership, and no manager use exemption is introduced.

### Lifetime and deliberately unfinished behavior

The resource pins its owner metadata even after Realm stop. External references
can therefore retain a child Realm's existing ancestor `max_realms` charge; the
charge returns only after all relevant resources/handles/deferred processes are
physically released. This prevents owner-pointer reuse/ABA and premature metadata
freeing. A dedicated test removes creator/root/management-handle references and
shows child admission remains blocked until the last resource holder exits.

This is **not** aggregate native-byte accounting. Arrays have no per-Realm byte
reservation, CPU charge, cancellation list or explicit revocation flag yet.
Current `realm:close/1` does not revoke existing arrays or stop existing members
from creating/using them. Close-versus-use linearization and generalized resource
admission remain W5/W6 work; they are not inferred from the ownership check.
An object may remain physically allocated behind an unusable foreign handle.

## Persistent terms: explicit restricted-profile denial

All seven public persistent-term BIF entries now reject restricted callers before
lookup, scans, update permission, context/literal allocation or publication:
`get/0,1,2`, `put/2`, `put_new/2`, `erase/1` and `info/0`. Direct and dynamic calls
reach the same native checks. Even a missing key with a default raises `badarg`
under this unsupported profile; there is no global-key existence oracle here.
The internal global erase operation is host-only, including denial for legacy
non-host Realms and absent context.

Host persistent-term behavior remains global and unchanged. Legacy non-host
profiles still have public global access and are not production-safe. The VM-only
`erts_persistent_term_get(key)` helper is unchanged; its in-tree native caller is
the distribution machinery in `dist.c`. This is not an approved general native
access API. Native/debug/distribution paths remain subject to the unfinished W3/W4
audit, and arbitrary approved native code is still inside the trusted boundary.

Process Realm membership/policy is immutable, so an authorized host operation
cannot become a restricted operation while trapped. Existing host continuation,
update-permission, literal-reclamation and cancellation paths remain intact.
No new tenant-owned persistent entries are created by the denied profile.

This is temporary compatibility loss, not a Realm-local persistent-term namespace.
OTP/library code that depends on persistent terms must be adapted or remain outside
the supported tenant profile. Module names, platform-code provenance and prior
native loading do not grant an exception.

## Tests and audit evidence

`realm_resource_SUITE` adds nine cases covering local operations, spawn inheritance,
creator exit, leaked host/sibling handles across both process profiles, parent/child
management without resource authority, stopped owners, retained-owner quota return,
and persistent-term denial without changing host entries. Wrong-type/index cases
and direct internal counter calls are included. The fixtures deliberately use
trusted bootstrap closures and management inspection to leak references; they do
not claim W3's fixture migration or W2's private-fun semantics are complete.

`--profile resources` runs those nine cases plus complete `atomics_SUITE` (7),
`counters_SUITE` (6) and `persistent_term_SUITE` (22): **44 passed, no skips/failures,
on optimized/debug JIT and optimized/debug interpreter**. All four variants were
built and identity-probed on ARM64 macOS, including the previously untested local
optimized interpreter. Logs: `/tmp/beam-realms-resources/{shared-state,opt-emu-resources}/summary.json`.
This is not a fresh-worktree interpreter rebuild or hosted Linux/x86-64 coverage.
The earlier ownership-only checkpoint passed 21 cases per variant under `first`.

The default Realm suite count is now 64 (45 lifecycle + 10 process + 9 resources),
plus 84 focused cases for `all`; public API adds five per variant. CI's expected
per-cell core/API total is therefore 153. The full 148-case default profile and
five-case API profile pass on all four local variants with no skips/failures.
Evidence: `/tmp/beam-realms-resources/{regressions,public-api,opt-emu-regressions,opt-emu-api}`.
Hosted execution remains unclaimed.

The effects witness now expects foreign atomics and restricted persistent-term
access to be **denied**. It still expects host-timer ingress to succeed, preserving
negative evidence for the outstanding deferred-delivery bypass. It confirms both
new denials and the remaining timer bypass on all four variants under
`/tmp/beam-realms-resources/effects-final`. No assertion was weakened to turn that
timer path into a boundary pass. The 52 Python tooling tests also pass.

The source review checker correctly rejected stale atomics dependencies before
refresh. Re-reviewed records now include counter backends and the Realm lifetime
implementation/wrappers: 11 records cover 37 declarations; 713 declared entries
still lack design records. Full-file fingerprints are not a transitive call graph
or independent security approval. Native scope, allocation failure, closure races,
32-bit builds, hosted platforms, sanitizers and independent review remain open.

## Performance and remaining gates

The benchmark fixture adds three owned-resource workloads: atomics `add_get`,
atomics-backed counter add/read and write-concurrency counter add/read. Creation is
outside the timed loop. Baseline/current reports must use this same expanded
fixture; old ten-workload reports remain historical evidence, not interchangeable
inputs to the new strict comparison.

Matched optimized-JIT upstream/current measurements completed with 5000 samples ×
ten fresh-VM repetitions for upstream host and current host/restricted modes.
[Durable aggregates](0001-resource-benchmark-results.tsv) preserve dispersion;
raw reports and comparison are under `/tmp/beam-realms-resources/bench-{upstream,current}`
and `/tmp/beam-realms-resources/comparison.json`.

All three new workloads show median p50 of 42 ns in both runtimes/modes, at this
host's roughly 42-ns clock resolution. Counter add/read p99 medians are 84 ns;
atomics p99 medians range between 42 and 62.5 ns across modes. These observations
cannot establish zero overhead or a speedup: loop/timer quantization dominates,
and allocation/final destruction is outside these loops. Batched-operation,
contention, allocation/reclamation and broader-host measurements remain necessary.
No threshold or performance acceptance is inferred. W4-04/05 remain open for full scoped-service/
alternate-path review and other mutable resources; W5/W6 resource lifecycle/budgets
and all eight package gates remain open. **Untrusted workloads remain no-go.**
