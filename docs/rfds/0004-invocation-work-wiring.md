<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 Leandro Ostera <leandro@ostera.io> -->

# RFD 0004 invocation ledger and wiring plan

Status: **Owned credit, actual worker dispatch charging, causal process/message
wiring and family cancellation implemented as an integrated checkpoint. Native
charging coverage and broader failure-boundary tests are still under audit; the
resource milestone and whole-application goal remain unfinished.** This implements
the [selected policy](0004-isolate-resource-policy.md), not a relaxation of the goal.

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
This is an estimate, not elapsed time; small/large helper coverage still needs its
complete inventory and focused threshold/failure evidence before task completion.

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

## Wiring invariants and remaining audit

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

Fresh `/tmp/libbeam-causal-work-integrated-third/summary.json` passes **207 commands**.
The new actual-worker C and public C++ work suites pass Debug, Release and available
UBSan. Unchanged lifecycle/two-Isolate acceptance is green. Observed pure-call cost
is 95 units for this exact fixture/build profile; it is not a policy constant or
performance claim. Root admission now faults all seven measured allocation prefixes;
existing child/monitor/message/reclamation failure gates remain green.

Retained internal failures include the standalone UBSan missing-source link, the
callback-OOM monitor-wake regression, missing generated receive-consumption ownership
hook, a mutable assertion predicate, and the Release stop-versus-default-work-limit
race. Each was corrected rather than reclassifying failed tests as acceptance.
The independent sender-failure lease and complete native charging audit still need
more threshold/fault coverage; no resource-task completion is claimed.

## Required evidence before claiming complete enforcement

Threshold/default/override tests must exercise actual shared-worker loops, native
copy/traversal/decimal/GC work, child inheritance, receive scan/skip/consume owner
transfer, background work after response, repeated waits without reset, simultaneous
calls, private family reuse and escaped actor retirement. Fault all newly introduced
metadata prefixes. Preserve peers, closed controls, ready/detached replies and exact
resource balance. Fresh Debug/Release/UBSan integration and unchanged stateful
acceptance remain required. The default 10 million units is not milliseconds or a
security/performance guarantee, and an unfinished native charging audit cannot be
called full enforcement.
