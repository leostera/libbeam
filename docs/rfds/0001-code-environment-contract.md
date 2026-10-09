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

# Code-environment semantic contract for the W2 prototype

**These are design decisions, not implemented private-code support.** The
[source investigation and executable witness](0001-code-environment-spike.md)
currently demonstrate shared global code. W2-08–11, production feasibility,
performance and W7 compatibility remain open. These decisions complete semantic
specification work, not the implementation or security gates.

## W2-01: identity, ownership and lifetime

A code environment has a distinct opaque VM-lifetime identity, not a Realm ID,
module prefix, deployment name or code index. Identity is not loading authority.
The initial private profile binds each Realm immutably to one private environment;
an environment may be prepared before attachment but is not concurrently attached
to unrelated Realms. The host keeps a separate host environment. Ordinary spawn
inherits the Realm/environment; a process cannot switch either by supplying a
handle or identity. A future sharing API is not implicitly supported.

An environment owns module namespaces, exports/import bindings, code versions,
fun entries, loader transactions and associated literals/metadata. Its charged
owner is fixed on admission. Realm close stops new loading/admission; process
exit alone does not destroy code still referenced by stacks, funs, literals,
tracing, dirty jobs, native resources or callbacks. Prepared/unattached code is
charged to its authorized preparer's account. Attachment reserves destination
capacity before transferring charges; failure exposes no executable partial view.

Shared platform code and immutable artifact bytes may be shared through explicit
bounded pools. Private namespaces are not shared merely because bytecode hashes
match. The first prototype may use separately relocated private images and
per-environment module/export/fun tables, retaining VM-global publication epochs
for consistency. This is a candidate to measure, not a production memory-efficiency
claim. Never allocate tenants the existing current/staging code indices.

## W2-03: resolution and exception semantics

| Operation | Required resolution |
| --- | --- |
| Local call/tail call | Same loaded module instance and environment as the executing frame; no lookup into another tenant's module |
| Static external call/import | Environment-bound export selected when linking the private instance; reserved platform imports bind to immutable platform exports |
| Dynamic module/function call and `apply` | Resolve in the executing process's bound environment, with declared platform fallback only |
| BIF/intrinsic/optimized guard | Same language primitive across environments; actual caller Realm governs effects/charging, not the importer's or module owner's authority |
| External fun creation | Bind to that environment's logical export (or an explicitly neutral immutable platform export); see lifetime rules below |
| Root MFA | Resolve only in the destination Realm's environment after authorized startup; do not resolve in the host then transplant an instruction pointer |
| Undefined function/error handler | Invoke the environment-local handler/loader policy; never consult a tenant-writable global code path or silently load into the host |
| Callback from platform code | Resolve the callback in the current process's environment; platform internals keep their sealed platform imports |
| Callback/native work with no live process | Require retained, validated Realm/environment provenance and admission; absent context is not host context |

One operation requiring a coherent code view takes one publication-generation
snapshot. Publication must preserve the existing communication/thread-progress
visibility ordering on both JITs and the interpreter. Caches and inline fast
paths must distinguish environment and generation as needed; a stale foreign
export pointer is not repaired by checking only module names in `code_server`.

Stack traces retain ordinary logical module/function/arity and source locations.
Internal frame/range metadata must identify the owning environment/version so
lookup, diagnostics, breakpoints and purge cannot confuse same-named instances.
Tenant traces do not expose foreign environments or host locals. Missing local
exports retain `undef`; invalid/mismatched fun invocation uses `badfun` (and
ordinary arity mismatch uses `badarity`) without executing foreign code.

## W2-04: immutable platform code and deployment code

The platform manifest reserves its module names, versions, allowed callbacks and
native-effect policy. Tenant loading cannot shadow, replace, purge or instrument
these modules or another environment. Platform code may execute in a tenant
process without acquiring host authority. Its fixed internal dependencies stay
platform-bound; dynamic tenant callback operands resolve through the caller's
environment. Loading the same library in a host process does not authorize a
tenant's invocation of an already-loaded NIF or OS effect.

Private code owns a namespace with unchanged logical module names. Identical BEAM
artifacts can be retained once in a bounded shared artifact pool, but mutable
export bindings, versions, breakpoints and loader state remain private. Machine
code containing patched environment-specific pointers cannot simply be reused
in a second environment. Either relocate separate instances or introduce proven,
measured environment-relative imports. W2-10 decides whether the resulting
memory/call cost is acceptable; no module-name rewriting substitute is allowed.

## W2-05: fun binding and transfer

- Local fun identity includes its environment/module version, lambda identity and
  captured values. Equal-looking code/captures in two private environments do not
  make the funs equal. Funs pin the required version until purged under the normal
  soft/hard-purge rules; they do not follow a later replacement's local body.
- Private external funs bind an environment plus logical MFA and follow that
  environment's current export, not a same-named export in whichever Realm calls
  them. Their equality includes the environment. Neutral external funs are limited
  to manifest-declared immutable platform exports with no captured tenant state.
- Invoking an environment-bound fun from a different environment rejects before
  running code, including when a privileged host retains the fun. No implicit
  environment switch and no authority borrowed from the fun's creator.
- Root-start arguments are recursively checked: a foreign environment-bound fun,
  including a closure supplied by a trusted host fixture, is not an executable
  bootstrap shortcut. Use a destination MFA and validated data. Neutral platform
  external funs still confer no effect authority.
- Local fun serialization, if supported by the implementation, must encode and
  validate VM-lifetime environment/version binding; an unscoped encoding must not
  silently rebind a private fun. Cross-environment/process-external serialization
  of private funs is unsupported in the first profile. Encoded descriptors do not
  resurrect destroyed environments or grant authority. Until a scoped codec is
  implemented, private-fun encoding/decoding must fail closed with `badarg`.
- Fun inspection returns logical metadata for an authorized/local fun; raw native
  addresses, foreign captures and environment internals are not public metadata.

Fun terms and their serialized forms need explicit recursive, bounded validation
and charging. Checking only top-level root arguments or direct fun calls would
leave nested terms, decoder paths and callbacks uncovered. Legacy host-only fun
semantics need not change when no private environment is involved.

## W2-06: load transactions and initialization

Preparation validates the artifact, reserved-module rules, ownership, limits and
all retained references before staging. A prepared handle carries immutable owner,
environment and transaction generation; caller possession alone cannot publish it.
Direct/internal loader BIFs must enforce the same ownership as the public loader.
Tenant calls cannot mutate global host or platform code even if `code_server` is
bypassed. Global atom allocation during parsing needs its own bounded admission;
rollback cannot pretend to remove already-interned atoms.

Allow concurrent preparation within explicit budgets. Serialize finishing per
environment and preserve required VM-wide publication locks/barriers. Do not hold
a Realm/accounting lock across Erlang initialization, callbacks or blocking work.
Readers see a committed old or new view, never a partially linked namespace.
Failed/conflicting/stale finishing releases uncommitted reservations exactly once.

`on_load` executes with the destination Realm/environment and an attenuated
initialization phase, never host authority. Its private staging visibility must
not leak executable uncommitted entry points to ordinary readers. The eventual
implementation must explicitly handle recursive loads, local calls, spawned
children, captured funs and callbacks during initialization. Invalidated staged
entries cannot execute after failed publication; retained failed-image storage
cannot be freed until dependent references are safely handled.

Code publication is transactional; arbitrary application-side mutation is **not**.
Initialization cannot issue external host effects before activation commit. For a
fresh failed activation, stop the prepared Realm and drain/cancel all owned work
before declaring cleanup complete. During upgrades, local state/messages already
committed by permitted initialization are not magically rolled back; application
compensation is required. This distinction must be documented and tested rather
than claiming rollback of external effects or suppressing remaining retained work.

## W2-07: upgrades, purge and destruction

Each private module/environment has independent current/old versions. External
calls follow committed current exports; existing local stacks and funs retain
their version. A third conflicting version requires the appropriate old-version
purge; it cannot steal another environment's version slot. Live code replacement
never changes process Realm/environment membership or effect authority.

Soft purge must fail while any relevant local stack/fun/native reference still
requires the version. Hard purge is an authorized environment-management action;
it may terminate that environment's affected processes under documented OTP-like
semantics, but cannot kill another Realm's same-named module users. Pending fun
purge, code-presence checks, saved continuations, JIT addresses, breakpoints and
literal-copy work must all carry correct ownership and retain necessary lifetime.

Code ranges and catches may use globally allocated address/slot directories for
performance, provided every entry retains exact owner/version and no lookup or
reclamation conflates logical module names. Literal areas and shared artifact
pools need separate reference/charge release points. Already-loaded native code,
resource destructors and dirty callbacks can delay physical release; no metadata
free or quota refund occurs merely because an upgrade or logical stop returned.

Environment destruction closes admission/loading, invalidates uncommitted work,
stops/cancels execution and owned resources, removes namespace visibility, waits
for safe thread-progress/native release, then reclaims metadata and charges.
These are lifecycle phases, not a promise of bounded completion with arbitrary
native code. Stalls remain observable to an authorized controller and cannot be
reported as successful physical reclamation.

## Required evidence before implementation acceptance

Same-MFA `a`/`b` resolution must hold concurrently across static/import, dynamic,
external/local fun, root, callback, undefined-function and exception paths on both
JITs and interpreter. Test concurrent loads, rejected global mutation, failing
`on_load`, leaked prepared handles, nested startup funs, old code on stacks,
soft/hard purge, native/dirty retention and repeated environment destruction.
Run host-only compatibility and measure lookup/relocation/metadata costs.

The witness currently returning `b` in both Realms is intentionally **not** this
proof. Unresolved implementation representation/performance and complete source
mapping remain W2-02/08–11 blockers, not grounds to weaken these semantics.
