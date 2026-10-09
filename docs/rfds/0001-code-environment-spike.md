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

# W2: private code environment investigation

**Status: investigation and semantic contracts, not private-code acceptance.**
[W2-01/03–07 semantics](0001-code-environment-contract.md) are specified; the full
source map, prototype, measurements, reviewed feasibility and W2-G remain open.
No private loader,
independent module namespace, or feasibility approval is implemented here. See the
[work plan](0001-work-plan.md) and [native discovery ledger](0001-native-inventory.md).

## Existing lookup boundaries

These inspected source paths establish why separate code-server processes alone
cannot implement environment-aware module resolution:

| Path | Current mechanism | Required investigation |
| --- | --- | --- |
| `erts/emulator/beam/module.c`: `module_tables`, `module_hash`, `module_cmp`, `erts_get_module` | Global tables indexed by code index; key/comparison uses the module atom, not a Realm/environment | Environment ownership and table keys; distinguish publication indices from deployment namespaces |
| `erts/emulator/beam/export.c`: `erts_find_export_entry`, `erts_find_function`, `erts_export_put` | Export templates contain module/function/arity; lookup takes a code index, with no caller environment | Environment-aware resolution and import binding; shared platform fallback must be explicit |
| `erts/emulator/beam/code_ix.h`: `erts_active_code_ix`, `erts_staging_code_ix` | Reads VM-global atomic code indices | A code index is not an environment identity; preserve publication/thread-progress guarantees rather than assigning tenants indices |
| `erts/emulator/beam/jit/arm/instr_call.cpp`: `emit_i_call_ext`, `emit_i_call_ext_only` | Emitted external calls use an `ArgExport` and a dispatchable-call target | Determine where imported export pointers become environment-specific and which compiled code can remain shared |
| `erts/emulator/beam/jit/x86/instr_call.cpp`: corresponding external-call emitters | Also dispatches through export operands | Both JITs need proof; fixing only dynamic `apply` would leave direct calls unresolved |
| `erts/emulator/beam/beam_common.c`: `call_fun` | Fun dispatch addresses are selected using the active code index; captured values are copied from the fun environment | Separate code-environment ownership from captured Erlang values; preserve old-code lifetime, invocation semantics, and purge safety |

This is not the complete W2-02 map. Interpreter instructions, loader import
patching, literal/catch ownership, on-load execution, BIF exports, native code,
callback resolution, tracing/breakpoints, dirty work, and reclamation still need
explicit paths and invariants. No lookup-overhead estimate has been measured.

## Executable shared-namespace witness

[`scripts/realm_code_probe.erl`](../../beam/scripts/realm_code_probe.erl) is a diagnostic
for the existing implementation, **not a passing private-environment test**.
It uses two restricted Realms with host-bound byte endpoints and no cross-Realm
ordinary messages. The host loads two versions of the same unchanged module name.

The diagnostic expects the current global-code behavior:

1. Both roots call the first version through a static external call, dynamic
   `apply`, an external fun, and a retained local fun: all return `a`.
2. The host loads the second version. Both roots now return `b` through static,
   dynamic, and external-fun calls. Both retained local funs still return `a`.
3. The result explicitly reports `shared_code_only` and
   `private_environment_acceptance => not_met`.

Keeping an old local fun alive is version retention, **not** evidence that two
Realms resolve the same module name independently. This witness intentionally
makes that distinction visible. It neither renames the subject module per tenant
nor claims two global current/old versions are private environments.

Run only in a disposable VM, serially with other OTP validation jobs:

```sh
mkdir -p /tmp/realm-code-probe
bin/erlc -o /tmp/realm-code-probe scripts/realm_code_probe.erl
bin/erl -emu_type debug -emu_flavor jit -noshell -pa /tmp/realm-code-probe \
  -eval 'io:format("~p~n",[realm_code_probe:run()]),halt().'
```

The diagnostic checks that its subject module is initially unloaded, stops both
roots in cleanup, then purges/deletes its subject. Endpoint reads have a deadline.
Unexpected observations fail; success only confirms the shared-namespace baseline.
Compilation with the checkout's `bin/erlc` succeeds, using a private `/tmp` output
directory without changing the running test tree. `beam_disasm:file/1` confirms a
real `apply` instruction in `apply_subject/2` and two fun-call instructions in
`loop/4`. An initial constant-operand attempt was optimized to a static call even
with `no_inline`; operands now come from endpoint decoding to preserve the path
under investigation. The witness executed successfully on optimized JIT, debug
JIT and debug interpreter, reporting `shared_code_only` in each case. Both Realms
observed `b` for static/dynamic/external-fun calls and retained `a` for local funs
following replacement. Evidence: `/tmp/beam-realms-w2/code-probe/summary.json` and
per-variant logs. These results confirm the missing private namespace, not W2-G.

## Next implementation checkpoint

Define environment identity and lifetime independently of Realm identity, then
trace import/export creation and every call path before selecting a prototype
representation. The actual W2-08–09 prototype must demonstrate concurrent `a`/`b`
resolution for the **same MFA**, including static calls, dynamic calls, funs and
callbacks, with correct failure/initialization/purge behavior. This diagnostic
cannot satisfy that gate. Do not enable an untrusted profile or commit to a
production loader architecture based on the current source map alone.
