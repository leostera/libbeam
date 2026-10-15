# libbeam v0.1 runtime contract (prototype target)

**Status: draft target; not yet accepted.** This contract describes the prototype boundary and identifies an unresolved incompatibility with the unchanged HelloWorker bundle. It does not claim whole-OTP compatibility or that the current acceptance suite passes.

## Value to a host

A C++ host creates one shared `Engine`, creates isolated `Isolate`s, loads BEAM modules from caller-owned bytes, starts or calls a named arity-one guest entry with a binary payload, and waits for binary response bytes. Isolates share immutable engine/runtime machinery but keep their module namespace, process state, atoms, accounting, and application state separate. Stopping an isolate closes admission and asynchronously drains/reclaims its world; destroying a handle is not proof of reclamation.

The public surface is the experimental C++ API in `libbeam/include/libbeam/engine.hpp`: `Engine::create`, `Engine::create_isolate`, `Isolate::load_module`/`load_modules`, `start`/`call`, `Call::wait_until`/`cancel`, `resource_usage`, and `stop`/`Reclamation::wait_until`. API inputs are copied at admission. Module groups are eagerly verified and published atomically; loading after start is rejected. The API is experimental and makes no stable ABI promise.

## Inputs and admission

- Input is BEAM module bytes supplied by the host. The runtime derives the module name from the image; it does not search the filesystem or implicitly start OTP applications.
- A module group may contain exact-name forward references and cycles. All imports, instruction forms, and metadata are verified before publication. Failure rolls back the unpublished group.
- The loader does not promise to load arbitrary BEAM code. The supported input profile is the exact verifier/transform/dispatch profile and exact import bindings documented by the [current worktree inventory](libbeam-v0.1-inventory.md) and executable tests. A cataloged opcode name or imported MFA alone is not evidence of support.
- Batch admission limits are 4,096 modules and 64 MiB aggregate encoded input (`LB_CODE_BATCH_MODULE_LIMIT`, `LB_CODE_BATCH_BYTES_LIMIT`).
- The API boundary limits each call payload and result to 64 KiB, allows at most 64 outstanding calls per isolate, and bounds queued payload to 1 MiB per isolate. These transport limits are separate from the heap/work limits.

## Resource policy

Public defaults are **256 MiB accounted memory per isolate** and **10,000,000 invocation work units**. Explicit resource overrides must be finite and positive; zero does not mean unlimited. Accounted memory is not a promise about process RSS. Work units meter the admitted native paths and are not a wall-clock CPU-time guarantee. The HelloWorker test may opt into a larger memory limit locally; this does not change public defaults.

## BEAM and BIF compatibility policy

libbeam is a bounded runtime profile, not an OTP VM. For each exact instruction form and imported MFA required to admit the selected module closure, classify it as one of:

1. implemented, with behavior tests;
2. an exact standard-library compatibility binding that resolves at load time and raises a defined guest runtime error only if invoked;
3. an exact capability denial that retains its explicit denial result and precedence; or
4. outside the selected v0.1 module profile.

An unresolved import fallback is never used. Compatibility-error bindings are not substitutes for capability denials, and capability failures must not be turned into ordinary guest exceptions or false values. Standard-library imports needed for module loading do not imply blanket OTP BIF compatibility. Each binding's exact MFA and invocation behavior must be inventoried and tested.

## Explicit denials and missing platform services

There is no implicit filesystem, network, port, distribution, host-output, host-control, dynamic-code-loading, or native-library authority. Exact denied `erlang` MFAs and denial categories are defined in `libbeam/core/capability_denials.def`; in particular, NIF loading (`erlang:load_nif/2`) is denied. A denied operation must fail explicitly; the runtime must not pretend that a host service or native library was initialized.

### Current release blocker: `on_load` versus denied NIF loading

**Decision for the prototype target:** do not fake-success an `on_load` callback. `on_load` initialization remains refused unless its real initialization can be executed truthfully under the v0.1 capability policy.

The unchanged HelloWorker bundle contains `crypto` (module index 197 in the current artifact). Pinned OTP source declares `-on_load(on_load/0)` in `beam/lib/crypto/src/crypto.erl`; that callback consults application/code/priv-directory state and calls `erlang:load_nif/2`. NIF loading is explicitly denied. Skipping the callback and publishing `crypto` as initialized would lie about OTP initialization; running it reaches a denied capability and must fail module loading. Therefore the current unchanged full module closure cannot yet be admitted while preserving the NIF denial. This is a **v0.1 blocker**, not an exception to hide in the loader. With the HelloWorker test's explicit 512 MiB limit, the load stops at `crypto`, module index 197, stage `on_load`. The default 256 MiB bridge attempt hits the accounted memory limit earlier (module index 117, stage `link/admission`) and also refuses before listening.

The blocker can only be cleared by a reviewed policy/architecture change that truthfully permits the target artifact while preserving the no-fake-initialization invariant, or by changing the target artifact/closure. Neither change is silently assumed here.

## Non-goals

- Implementing every OTP opcode, BIF, application, or startup path.
- Claiming complete Erlang/Elixir compatibility from a finite module closure or a successful function-name lookup.
- Supplying host services that are not present in the explicit API/capability policy.
- Claiming v0.1 acceptance until the unchanged target closure, real HelloWorker behavior, multi-isolate lifecycle, required test gates, and runnable examples all pass.

## Acceptance evidence required before calling this v0.1

Keep source inventory, admitted-form inventory, and execution evidence separate. Require a fresh closure audit; direct behavior tests for selected forms and denials; real HelloWorker request/response tests; two-isolate state/lifecycle/reclamation tests; exact default `bazel test //...`; Release and UBSan runs; and runnable, reviewed host examples. Focused tests are diagnostic evidence, not a substitute for those gates.
