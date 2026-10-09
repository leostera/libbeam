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

# Initial process-operation policy

This is an implementation contract for the first process-boundary batch in
[RFD 0001](0001-beam-realms.md), complementing the
[Realm/endpoint API contract](0001-api-contract.md). It is **not** the complete
BIF/native inventory or a safe profile for untrusted workloads.

## Activation and authority

```erlang
Realm = erts_internal:realm_create(#{restrict_process_access => true}).
```

The flag is immutable. Top-level creation currently defaults to `false` to keep
legacy trusted-fixture behavior available during migration. New child Realms
inherit the flag unless explicitly made more restrictive; a restricted creator
cannot request an unrestricted child. Ordinary processes always inherit their
current Realm and policy. Setting this flag is not completion of M2 or M3.

For covered local data sends:

1. Same-Realm delivery retains ordinary semantics.
2. If either endpoint Realm is restricted, foreign delivery raises `badarg`.
3. If neither is restricted, legacy behavior remains.
4. Host and ancestor management authority is **not** an exception for ordinary
   message delivery. Use an explicitly granted endpoint.

For covered process-control/inspection operations, the same rule applies with a
management exception: the host and a creation-enabled strict ancestor may operate
on their managed processes. This supports existing Realm shutdown, monitors, and
system-task forwarding. It is execution-context authority, not authority inferred
from a PID or module name. W1-12 now explicitly retains these narrow ordinary
control/inspection exceptions; see the [adopted decisions](0001-contract-decisions.md).
This does not extend management exceptions to ordinary data sends or unreviewed
resource/native APIs.

Missing current-incarnation local PIDs retain each operation's ordinary OTP
behavior. External PID encodings, including old incarnations, are unsupported
for restricted callers rather than covered by that absence rule. In particular,
a send to a dead PID can still succeed without delivery and monitoring a missing
process can still produce `noproc`. The contract does not hide every existence
side channel: a known live foreign target can produce `badarg` where a missing
target has ordinary absence semantics.

## Implemented operation matrix

| Operation | Restricted-profile behavior | Enforcement point / notes |
| --- | --- | --- |
| `!`, `send/2`, `send/3`, `send_nosuspend` to a local PID | Same Realm allowed; foreign delivery denied if either Realm is restricted | `bif.c:do_send()`, before message enqueue |
| Alias and priority sends | Same rule, resolving the alias owner rather than treating possession of a reference as authority | Check before alternative-action signal construction; denied traffic does not consume reply-demonitor aliases |
| Atom and `{Name, node()}` destinations | Check the actually resolved process before delivery | This is not a Realm-local registry; host-provisioned same-Realm names work |
| Remote PID/reference and remote-name sends | Denied for restricted callers, before connection work | Incoming distribution is not covered by this check |
| Sends to ports through `send/2,3` | Denied for restricted callers | This does not mediate `port_command`, all port APIs, drivers, or NIF effects |
| `link/1,2`, `unlink/1` | Same Realm or authorized management scope | Check before link state creation/mutation; internal exit/unlink cleanup remains unchanged |
| `monitor/2,3` for local processes, including names | Same Realm or authorized management scope | Check after name resolution and before installing monitors; remote/port/time-offset monitor requests are denied for restricted callers |
| `demonitor` | Existing owner-local monitor semantics | A foreign reference does not install a monitor in the caller's monitor tree; cleanup is not blanket-filtered |
| `exit/2,3`, `exit_signal/2,3` | Same Realm or authorized management scope | Both deprecated and current entry points, including priority options |
| `process_info/1,2`, `is_process_alive/1` | Same Realm or authorized management scope | Before inspection or request construction |
| Internal `is_process_alive/2`, `is_system_process/1`, `is_process_executing_dirty/1` | Same rule | Direct internal exports are not a bypass |
| `process_flag/3`, internal backend, `process_display/2` | Same Realm or authorized management scope | Local process flags do not change Realm membership/policy |
| `group_leader/2` and internal `/2,3` backends | Both leader and target must be in allowed control scope | Internal `/2` returns its existing `badarg` atom; public wrapper and asynchronous `/3` raise `badarg` |
| Suspend/resume, including internal suspension entry | Same Realm or authorized management scope | Check before installing suspension state |
| GC and code-presence system tasks | Target and explicit reply destination must be in allowed management scope | Covers `request_system_task/3,4`; restricted callers must use an internal PID requester. No unrelated foreign reply channel is created |
| Global process/port/registration enumeration | Denied for restricted callers | Includes direct `processes_next/1`; no final-list filtering or partial foreign listing |
| `register`, `unregister`, `whereis` | Denied for restricted callers | Temporary fail-closed behavior, not implemented scoped namespaces |
| `send_after`, `start_timer`, `read_timer`, `cancel_timer`, all exposed arities/options | Denied for restricted callers | Before timer creation/cancellation or asynchronous replies. `receive after` still works |
| Atomics and both counter backends, including internal counter BIFs | Actual caller must equal the retained owner Realm; applies to legacy profiles too | All value and info operations check before effects; no host/manager exception. Creator exit does not revoke same-Realm use |
| `persistent_term:get/0,1,2`, `put/2`, `put_new/2`, `erase/1`, `info/0` | Denied for restricted callers | Temporary profile denial before lookup/scans/publication, not a private namespace |
| `erts_internal:erase_persistent_terms/0` | Host-only, including denial for legacy non-host callers | Before global update permission or mutation |
| Trace setup/patterns/session management/info/delivery barriers | Denied for restricted callers | Public and current internal entry points; permissions checked before code-modification/session work |
| Sequential trace mutation/output, system monitoring/profiling, global `system_flag` | Denied for restricted callers | Own sequential-token introspection is not globally denied |
| Node enumeration/monitoring, `setnode`, channel creation, remote spawn, distribution-controller APIs | Denied for restricted callers | Includes indirect node monitoring through `process_flag` and direct internal entry points |

Malformed arguments retain ordinary validation except that unsupported operations
can reject immediately. Denials are synchronous `badarg` errors, except for an
internal backend's documented status convention. Existing spawn admission errors
remain `system_limit` (or the normal asynchronous spawn error reply). Policy
attenuation errors remain `badarg`.

## Placement and cleanup invariants

Checks live at operation-specific user entry points, not indiscriminately in PID
lookup, message queues, or exit-signal delivery. Core monitor replies, link exits,
deferred cleanup, and scheduler-owned work must retain their authorization paths.
Target membership is immutable and initialized before publication, so managed
scheduler lookups can check it without taking a Realm mutex.

System-task effects and their explicit reply destinations are both checked. Host
system-task forwarders remain permitted to service restricted requesters. Dirty
signal handling, dirty code checking, and literal-area copying already restrict
callers to specific VM housekeeping processes; those checks remain intact.
`erts_internal` naming by itself never grants permission.

Ordinary data-send checks precede destination queue insertion. Existing tracing
can still observe attempted sends; trace/diagnostic side effects are not promised
to disappear on denial. Alias activity and normal receiver exit races are still
handled by the existing signal machinery after authorization.

## Fixture migration and evidence

`erts/emulator/test/realm_process_SUITE.erl` uses prepared startup and endpoint
reporting throughout. It covers policy inheritance/attenuation, local PID/alias/
priority messaging on both queue modes, ordinary controls and cleanup, denied
foreign sends from both directions, monitor aliases that remain active after
rejection, direct internal control entry points, unrelated system-task reply
rejection, unsupported global operations, and dying-process races.

Both suites pass together (55 cases) on optimized JIT, debug JIT, and debug
interpreter on ARM64 macOS. The [validation record](0001-implementation.md) also
records 84 focused debug regressions. The separate historical C-node failure was
subsequently isolated and repaired in its fixture; see the
[W1 validation evidence](0001-validation.md), not a claim of full regression clearance.
The [native discovery ledger](0001-native-inventory.md) now accounts for 570 declared
BIF dispatch entries and lists remaining surface families. All entries still need
complete per-operation review; a matching inventory is not an allowlist.

The older suite's prepared-bootstrap case now opts into restrictions. Most older
lifecycle fixtures still deliberately use unrestricted Realms and ordinary
cross-Realm messages. **Full fixture migration is not complete**, and restrictions
are not the default. Changing the default requires that migration and additional
compatibility/bypass tests; it must not be described as already done.

## Source-bound design reviews

The [operation review registry](0001-operation-reviews.tsv) now specifies initial
dispositions for 37 BIF/NIF API entries, with source dependencies and unresolved
review/tests. The [original negative witnesses](0001-next-five-evidence.md) showed
foreign atomics, global persistent-term and host-timer bypasses. The subsequent
[shared-resource implementation](0001-shared-resource-enforcement.md) enforces
array ownership and persistent-term profile denial; timer ingress remains open.
Resource admission/revocation and aggregate byte/CPU accounting are not implemented. The declared inventories remain unapproved until
complete semantic, runtime and independent review.

## Explicit gaps before M2/M3 acceptance

The [execution checklist](0001-work-plan.md) tracks closure of these gaps through
W1's inventory and W3–W6's enforcement, ownership, and accounting tasks.

- Realm-local registration, enumeration, timers, and tracing are not implemented;
  denying them is a temporary restricted-fixture profile, not OTP compatibility.
- An unrestricted caller's existing named timers and other deferred work do not
  yet retain and enforce Realm context at delivery. These can bypass the ordinary
  send entry points. Timer ownership and name-rebinding races remain work.
- Incoming distribution is not isolated, and the host can still activate a node.
  A globally enforced non-distributed runtime profile and activation races remain
  required before tenant deployment.
- Already-loaded NIFs, drivers, debug facilities, ports, OS effects, and shared
  ETS/persistent/code state are not generally mediated. Their alternate send or
  inspection paths are outside this initial entry-point contract.
- This is not a complete audit of every native/internal operation. Additional
  wrappers, asynchronous work, and resource APIs need inventory and adversarial
  tests. Disallowing NIF loading alone would not close already-loaded effects.
- The legacy `false` profile is trusted-fixture-only. Neither it nor the opt-in
  profile is a sandbox. Memory/CPU accounting, independent code environments,
  broader regression/performance/platform coverage, and security review remain.

Accordingly, this batch advances the first four planned work areas, but does not
claim their complete acceptance or a completed process-compartment milestone.
