<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 Leandro Ostera <leandro@ostera.io> -->

# BIF batch runtime progress

The coordinated 185-MFA implementation pass is **in progress**. This is not
whole-BIF acceptance and does not establish any hello_worker response bytes.
Do not return to application-driven retries until the complete batch is validated.

## First cluster: explicit capability outcomes

At checkpoint `f5956194`, the native catalog contained **83 existing local implementations plus 27
explicit denial entries**, and the separate apply/3 instruction export. The
27 exact names are in `libbeam/core/capability_denials.def` and checked against
the independent per-MFA contracts. They are not 27 newly implemented host effects.
Unknown imports still refuse admission. Denials perform no upstream wrapper or
prohibited operation, including for malformed arguments.

Each denial charges one primitive work unit, then records a bounded static
MFA/capability diagnostic without allocation or guest references. Exhaustion of
that charge remains a work failure. Process/task/World/public categories are
separate from exceptions, unsupported bytecode, quota, callback OOM and cancellation.
The C++ adapter reports, for example:

```text
ErrorCode::capability_denied
 erlang:load_nif/2 denied: NATIVE_LIBRARY
```

The shared worker retains the causal owner, records the first denial before
retiring the offender, suppresses guest DOWN publication, fails an active owning
call and applies the existing containment rule: private family cancellation or
World closure after escaped effects. Already-terminal responses are not rewritten.
Static diagnostics remain valid after guest process/code/World-storage teardown.

Native compilation required two distinct paths to be handled:

- Direct load_nif/2 lowers to `i_load_nif`. Its entire effectful operation is
  replaced by the same explicit boundary, before code-modification permission or
  library access. Dynamic calls use the native export.
- A module containing load_nif gets `nif_start` metadata, including after
  allocation/init prologues. Like native emu_load.c, the loader removes this
  marker after verifying a function header; it creates no register/heap facts.
  No installation/patch table is enabled. Actual call_nif, on_load and unsupported
  padding remain refused. This is native metadata treatment, not stripping a
  guest function or replacing a missing implementation with success.

The pinned naming inputs are unchanged. The owned set_cookie name is appended
without shifting existing atom indices; 13 denied wrapper arities receive explicit
extension IDs after the five existing conversion entries.

## Evidence for this cluster

`core_capability_test.c` verifies all 27 direct and 27 dynamic calls, each primitive's
zero/exact/extra work under exhausted quota and denied allocation, unchanged
heap/credit/atoms, monitored-child failure reaching its active root, private reuse,
escaped World closure, peer/replacement survival, static diagnostic lifetime and
final allocator balance. `api_capability_test.cpp` verifies the real C++ error,
MFA message, consume-once behavior, child propagation, reuse, peer and reclamation.

Fresh locked `/tmp/libbeam-capability-integrated-third/summary.json` reports
`passed=true`, **274 commands**, **110 Debug + 110 Release CTests**, **31 existing
+ 67 profile UBSan tests**, and the existing lifecycle/two-Isolate gates. All 393
recorded source inputs matched across validation. Defaults remain 256 MiB,
10,000,000 invocation work units, and a separate 64 KiB transport bound.

Earlier attempts found and fixed a provenance-hash transcription error and the
new C source missing from the standalone UBSan linker list; neither was reported
as a passing full gate.

## Second cluster: load_module/2 compatibility exception

The catalog adds one separately classified compatibility exception (111 entries
in total), not a loader implementation. Entry work precedes type checks; atom plus
byte-length binary is required even for existing modules, including unaligned
views. Charged lookup of real published code permits `{module, Module}` only for
an existing module. Supplied bytes are ignored, not parsed, installed or said to
have been installed. Valid absent requests report
`erlang:load_module/2 denied: CODE_LOADING` at the host boundary.

`core_load_module_test.c` exercises direct/input-supplied dynamic calls at every
execution work prefix, plus decode-supplied dynamic calls, for present, absent and malformed requests; actual preloaded and peer
state; arbitrary, empty and unaligned code; moving GC and retained heap credit;
quota versus allocator refusal; unchanged metadata/code/export bytes; unload and
reload; host-worker denial/reuse and zero final accounting. The compiled witness
also adds the **complete native move2_par_yxxx case**, with both sources read
before writes, with an explicit observed-opcode witness in the decode-supplied dynamic path.
No invented tuple-return proof was needed. Provenance:
`libbeam/core/otp/load-module-sources.json`.

Fresh `/tmp/libbeam-load-module-integrated-complete/summary.json` passes all
**274 commands**, **111 Debug + 111 Release CTests**, **31 existing + 68 profile
UBSan tests**, and prior lifecycle/two-Isolate acceptance. All **396 source inputs**
matched. Earlier failed runs caught strict signed-pointer warnings in the test
and an outdated escaped-work witness: measured `escaped=0 remaining=0` showed its
4096 units expired before registration. Only that witness now uses a dedicated
finite 1,000,000 units and explicitly asserts escaped state and zero remaining
credit. Exact measured boundaries, private 4096-unit tests and production defaults
are unchanged. This fixes the witness's premise, not the runtime containment rule.

## Still required

- Correct catch/unwind and admitted try/catch/rescue witnesses. The new outcome
  bypasses current guest-error/guard paths, but a refused catch fixture is not
  evidence of uncatchability through a working handler. That acceptance claim
  remains open.
- Additional invocation-level work/allocation prefixes and cancellation/race,
  background and foreign-causal-owner cases, plus explicit malformed-marker tests.
- New cross-family monitor/link/alias/tracing operations must preserve containment:
  observers/effects outside a private family cannot be ignored when deciding
  reuse. Only the already-supported private spawned-monitor path is witnessed here.
- The remaining real BIFs, query adapters, complete preserved BEAM wrappers and
  their native/internal prerequisites. Both clusters still need functioning
  catch/rescue coverage as part of the final batch audit.
- Final per-MFA coverage audit and fresh whole-batch gates, followed by actual
  application admission/execution, all route/resource/lifecycle tests and startup
  measurements. No BIF task or application acceptance is complete yet.

See [RFD 0005](rfds/0005-isolate-bif-catalog.md) and
[capability provenance](../libbeam/core/otp/capability-denial-sources.json).
