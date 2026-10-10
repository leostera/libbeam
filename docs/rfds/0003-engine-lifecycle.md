<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 Leandro Ostera <leandro@ostera.io> -->

# Additive Engine lifecycle over the actual execution substrate

Status: **Implemented G1 slice; stateful Isolate acceptance remains red.**

This is a snapshot of the C ownership/adapter integration required by
[RFD 0003](0003-additive-runtime-construction.md), following
[first execution](0003-first-execution.md) and [ordinary binaries](0003-owned-binaries.md).
It does not expand the admitted instruction/native profile or declare the RFD done.
The subsequent [owned executor](0003-owned-executor.md) adds a lazy shared worker;
the no-worker statements below describe this original G1 checkpoint.
The unchanged `libbeam/examples/engine_lifecycle.cpp` now creates, shuts down,
destroys and recreates the Engine in one host. The unchanged `two_isolates.cpp`
gets past Engine creation and explicitly refuses `Engine::create_isolate`.

## What the Engine owns

`libbeam/core/engine.c`, `engine.h` and `engine_internal.h` are new C ownership
coordination, not a transplant of OTP startup. They construct the allocation
substrate used by real loading, process heaps, copying GC and ordinary binary
payloads, and the positive-listed native implementation catalog previously
constructed separately by each code space.

The catalog contains the real `erlang:get_module_info/1,2` implementations, their
predefined atom identities, arities and HEAVY classification. Code-space creation
now requires an `LbEngine *`; loader import resolution, transform predicates and
native trampoline binding read that Engine's catalog. Processes execute those
bound functions. Generated BIF identities still grant no capability by themselves.
There is no old-domain-only code-space constructor or alternative runtime path.

Private atom/module/export tables, executable words, literals and process state
remain code-space/module/process-owned. Mutable native exports and trampolines
are private per space; only the implementation catalog is shared. Allocation-domain
bookkeeping is Engine infrastructure, not shared Erlang state. Immutable generated
instruction/atom definitions and implementation code need no per-Engine copies.

Construction creates **no code space**, process, OTP service, worker, TLS key,
poll descriptor, clock service or process-wide initialization claim. The current
executor is serialized and explicitly driven by its caller. This Engine owns the
actual infrastructure that executor uses, not an empty success handle. Scheduling,
timers and future worker resources must acquire their own fallible construction
and stop/join protocols when admitted; they are not implicitly implemented here.

## Ownership and transitions

- `lb_engine_create(allocator, out)` has three allocation stages: the domain,
  Engine control, and implementation catalog. Invalid input clears the output;
  failure unwinds every completed allocation before returning an error.
- A code space publishes one parent reference only after its atom namespace and
  private native exports are fully constructed. Construction failure never
  publishes a child. Child-count overflow refuses before allocation.
- `lb_engine_shutdown` refuses **busy without mutation** while any space remains.
  Existing module/process/entry/atom-borrower guards in turn prevent premature
  space destruction. Physical space release precedes releasing its parent.
- Successful shutdown frees the execution catalog and closes admission. It keeps
  only a finite closed control handle and its bookkeeping domain: two C
  allocations, no execution resources. The C++ adapter additionally retains its
  one-pointer `Impl`. A later shutdown or space creation returns closed.
- `lb_engine_release` consumes the single host-owner reference. If children
  remain, it closes Engine admission without force-freeing them. Existing spaces
  can continue loading/executing and destroy themselves normally. The last
  physical child release then frees the catalog, control and empty domain.
  This is finite parent retention, not an immortal singleton or owner resurrection.
- All operations remain serialized; allocator callbacks must not reenter their
  domain. Callbacks/context outlive the closed handle and every retaining child.
  The catalog is immutable between construction and destruction. Internal opaque
  handles are trusted live pointers, not validators for arbitrary/stale addresses.

Dropping a future public Isolate/Call handle will need its own closure/drain
protocol. This implementation proves parent retention for the existing execution
objects, not cancellation, process termination or queued-work retirement.

## C++ adapter and build boundary

`Engine::Impl` owns one C handle and releases it through the same C protocol.
`Engine::create()` performs real construction, maps allocation failure to `limit`,
and unwinds its adapter allocation on failure. A `std::bad_alloc` while constructing
an error falls back to a typed error with an empty diagnostic, without allocating
another string. Move leaves a safely destructible empty handle; operations on it
return `invalid_state`.

`shutdown(deadline)` checks an expired deadline before changing the active Engine;
that timeout permits retry. There is no waiting work in this profile. Successful
shutdown closes the handle; repeated shutdown returns `closed`. Isolate creation
returns `not_implemented` while active and `closed` after shutdown. The public API
signatures and both acceptance examples are unchanged.

The production `libbeam` target now always links `libbeam_core`, never the optional
ERTS package. The package remains available only to explicit historical diagnostic
targets. The old startup probe's legacy C++ preflight is no longer enabled. The
preparation diagnostic checks that the additive factory is independent of the old
claim; its argument-copy/shared-prefix work remains preserved. Those old native
preparation/release paths are **not validated by this slice** and remain unfinished.
The factory retains the earlier source comments warning against either legacy
preparation or whole-world startup as a lifecycle shortcut.

## Provenance and alternatives

Baseline before this integration: `6f0874ff`. Existing C machinery and licenses
remain covered by `core/otp/{loader,execution,binary}-sources.json`, generated
opcode inputs and their linked admission records. Native catalog/lookup structures
move from the already executed `code.c`/`code_internal.h`; generated transform
locals now use const descriptor pointers without changing their identity comparisons.
No new BEAM semantics,
term representation, bytecode format or alternate interpreter was introduced.

Leaving a separate catalog per space would avoid API changes but still leave the
execution path without a retaining Engine owner. Publishing only an allocation
handle would not meet G1. Reusing old ERTS preparation would restore incomplete
global teardown and hidden startup dependencies. The cost of the chosen boundary
is explicit parent retention and a small closed-handle footprint; it does not yet
supply concurrent allocation, quotas or a compact per-module representation.

## Evidence

Fresh locked initial run:
`/tmp/libbeam-engine-lifecycle-first/summary.json`.
Final candidate: `/tmp/libbeam-engine-lifecycle-commit-fixed/summary.json`.
Tooling: `/tmp/libbeam-engine-lifecycle-tooling-final/summary.json`.
Runs hash inputs, retain command logs and compile fixtures only in external output
directories. The retained `commit-reviewed/build.log` records the intervening
compile refusal when a writable generated BIF local met the newly const catalog;
the projection now preserves read-only references, and the full fixed run passes.

- Nine CTests in Debug and Release, including the unchanged lifecycle target.
- All **three Engine initialization allocation failures**, exact balance and retry;
  independent simultaneous Engine controls, implicit cleanup and closed admission.
- All **1,159 code-space construction failures**, exact parent/resource balance;
  busy borrower refusal, shared catalog/private namespaces, overflow refusal and
  out-of-order owner drop. The final run retries every prefix and injects
  Engine initialization failure beside a retained peer.
- The adapter's real allocation failure, retry, move/destruction, closed handles
  and nonmutating deadline expiry. All 29 tooling tests pass; the example-link
  test configures a deliberately nonexistent optional ERTS archive, proving the
  production target does not acquire that diagnostic dependency.
- Existing first/linked/growth execution, all 80/472 loading failure prefixes,
  binary execution/retirement and its 94 load prefixes continue through Engine
  ownership. The linked test drops the Engine owner while a process remains,
  then executes a bounded loop and actual native module-info call before final
  physical release reaches zero tracked resources.
- UBSan covers C Engine, C++ adapter and unchanged lifecycle, separately from the
  existing execution/binary UBSan runs. No unresolved legacy runtime/thread
  symbols are allowed in the additive archive.
- `ENGINE_LIFECYCLE_OK create_shutdown_create=true` is actual libbeam evidence.
  Stock OTP semantics and MD5 remain a separate reference check.
- `two_isolates` still exits 1 at `Engine::create_isolate`. No stateful-world,
  public binary-call/completion, worker shutdown, security, performance, Linux,
  concurrency, pinned-OTP-30-compiler or ASan claim follows from this host run.

Next: extend the connected execution path and B-cluster ownership toward public
worlds, ordinary processes/mailboxes/timers and unchanged stateful acceptance.
Do not reinterpret G1 as completion of A12, B11 or RFD 0003.
