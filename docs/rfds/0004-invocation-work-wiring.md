<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 Leandro Ostera <leandro@ostera.io> -->

# RFD 0004 invocation ledger and wiring plan

Status: **Resource enforcement is complete for the currently admitted runtime
profile**: accounted memory, default/override work credit, dispatch/native/GC/message
charging, causal attribution, cancellation containment and explicit exclusions are
implemented and integrated-tested. The Midgard-required runtime capabilities and
whole-application goal remain unfinished; each newly admitted capability must extend
this inventory and its tests. This implements the [selected policy](0004-isolate-resource-policy.md),
not a relaxation of the goal.

## Implemented primitive

`work_budget.c/.h` supplies world-accounted, nonmoving C records with checked
remaining credit, irreversible exhaustion/escape facts, and explicit reference
leases. An independently accepted invocation must create a fresh record; inherited
child/message work must retain the original record rather than copy its remaining
counter. Zero charge does not renew credit; exhaustion cannot be cleared. Checked
`ceil(quantity / 16)` does not overflow even at UINT64_MAX. World destruction
refuses a retained ledger. A NULL record is only an explicit raw component or host
management sentinel, never an admitted public invocation.

The unit test checks independent credit, retained lease lifetime/accounting,
exact/over-threshold, reference-capacity refusal and UINT64_MAX behavior. Fresh
`/tmp/libbeam-work-ledger-integrated-first/summary.json` passes all 200 commands;
the new ledger gate runs in Debug, Release and available UBSan, and unchanged
lifecycle/two-Isolate acceptance remains green. This test does not run guest
instructions or claim that the experimental public work settings are already
enforced.

## Runtime checkpoint

Every accepted public root owns fresh finite credit. Children inherit its active
owner; messages retain their sender's owner. Receive candidate examination/import
switches ownership before dispatch; skip restores the previous owned lease, while
consumption continues the accepted owner. Native `remove_message` and marker cursor
repair are wired, not just the component consume helper. Waiting/polling does not
replenish credit. Existing raw component/management operations remain explicitly
unmetered, not alternative public invocation paths.

The serialized domain borrow carries the current owner into existing C helpers,
without TLS. First work failure additionally retains its **own** attribution lease:
restoring a prior receive/flush owner or destroying a message cannot erase the
actual failing sender. Constructor rollback and worker cleanup clear both borrow
and failure lease before releasing retained metadata. Assertions used by generated
`CHECK_ARGS` remain pure; private-directory checking is precharged at dispatch.

Charges currently cover instruction dispatch, allocation/preparation initialization,
binary construction/copies/zeroing, term equality/size/ownership traversal, decimal
continuations, numeric preparation, native option/error scans, code/trace metadata
and collector preparation. GC prepays a conservative roots/copy/sweep/code-mark
bound before forwarding; there are no newly fallible checks halfway through GC.
This is an estimate, not elapsed time. The integrated checkpoint below records
threshold evidence for the currently admitted helpers; code loading, setup, scheduler
bookkeeping and mandatory cleanup/reclamation remain explicitly outside guest work.

Private-family work/memory-limit failure cancels its pending birth-family tasks
and permits loaded-world reuse. Registered names, cross-family sends/monitors and
kill effects conservatively mark affected owners escaped before publication. A
failure after such effects closes/drains the world, preserves the original limit
status and cancels other pending work. Completed copied replies are not rewritten
by later background failure. Public `Call::cancel()` uses the same containment and
reports cancellation separately from work or memory limits. Callback OOM retains
the established infrastructure propagation/cleanup behavior; quota cancellation
must not suppress the monitor infrastructure wake that existing OOM tests require.
No generic actor recovery or guest-state rollback is promised.

`core_work_runtime_test` actually executes compiler-produced BEAM on the shared
worker. It measures exact pure-call threshold/one-unit-below refusal, independent
fresh credit with another pending family, private child cancellation/reuse, old
background credit after a ready reply, explicit cancellation, escaped retirement
and peer progress. Queued-noise scans charge their original sender; subsequent
requests do not borrow the actor's birth allowance. `api_work_test` exercises real
C++ Isolates/defaults/overrides, repeated host timeouts without reset, distinct
terminal errors, private reuse and physical zero. No-argument gates are separate
from these actual fixture-backed runtime commands.

The older writable-initializer stop-focused gate now explicitly raises that
world's finite work limit to UINT64_MAX: optimized Release can exhaust the default
while its peer initializes a large binary. This does not change the application,
stateful acceptance source or quota-test limits, nor interpret zero as unlimited.

## Wiring invariants and scope exclusions

1. Root admission allocates a record before process/task publication, alongside
   reserved completion capacity. Failed preparation releases its exact prefix.
   The Call retains the owner while pending; consumed/dropped terminal controls
   release their lease and keep only finite scalar diagnostics if needed.
2. Each process owns separate **birth-family** and **active causal** leases.
   A spawned child inherits the parent's active owner. Prepare those leases
   before the existing PID/monitor/runnable publication boundary, with no newly
   fallible work after publication. Destruction releases all leases exactly once.
3. Messages retain the sender's causal owner before queue publication. Cross-family
   send marks both affected owners escaped before publishing the effect. Normal
   monitor delivery likewise has explicit causal ownership; quota/family teardown
   drains privately without fabricating guest DOWN.
4. Before a native `loop_rec` instruction/import examines a candidate, activate
   that candidate's owner. Keep the previous owner in an owned restoration slot
   through matching/safepoints. Skip restores it; consumption releases it and
   continues the accepted candidate's owner. Empty waiting consumes no idle work.
   Message unlink, stale-marker repair, queue drain and process teardown must not
   leave dangling candidate/budget aliases.
5. Every actual interpreter dispatch charges a unit. Native bulk work, helper
   traversal and GC have explicit checked charging/continuation points; ordinary
   scheduler reductions remain scheduling, not the invocation allowance. Retrying
   a wait or scheduling a new slice cannot replenish an owner.
6. A serialized, scoped domain borrow may carry the active owner for helpers that
   already accept the owned allocation domain but not a process. It is not TLS:
   set it from an owned process/constructor lease, update it on receive transfer,
   and clear it before cleanup/unlock. Never leave a dangling borrow after release
   or accidentally meter preparation/peer work against the previous process.
7. GC must precharge a defensible full-copy bound before its first forwarding
   write. Adding a fallible work check halfway through moving roots would break
   the no-failure-after-commit collector invariant. Large copies/bit processing
   similarly precharge bounded spans; long traversals use bounded checkpoints.
8. Work failure is terminal infrastructure, not guest `badarg`, false guard,
   catchable exception or monitor reason. Check it before guest error/side-effect
   publication, including native BIF and exceptional trace paths.
9. On exhaustion/cancellation, retain the originating owner through cleanup. For
   an unescaped private family, terminate its pending birth-family tasks and
   owned queued effects at safepoints; preserve already-terminal copied results
   and loaded-world reuse. If effects escaped to existing actors/invocations,
   close/drain the world, preserve the originating limit/cancel outcome and cancel
   other pending work. Never promise state rollback or generic actor recovery.
10. A reply may precede background quiescence. Remaining tasks/messages retain
    the old owner; later independently admitted calls get fresh records without
    erasing old charges. The old memory-only solitary-process heuristic is removed;
    work and memory denial use the same family/escape facts.

## Integrated checkpoint evidence

Fresh `/tmp/libbeam-work-native-boundary-integrated-twentyfourth/summary.json` passes **207 commands**.
The new actual-worker C and public C++ work suites pass Debug, Release and available
UBSan. Unchanged lifecycle/two-Isolate acceptance is green. Observed pure-call cost
is 141 units for this exact fixture/build profile (per-instruction native target
validation and callsite metadata scans are charged); it is not a policy constant or
performance claim. Root admission now faults all seven measured allocation prefixes;
existing child/monitor/message/reclamation failure gates remain green.

The subsequent native-loop review hardened first-failure preservation (the first
work, memory-quota or allocator-OOM cause cannot be overwritten by a later failure),
made registry unlink constant-time, charged fixed-eight receive-marker scans and
message unlinks before mutation, accounted native trace-catalog scans, binary vheap
rounding and per-dispatch code-pointer scans, and bounded bignum-to-float and
small-decimal charges. PID free-ring
removal, receive/monitor drain and allocator-node release loops remain cleanup/bookkeeping:
they must run to preserve ownership after cancellation and are not a hard CPU bound.
Work units estimate admitted guest work; they do not count every host/control or
mandatory-reclamation instruction.

Actual-worker evidence now also checks charge thresholds around native algorithms.
A non-tail BEAM BIF call prevents constant folding: the unchanged decimal BIF
formats the real 8192-bit integer at 12,954 units and its 2467 result bytes match a
separately computed exact decimal MD5. The small-integer formatter costs 355 units;
a real bignum-to-float binary segment produces independently verified bytes
`58f0000000000000` at 287 units. A BEAM-built 256-element nested list compares
deeply under real term equality at 85,315 units. Eight real spawn_opt/monitor/DOWN
cycles cost 20,177 units across child execution, monitor publication and received
messages. For all five, exact measured credit succeeds and one unit less fails; the
141-unit pure path and native thresholds are identical in Debug, Release and UBSan.
A selective-receive fixture builds and collects its heap before waiting; the worker
reports three real collections, its credit remains stable while idle, and cancellation
drains it. The native collector component measures its bound after prepaying the full-width
binary-vheap threshold-rounding allowance; exact succeeds and one below denies
before forwarding, preserving roots/peer collection and releasing all backing.
The allowance is a conservative 2048 work units for bounded uint64 growth and
wide-rounding loops, not an elapsed-time estimate. Exact thresholds are evidence for
the named fixtures; the source-level inventory covers the current admitted profile,
not capabilities that have not yet been implemented for Midgard.

A further actual-worker test makes a registered receiving root return the sender's
real 32-KiB binary after the sender already returned `delivered`. Copying the
receiver's completion exceeds the sender's remaining credit: the ready sender
reply survives, the receiving root reports cancellation, and the escaped world
closes. Completion status remapping must not erase the underlying quota fact and
omit containment. Both work and memory completion attribution use the causal owner.

Native dynamic tuple construction, frame initialization, closure-environment
construction and linear selection now precharge/checkpoint in the complete
projected cases. PID insertion prepays its worst-case ring search before any
identity counter mutation; logical removal/cleanup remains unmetered release work.
Together with instruction dispatch and native helper review, these complete the
source-level coverage inventory for the current admitted profile. Future runtime
capabilities require new charges and boundary evidence.

Retained internal failures include the standalone UBSan missing-source link, the
callback-OOM monitor-wake regression, missing generated receive-consumption ownership
hook, a mutable assertion predicate, and the Release stop-versus-default-work-limit
race. Each was corrected rather than reclassifying failed tests as acceptance.
The standalone work-ledger test now forces an exhausted sender failure while a
separate receiver budget is borrowed, proves the first causal owner remains leased,
and verifies that later memory denial cannot overwrite the first work failure; the
inverse memory-first/work-later case preserves memory as the operation result too.
An injected allocator callback OOM followed by work denial keeps `NO_MEMORY` as the
first cause and never fabricates a quota-owner lease.
When completion copying exhausts a borrowed sender budget after the worker has
changed owners, the executor reselects the retained first-failure owner before
family cancellation. The shared-worker test asserts the sender is exhausted while
the receiver's root budget is not.
The public work API test also reads both default policy values and runs a real
zero-override default-credit call before testing lower Engine defaults and per-world
overrides, and rejects zero-valued Engine/Isolate work limits. A one-unit-below
monitored-spawn run reports a distinct work-limit outcome,
reuses the contained Isolate for a pure call, and leaves the independent peer live.
The inventory in this checkpoint covers every currently admitted interpreter and
native runtime path; cleanup, allocator-node lookup and worker scheduling are listed
as explicit exclusions, not silently charged work. The Midgard-required modules and
operations are not yet admitted, so this is not an application-wide coverage claim;
every capability added for that profile must extend this inventory and its boundary
tests. Root admission faults all seven measured allocation prefixes, and existing
child/monitor/message/reclamation rollback gates remain green. No hard CPU-time or
security guarantee follows from this work ledger.

## Requirements for extending the admitted profile

Every new opcode, BIF, helper or representation needed by Midgard must enter the
native charging inventory with actual-worker threshold/failure tests. Such tests
must preserve causal ownership, dispatch status distinctions, moving-GC commit
boundaries, private reuse or escaped-world retirement, peers, ready/detached replies
and exact resource balance. Repeat fresh Debug/Release/UBSan integration and the
unchanged lifecycle/two-Isolate acceptance at each integrated boundary. This
checkpoint covers only the profile that currently loads; it does not claim
whole-application resource enforcement. The 10 million work-unit default is not
milliseconds, a security boundary or a hard CPU-time guarantee.
