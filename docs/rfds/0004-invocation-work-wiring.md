<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 Leandro Ostera <leandro@ostera.io> -->

# RFD 0004 invocation ledger and wiring plan

Status: **Stable owned credit ledger implemented; production charging, causal
message/process wiring and cancellation not implemented yet.** This is a working
implementation plan under the [selected policy](0004-isolate-resource-policy.md),
not acceptance evidence or a relaxation of the whole-application goal.

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

## Next coherent wiring

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
    erasing old charges. Replace the current memory-only solitary-process check
    with the same proved family containment rather than keeping two policies.

## Required evidence before claiming enforcement

Threshold/default/override tests must exercise actual shared-worker loops, native
copy/traversal/decimal/GC work, child inheritance, receive scan/skip/consume owner
transfer, background work after response, repeated waits without reset, simultaneous
calls, private family reuse and escaped actor retirement. Fault all newly introduced
metadata prefixes. Preserve peers, closed controls, ready/detached replies and exact
resource balance. Fresh Debug/Release/UBSan integration and unchanged stateful
acceptance remain required. The default 10 million units is not milliseconds or a
security/performance guarantee, and an unfinished native charging audit cannot be
called full enforcement.
