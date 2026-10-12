<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 Leandro Ostera <leandro@ostera.io> -->

# RFD 0004 memory implementation checkpoint

Status: **Per-world memory accounting/enforcement implemented; fresh integrated
Debug/Release/UBSan validation passed. Invocation work enforcement/causal family
cancellation remain unfinished; this is not completion of the resource milestone
or whole-application goal.**

The [policy](0004-isolate-resource-policy.md) selected 256 MiB default accounted
world memory and 10 million per-invocation work units before implementation.
The policy fields are resolved immutably at Engine/Isolate creation. At this
memory checkpoint work settings were metadata only. The later
[invocation wiring checkpoint](0004-invocation-work-wiring.md) implements fresh
credit/dispatch/causal ownership and replaces the interim cancellation heuristic;
its native charging audit remains unfinished.

## Ownership and counters

`alloc.c` retains the original owned-node unknown-pointer checking and alignment.
Each node now records requested backing **including its allocation header**;
a world-embedded child domain records current, peak, finite positive limit and
causal allocation-operation failure. Check addition and available credit before
calling the system allocator. Policy denial performs no callback and returns a
separate `LB_ALLOC_LIMIT`; callback failure remains `LB_ALLOC_NO_MEMORY`.
Decrement only after the release callback physically releases backing.

The embedded domain copies the Engine allocator callbacks and retains a parent
child lease. Parent destruction refuses outstanding child scopes, even when the
parent has no allocation nodes. A scope refuses live backing; its stable storage
outlives code/actors and ready internal replies, and is destroyed before its world
control. No per-Isolate emulator, tenant TLS or extra runtime is introduced.

Private code space/atoms and all their loader, verifier, literal, process, GC,
binary, mailbox, monitor and registry allocations already flow through the code
space domain. Task records now follow that same domain instead of the Engine.
Ready internally copied output bytes are charged to the world until consumed or
discarded, including after code-space reclamation. Raw component code spaces still
use the shared unbounded Engine bootstrap domain; those are not public Isolates.

Count backing capacity, retained preparation arenas and simultaneous old/new GC
buffers, not just live terms. Peak remains cumulative for the world's lifetime.
The diagnostic operation result is cleared under serialization before a load,
root admission or worker slice; it distinguishes quota denial from callback OOM
when adapting older fallible native helper return codes. Native worker denial is
infrastructure failure, not a catchable guest `badarg` or fabricated exception.

## Explicit exclusions

The shared Engine catalog/synchronization/worker, static compiled source/catalog
bytes and allocator carriers are not world-owned backing. World/Call/Reclamation
**host management controls** and the fixed embedded ledger reside in the shared
control allocation domain; they are retained finite tombstones, not guest runtime
storage. They remain independently bounded by handle/transport ownership.
Isolate-owned task storage and pending process/input/output backing cannot escape
charging by moving to that domain. Independently copied host vectors and runner
file buffers are excluded; a ready internal reply is not yet a detached vector.
Native host stacks, malloc carrier overhead and other host application memory are
not included. This is not RSS, a process-wide cap or security containment.

## Public behavior and current cancellation boundary

Public `ResourceLimits` supply Engine defaults and `ResourceOverrides` selectively
replace them per new world. Zero is invalid, never unlimited. A valid tiny memory
policy may construct an Engine but refuse world bootstrap with `memory_limit`;
failed construction unwinds the scope and leaves the Engine reusable. The default
256 MiB is now enforced on public Isolates. `resource_usage()` observes current,
peak and resolved policy, including a retained closed control without revival.

Loader/admission quota failure rolls back the unpublished allocation prefix.
Runtime quota failure is a distinct memory-limit terminal status. **Historical
checkpoint boundary (superseded by the later invocation wiring):** a solitary
private invocation could retire its own heap/task and leave loaded code reusable.
If another same-world process is still active, the interim conservative policy
closes/drains that world after publishing the originating limit outcome; peer
worlds survive. The next causal-budget implementation must replace this interim
solitary check with proved private-family containment, including messages and
persistent actors. The current checkpoint does **not** establish full safe family
cancellation, per-invocation native work charging or generic actor recovery.

## Evidence

The allocator gate checks exact header-inclusive threshold, no callback on denial,
OOM distinction, old-plus-new temporary peak rejection, parent child leases,
unknown/wrong-domain pointer rejection, peer contents and exact final balance.
Existing world failure-prefix and physical-reclamation tests ran through the
moved domain/task paths; an initial focused command then failed because it supplied
incorrect probe file paths, not a guest/runtime failure.

The public fixture [memory_slice.erl](../../libbeam/tests/fixtures/memory_slice.erl)
uses ordinary native equality and dynamic integer-segment construction to allocate
16 MiB, consume the resulting real binary and return `grown`. It is not an oversized
literal, host-synthesized output or replacement for the target application.

`api_memory_test` measures the real peak on a fresh default world, then proves that
an exactly capped world executes and a world capped one byte below fails with a
memory-limit status, safely serves a smaller subsequent pure call, and does not
poison a peer. Bootstrap denial/override, zero rejection, detached result lifetime
and zero charged backing after physical stop are checked separately.

Focused evidence: `/tmp/libbeam-memory-dynamic-supported/1.log`:

```
API_MEMORY_OK default_mib=256 exact_peak=16947868 load_peak=653725
threshold=true pure_reuse=true peer_survives=true physical_zero=true
```

Fresh integrated evidence: `/tmp/libbeam-memory-integrated-first/summary.json`
passes all 200 commands, including unchanged lifecycle/two-Isolate acceptance and
both memory suites in Debug, Release and available UBSan. `core_memory_test`
executes the actual collector under a configured world domain (not host bytecode
interpretation): it verifies exact old-plus-new heap charging, denial before the
first forwarding write, unchanged roots/heap after denial, successful peer
collection, physical zero and exact allocator balance. Debug observed peaks are
16,947,956 bytes for dynamic construction and 8,558,708 bytes for the collector
case; Release/UBSan differ slightly with build layout.

These byte counts are compiler/build/fixture-specific observations, not fixed
policy constants or deployment performance promises. The first memory fixture
used an unadmitted `bs_get_position` case and was honestly refused; the bounded
memory fixture now tests equality/segment construction already in the positive
profile. No application source was changed to avoid that missing capability;
whole Midgard still requires the wider binary runtime slice.
