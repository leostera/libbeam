<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 Leandro Ostera <leandro@ostera.io> -->

# BIF batch: wrapper and compiler prerequisites

This is an implementation inventory, not executable coverage. It complements
[the exact-MFA contracts](../libbeam/core/otp/bif-batch-contracts.json),
[the 50-entry wrapper plan](../libbeam/core/otp/bif-batch-wrapper-plan.json) and
[RFD 0005](rfds/0005-isolate-bif-catalog.md).

## Source and compiler evidence

Trusted OTP 28 / ERTS 16.3 successfully parsed all **403** functions in the pinned
preloaded `erlang.erl`, including every requested overload. AST inspection records
exact arities, static calls, guard calls, static fun references, operators and
unresolved dynamic-call sites. It does not execute those functions. The 50
requested functions without direct native declarations comprise:

- 27 complete BEAM wrappers;
- 3 intrinsics (`apply/2`, `hibernate/0`, `yield/0`), whose source self-calls must
  not be mistaken for ordinary executable recursion;
- 4 owned-query adapters (`disconnect_node/1`, `get_cookie/0`, `memory/0,1`);
- 2 native local tracing adapters, rejecting unavailable module/NIF tracers before
  the upstream wrapper's tracer-module autoload;
- 13 whole-MFA capability boundaries;
- the authorized `load_module/2` exception.

The other 135 requested MFAs have pinned native declarations and C source
candidates. Native candidates are provenance, not proof of a completed adaptation.
The query/tracing adapters are explicit scope adaptations, not fabricated auth,
net_kernel, allocator or code-server modules. No application/dependency function
is deleted or renamed to accommodate them.

`audit_bif_lowering.escript` compiles but never executes representative calls.
The current compiler produced **401 fixtures**: 185 body calls, 185 dynamic
calls, 21 guard-result comparisons, 9 predicate-test guards and a send operator.
Examples include `gc_bif` for unary plus/ceil, ordinary `bif` for PID/tuple tests,
`call_ext_only` for generic apply/hibernate/yield/send and the `send` opcode for
`!`. These are minimum concrete witnesses, not proof of every possible compiler
optimization or of libbeam execution. Implementation tests must exercise them
and relevant additional forms, rather than treating a native export as sufficient.

## Preserve complete wrapper closure

The 27 BEAM roots reach **45 complete Erlang functions** in the pinned erlang
source. Besides those roots, retain these 18 helpers/prerequisites:

`badarg_with_cause/2`, `badarg_with_info/1`, `big_binary_to_int/2`, `combine/2`,
`combine_pairs/2`, `digits_per_small/2`, `error_with_info/2`,
`error_with_inherited_info/3`, `fun_info_1/3`, `garbage_collect/2`, `get_gc_opts/2`,
`get_sign/1`, `radix/2`, `segmentize/4`, `segmentize_1/4`, `spawn/1`,
`spawn_request/4`, `trim_zeroes/1`.

Preserve required source record/macro declarations, notably `gcopt`; do not cut
function clauses, catches or remote branches. The existing protected
`spawn_monitor/1` remains present too. All native calls in this inspected closure
are explicitly module-qualified; do not silently reinterpret ambiguous local
calls as BIFs in future generator changes.

The source closure reaches 39 static native/intrinsic/remote dependencies. Of
these, 23 are erlang BIFs already existing or in the requested batch; the
additional exact native/internal prerequisites are:

| MFA | Contract |
| --- | --- |
| `erts_internal:binary_to_integer/2` | Real native parser/status protocol used by the full wrapper and bignum helper. |
| `erts_internal:list_to_integer/2` | Real native list parser/status/tail protocol, not a shortcut bypassing wrapper validation. |
| `erts_internal:dist_spawn_request/4` | Explicit uncatchable distribution denial; local-node wrappers still use real local paths. |
| `erts_internal:dynamic_node_name/0` | Owned immutable no-dynamic-naming query; explicit scope adapter. Upstream reads persistent_term; do not bootstrap global naming/storage services merely to answer this disabled-runtime query. |
| `erts_internal:garbage_collect/1` | Actual owned collector with native major/minor argument behavior. |
| `erts_internal:group_leader/2,3` | Real local group-leader mutation and required result/reply protocol, without starting an I/O service. |
| `erts_internal:perf_counter_unit/0` | Actual selected clock frequency/unit metadata. |
| `erts_internal:time_unit/0` | Actual native time unit used by the implementation. |
| `erts_internal:port_info/1,2` | Native validation and accurate absent/dead-port behavior in the port-free namespace. |
| `erts_internal:process_flag/3` | Actual owned target-process flag/result/reply protocol; explicit unsupported modes. |
| `erts_internal:processes_next/1` | Owned iterator state/cursor protocol, including lifetime, exhaustion and invalid-state behavior. |
| `erts_internal:request_system_task/3` | Real local GC request/completion required by garbage_collect/2, including dead targets and causal reply work. Review other selectors; never acknowledge work not performed. |
| `erts_internal:spawn_request/4` | Real local asynchronous spawn request/reply protocol with cancellation/abandonment and causal ownership. |
| `lists:keyfind/3` | Real native list/tuple search, native comparison/index/error semantics and charged traversal. |

Native registration must support these exact modules without pretending to
provide whole BEAM dependency modules. Preserve the actual `lists` and
`erts_internal` dependency BEAMs during application admission. Review native-BIF
binding precedence for external imports, local calls, funs and metadata against
OTP: executing an original NIF stub instead of its native BIF, or silently replacing
arbitrary ordinary guest functions, is not correct. Do not broaden import fallback.

## Verified query details and configuration difference

Source inspection plus an independent anonymous OTP reference confirms:

- No auth/distribution service: `get_cookie()` is `nocookie`;
  `disconnect_node(Anything)` is `ignored`, including a non-atom argument.
- `nodes()`/visible/hidden/connected are empty; known/this include
  `nonode@nohost`. Repeated filters do not duplicate the local entry.
- `nodes(this, #{})` returns `[{nonode@nohost, #{}}]`, not a bare atom list.
  Requested local metadata is `connection_id => undefined, node_type => this`.
  Unknown options and improper filter lists produce badarg.
- The installed OTP has probe-enabled `dt_*` behavior: spreading a boolean returns
  `{0,[]}`, and restoring `true` produces badarg. Libbeam's opcode manifest selects
  `USE_VM_PROBES=0`. The pinned disabled branches instead return true for boolean
  spread and true-only restore, rejecting other inputs. This is an explicit
  configuration difference; installed results are not a disabled-probe oracle.

## Verification boundary

The inventory tools pass eight Python source/contract tests and four locked
OTP AST/compiler tests. Primary source facts, ASTs, compiler listings and reference
observations are retained in the private research topic
`personal/libbeam/hello-worker-bif-inventory/`. They do not establish runtime BIF
support, full opcode coverage, catch/unwind correctness, whole-application wire
results or resource/lifecycle acceptance. Those remain implementation and batch
validation gates.
