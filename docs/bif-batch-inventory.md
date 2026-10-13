<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 Leandro Ostera <leandro@ostera.io> -->

# Coordinated BIF inventory — implementation contracts assigned

Workspace checkpoint: `cebaa7f6`. No production BIF behavior has been changed by
this inventory work. All 185 requested MFAs have explicit implementation
contracts, source provenance candidates and native-versus-BEAM plans. This
completes the planning inventory, not implementation or executable coverage.

The authoritative [requested MFA list](../libbeam/core/otp/bif-batch-request.txt)
contains **185 distinct erlang name/arity pairs**, matching the user's list.
This is a requirement list, not an admission allowlist or an implementation claim.

Initial comparison against the pinned `beam/erts/emulator/beam/bif.tab` finds
**135 direct native declarations** and **50 entries without a direct erlang BIF
declaration**. The latter require inspection of preloaded Erlang wrappers,
intrinsics and underlying `erts_internal` dependencies; they must not be treated
as missing C functions automatically. For example, apply/2 is an intrinsic,
while processes_iterator/0 and processes_next/1 have preloaded Erlang bodies.
The current protected preloaded subset is not the full upstream erlang module.

## Decisions already settled by the revised goal

- Strict exact-name imports still resolve transactionally before publication.
- A specifically catalogued capability denial is a real resolved entry whose
  invocation fails at the host boundary, uncatchably by guest code. It is not
  permission to accept arbitrary unresolved imports or unsupported instructions.
- Real meaningful Isolate-local functionality must not be denied merely because
  implementation is difficult. Native OTP behavior remains in C adaptations;
  existing BEAM implementations should remain BEAM where appropriate.
- Distribution, host NIF execution, host ports/control, ambient filesystem/network
  access and runtime code installation/replacement remain unavailable. Queries
  must describe actual owned state, including genuinely empty collections.
- load_module/2 has an explicit already-loaded no-op exception, not permission to
  install supplied bytes, change exports, run on_load or load an absent module.
- Each MFA needs provenance, argument/selector semantics, code/GC ownership,
  memory/work accounting, direct/dynamic/compiler-form coverage, failure behavior
  and executable evidence. Registered denial does not count as successful native
  capability implementation.

## Machine-auditable implementation contracts

[Contracts](../libbeam/core/otp/bif-batch-contracts.json) assign every MFA
exactly once to 30 behavior groups, with shared ownership, accounting, error,
invocation-path and test requirements. These are implementation
contracts, not claims that the functions work. Detailed native body/mode review
and executable tests remain mandatory during implementation.

`python3 -B libbeam/tools/audit_bif_batch.py` joins those contracts with pinned
native declarations, C source candidates (including hashes), preloaded function
head candidates and current native-ID registration facts. It rejects missing,
extra or duplicated contracts. Head candidates deliberately do not prove arity,
actual executable bodies or supported behavior. Every row is explicitly contract-assigned with implementation review pending,
and has no executable implementation evidence until supplied.

[Policy RFD 0005](rfds/0005-isolate-bif-catalog.md) specifies failure propagation,
including why a denied child operation cannot merely emit a catchable DOWN and
let the owning active invocation return success. It also separates contained
invocation failure from unsafe escaped/background work requiring termination.

The [wrapper plan](../libbeam/core/otp/bif-batch-wrapper-plan.json) accounts for all
50 non-native declarations. [Wrapper/compiler prerequisites](bif-batch-wrapper-prerequisites.md)
record 45 required complete BEAM functions, additional exact native/internal
dependencies, 401 compiler witnesses and verified query/configuration details.

## Implementation work still required

Implement the per-MFA contracts and native prerequisites, finish detailed
source/mode review, implement host-denial reporting/propagation and lifecycle
rules, and turn each class's test contract into executable acceptance evidence. Ordinary badarg/guest exceptions remain
separate from the new uncatchable capability outcome. Genuine runtime interfaces
such as predicates, pure encoders/hashes, process-local state and local scheduling
must not be confused with enabling host or distributed effects.

Do not retry hello_worker after each added function. Complete and audit the full
BIF pass, run fresh integrated validation, then return to application admission
and execution. The current application acceptance target and independent wire
reference tests remain unchanged.
