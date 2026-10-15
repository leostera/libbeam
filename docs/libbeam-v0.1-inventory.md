# libbeam v0.1 inventory — worktree snapshot

**Status: incomplete; do not use this file as a compatibility claim.** This is a
snapshot of the dirty worktree based on `61c7ede7` plus its current local changes.
The required module-closure audit stops at the first refusal described below.
The inventory deliberately separates source catalogs, verifier admission, exact
selected opcode forms, and behavioral evidence.

## What each layer means

- **Cataloged name:** a spelling or form exists in the pinned OTP source inputs.
  This grants no loader or executor authority.
- **Admitted instruction:** the exact generic BEAM operation and operands pass
  `beam_verify.c` and the applicable module/import policy. This is not proof that
  every specialized form can be transformed or dispatched.
- **Supported specific opcode:** the exact transformed opcode selection is
  authorized by `lb_instruction_supported()` and has a dispatch implementation.
- **Behavior-tested:** a test executes the operation with observable result or
  failure assertions. Parser acceptance, inventory presence, and code generation
  alone do not qualify.

The name catalogs come from `libbeam/core/otp/opcodes/genop.tab`,
`libbeam/core/otp/opcodes/emu/*.tab`, and the generated opcode projection. The
import-name source inventory is `libbeam/core/otp/bif-batch-request.txt`; the
pinned inventory test reports 185 requested MFAs and 135 direct native
`erlang` declarations. Those are source-catalog counts, not counts of supported
or behavior-tested MFAs.

## Current closure frontier

The unchanged HelloWorker bundle has 381 modules and SHA-256
`25039ee656bd1148221085309a4cab379847953b61c82074cf2b80ede8b27d91`. The
current loader does **not** admit the full closure. The HelloWorker test's
explicit 512 MiB limit reaches `crypto` (module index 197), `on_load`, bytecode
offset 16066. OTP's `crypto:on_load/0` calls `erlang:load_nif/2`; NIF loading is
explicitly denied. The runtime does not skip initialization or publish the module
as successfully initialized. With the public default 256 MiB limit, the bridge
smoke attempt refuses earlier at module index 117, stage `link/admission`, on the
accounted memory limit; its isolate closes with current memory zero and no
listener starts. See [the runtime contract](libbeam-v0.1-contract.md). No claims
about modules after either refusal are an audit of the complete closure.

The literal default `bazel test //...`, Release, and UBSan runs each reached
478 test targets: 477 passed and `//libbeam:hello_worker_test` failed at this
refusal. HelloWorker behavior and multi-Isolate tests behind the shared fixture
setup were skipped. `bazel test --check_tests_up_to_date //...` reported 477
passing and one failing test up to date, zero out of date; freshness therefore
does not clear the acceptance failure.

## Exact-form status and behavior evidence

| Form or surface | Cataloged | Admission / specific form | Behavior evidence in this worktree | Honest status |
|---|---|---|---|---|
| `bs_match` equality command, including equality spanning one machine word | Yes | Verifier accepts the tested equality layout and empty flags | `//libbeam:bs_match_equality_admission` checks verifier admission and full-word encoding; it does not execute equality, skip, binary, and integer commands end-to-end | Minimal verifier regression only; requested direct command matrix remains incomplete |
| `bs_match_string` emitted form | Yes | Exact `i_bs_match_string` selection is supported | `//libbeam/examples/hello_worker_http:match_string_test` | Tested in its fixture; not evidence for all `bs_match` commands |
| `gc_bif` `bnot/1` | Yes | Generic `gc_bif1` transformed to selected execution path | `//libbeam:core_gc_bif_forms` executes the compiler-form fixture, including failure/GC assertions | Behavior-tested for this fixture |
| `gc_bif` `list_to_atom/1` | Yes | Generic `gc_bif1` transformed to selected execution path | `//libbeam:core_gc_bif_forms` | Behavior-tested for this fixture; not blanket BIF coverage |
| `gc_bif` `erlang:'/'/2` | Yes | Generic `gc_bif2` operand layout is transformed explicitly | `//libbeam:core_gc_bif_forms` | Behavior-tested for this fixture; not blanket arithmetic coverage |
| `raw_raise` | Yes | Admitted for the tested try/catch path | `//libbeam:core_raise_execution` asserts class, reason, trace, re-raise behavior, GC, and monitor reason | Behavior-tested for listed paths only |
| `erlang:setelement/3` BIF | Yes | Exact local BIF implementation is selected | `//libbeam:core_setelement_execution` | BIF behavior tested; this does not establish support for the `set_tuple_element` opcode |
| `set_tuple_element` opcode form | Yes (`genop.tab`, `ops.tab`) | Not present in the libbeam supported-opcode dispatch profile | `//libbeam:opcode_profile_exclusions` asserts this exact selection is unsupported; this is not tuple-update execution evidence | Explicitly outside v0.1 |
| Binary append construction and fallback | Yes | Verified writable provenance may use private append; otherwise transform falls back to ordinary append | `//libbeam:binary_flow`, `//libbeam:binary_transform`, and append inference tests | Tested for the listed provenance and fallback paths |
| UTF-8 get/skip | Yes | Generic UTF-8 transform and dispatch paths exist | Existing instruction-level tests do not yet establish the requested full boundary/malformed/GC/work matrix | Partial; acceptance coverage incomplete |
| UTF-16/UTF-32 get | Yes | No matching selected transform/dispatch path is recorded in this profile | No behavior test | Unsupported in this snapshot |
| Binary/integer/float extraction and bit skipping | Yes | Only exact transformed forms with registered verifier and dispatcher paths are admitted | Existing binary/bit-syntax tests cover selected paths, not the requested full malformed-input/endian/sign/GC/work matrix | Partial; exact-form audit and behavior matrix incomplete |
| `on_load` / `erlang:load_nif/2` | Yes | `load_nif/2` is an explicit `NATIVE_LIBRARY` denial; `on_load` cannot be fake-successful | `//libbeam:hello_worker_test` reaches the real refusal | Full HelloWorker closure blocked |

## Required next audit—not yet established

1. Run the module-closure traversal to completion after a truthful `on_load` / NIF
   policy exists. For every module, record cataloged opcode names separately from
   generic operations admitted, exact transformed opcode selections, and
   behavior-tested semantics.
2. Audit every imported MFA in the reachable closure as required, intentionally
   denied, or out of scope. Keep exact names/arity and denial precedence; do not
   infer BIF support from `bif.tab`, engine catalog registration, or an import
   resolving successfully.
3. Add direct `bs_match` execution tests for equality, skip, binary, and integer
   commands, including valid flags, malformed layouts, and failure paths.
4. Finish the requested UTF-8/UTF-16/UTF-32, binary/integer/float extraction,
   and bit-skip matrix, or explicitly exclude forms that cannot be completed in
   v0.1. Test boundaries, malformed input, endian/sign behavior, failure, GC,
   and work limits.
5. Add opcode-level `set_tuple_element` refusal evidence if it remains excluded;
   the BIF test is not a substitute.
6. Re-run unchanged HelloWorker behavior, multi-Isolate lifecycle/reclamation,
   exact default `bazel test //...`, Release, UBSan, and freshness gates before
   changing this inventory's status to accepted.
