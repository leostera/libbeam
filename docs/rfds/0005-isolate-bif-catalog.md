<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 Leandro Ostera <leandro@ostera.io> -->

# RFD 0005: Complete BIF inventory and Isolate capability boundaries

Status: **Design in progress. The user authorized catalogued runtime denials;
this document does not claim they are implemented.**

This extends [eager admission](0004-eager-batch-admission.md) and preserves
[causal work policy](0004-invocation-work-wiring.md). The workspace was checkpointed
at `cebaa7f6`. The [185-MFA requirement list](../../libbeam/core/otp/bif-batch-request.txt)
is authoritative; the [inventory work](../bif-batch-inventory.md) is not yet a
completed per-MFA coverage matrix.

## Decision and invariant

Every import still resolves to a real, reviewed entry before group publication.
An absent module/export or unclassified entry refuses the whole group. A known
entry may implement either real supported behavior or an explicit capability
boundary. The latter is callable but cannot perform its prohibited operation.
It fails the invocation at the host boundary, not as a catchable Erlang error.
This is the user's explicit change to the earlier blanket refusal of such
imports; it is not a general undefined-call fallback.

No runtime may infer successful application acceptance from a complete catalog.
No catalog entry may be called implemented merely because a generic placeholder
returns success. Ordinary missing functionality that makes sense locally must
be implemented, not moved into the denial category to finish the inventory.

## Four distinct contracts

1. **Real local semantics:** arithmetic, data conversion, matching, local process
   state and other meaningful operations use actual owned data and native
   behavior. Preserve BEAM wrappers where appropriate; do not copy recursive
   intrinsic/NIF documentation stubs as executable implementations.
2. **Truthful queries:** report real Isolate state. Empty collections are justified
   by absent resources, not by an unimplemented query. Validate native options;
   a selector without truthful backing is explicitly unsupported, not guessed.
3. **Capability denial:** unavailable host/distributed effects resolve to reviewed
   entries but fail uncatchably before prohibited effects. Exact classifications
   and any mixed local/remote cases belong in the per-MFA matrix.
4. **Authorized exception:** load_module/2 may report that an already-published
   module is present, without installing/replacing any supplied code. This is a
   documented no-op exception, not permission to implement other successful stubs.

Distribution, host NIF execution, host ports/control, ambient filesystem/network
access and runtime code installation remain unavailable. Local variants of an
operation are not automatically denied because a remote variant exists. Pure
port/PID/reference text conversion or type inspection is not itself host I/O.
Likewise nif_error/1 must not be confused with load_nif/2: a guest error producer
and a prohibited capability have different failure contracts.

## Denial propagation and ownership

A capability failure needs an explicit identity through process, task, invocation,
World and public-host reporting, distinct from guest exceptions, unsupported
bytecode, work exhaustion, memory quota, callback OOM and cancellation. Record
the denied MFA and a bounded capability/reason identifier; do not copy arbitrary
guest arguments into diagnostics. Reporting must survive code/process teardown.
Prefer fixed owned diagnostic data over allocating a guest term on the failure
path. Public message formatting is not guest execution.

Charge the bounded entry cost. If that charge fails first, preserve the work
failure instead of overwriting its cause. A whole-MFA denial need not execute
upstream wrapper side effects or attempt the operation before refusing it.
Mixed local/unsupported modes require explicit selector validation and error
precedence; malformed supported input remains a native guest error where specified.

Every dispatch form must check infrastructure/capability outcomes before guest
exception, guard-failure or error-handler paths. Catch/rescue/try and process
monitors must not allow an active owning invocation to turn a descendant's
capability denial into host success. Correct catch/unwind support is therefore a
prerequisite for meaningful uncatchability tests: rejecting the test module at
load time does not prove this boundary.

Use the established causal ownership rules, including the distinction between
execution and borrowed message-copy work; do not silently redefine attribution.
Failure must reach the active owning invocation even when the offending process
is a child and the root is waiting. Stop affected execution before further
prohibited effects. Private, contained work may be reclaimed while retaining a
reusable World only when justified by the existing policy. Escaped effects or
persistent work require explicit World termination when containment cannot be
proved. A later background denial cannot retroactively retract an already
consumed response; its World/remaining-call outcome must be explicit. No peer
Isolate or host process may be terminated, and no guest-state rollback is promised.

## load_module/2 compatibility exception

After charging entry work, require an atom module and a byte-aligned binary code
argument; wrong types produce guest badarg regardless of module presence. Then
perform charged lookup of actual published state. For an existing module return
the native-shaped `{module, Module}`. Arbitrary binary payload bytes are ignored:
this exception does not assert that the supplied binary is valid BEAM. No supplied
bytes are installed or represented
as having been installed. Exports, code identity, imports, leases, on_load state
and published namespace remain unchanged. Result construction still obeys GC,
work and allocation failure rules; a no-op is not an allocation guarantee.

A valid request for an absent module fails at the host capability boundary. It
must not intern names from supplied BEAM data, prepare hidden code or invoke any
loader. Existing module presence comes from real published state, not a filename
or a forged module_info result. The draft exact-MFA contracts are recorded in
[the inventory](../../libbeam/core/otp/bif-batch-contracts.json); implementation
and source/mode review remain open.

## Implementation and test gates

- Account for all 185 exact MFAs, including native exports, intrinsic instruction
  forms, protected preloaded wrappers and required internal dependencies.
- Keep original-source provenance distinct from the location of an adapter or
  capability boundary. A wrapper is not automatically an absent native BIF.
- Specify ownership, roots/code retention, work and memory accounting, state
  publication order, supported modes and failure categories for each entry.
- Meaningful introspection requires actual counters/state or additional
  instrumentation, not invented zeroes or host-global data masquerading as local.
- Exercise direct, dynamic and applicable compiler-lowered forms, guard/body
  errors, moving GC, allocation/quota/work prefixes, no effects on denial,
  active-invocation/descendant propagation, explicit termination/reuse and peer
  survival with exact reclamation.
- Test an ordinary catchable error alongside a denied operation in admitted
  catch/rescue code. The former must be catchable; the latter must fail at the host.
- Complete and audit the whole batch before returning to application-driven
  retries. Run fresh full locked Debug/Release/available UBSan and existing
  lifecycle/two-Isolate gates; preserve production defaults and transport limits.

## Alternatives and remaining work

Strict rejection of every unavailable capability was the earlier policy and
remains valid for unclassified imports, but the user explicitly chose catalogued
host-denied entries for this batch. Catchable exceptions for prohibited operations
were considered and explicitly rejected. Silently successful placeholders,
namespace aliases and generic missing-import traps are not alternatives here.

The per-MFA draft matrix covers all 185 names without omissions or duplicates.
Detailed source/mode review, native/preloaded dependency mapping, selector policies,
diagnostics representation and lifecycle implementation/tests are unfinished. This RFD is not a completion claim or a grant of additional host,
network, distribution, NIF, tracing or I/O bindings. Local tracing and pure
match-spec behavior must be assessed on their actual local semantics rather than
being dismissed merely because upstream also uses them for broader facilities.
