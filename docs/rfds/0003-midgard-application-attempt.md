<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 Leandro Ostera <leandro@ostera.io> -->

# RFD 0003: Midgard whole-application attempt

Status: **Compilation demonstrated; real-Isolate admission refused. No application execution success.**

This is an implementation snapshot for [RFD 0003](0003-additive-runtime-construction.md),
not an expansion of the positive runtime profile. M3–M4 acceptance was completed
before this attempt. The unchanged lifecycle and two-Isolate examples pass Debug,
Release and available UBSan; persistent workers, moving GC, allocation prefixes,
32 replacements, 4096 correlated requests, duplicate-bootstrap exceptions, queued
retirement, detached replies, peer progress and exact final allocation balance are
recorded in `/tmp/libbeam-monitor-errors-integrated-final/summary.json`.
After the application-boundary regression below, fresh full validation passes at
`/tmp/libbeam-midgard-boundary-integrated-final/summary.json` as well.

## Application and compiler identity

The existing sibling Midgard checkout is
`/Users/leostera/Developer/github.com/leostera/midgard`, revision
`eeea8607d0e63efc0f436e3bd90cec35e0940b0f`. No tracked Midgard sources were changed.
The selected whole application is `examples/hello_worker`, not a reduced replacement
or renamed fixture: two project modules, the three-module Midgard SDK and all
27 Jason dependency modules. The artifact contains 32 exact-name BEAMs, 356048
bytes total, built with Elixir 1.19.5 and installed OTP 28/ERTS 16.3.

The trusted compiler/tooling VM is distinct from libbeam's pinned interpreter
source. It performs local Mix compilation, static inspection and wire-reference
construction only. It is not a runtime fallback. Existing dependencies were
available; no dependency or application code was replaced with successful stubs.

Artifact: `/tmp/libbeam-midgard-hello-attempt/hello_worker.midgard`.
Manifest SHA-256:
`9952155ebfee13529cea032b9d8db6273285e35d58618059a34f6d3de83c13d7`.
The manifest retains compiler identities, every module checksum/size and dependency
ownership. All file sizes/checksums were checked before handing bytes to libbeam;
Midgard's inspector also checked structure/internal module identities. Generated
BEAMs are retained externally, not committed. Recompiling elsewhere may change
compiler metadata and hashes; compare the resulting manifest, not only its name.

## Reproducible compilation

Set paths to the sibling checkout and a fresh external output directory:

```sh
midgard=/Users/leostera/Developer/github.com/leostera/midgard
out=/tmp/libbeam-midgard-hello-attempt
cd "$midgard/examples/hello_worker"
export ERL_FLAGS='+S 1:1 +SDcpu 1:1 +SDio 1 +A 0'
export MIX_BUILD_PATH="$out/mix-build"
mix --version
mix test
mix midgard.build --output "$out/hello_worker.midgard"
mix packer.inspect "$out/hello_worker.midgard"
mix packer.inspect "$out/hello_worker.midgard" --target current-core
```

Native validation and these tooling commands were serialized using
`/tmp/otp-realm-validation-<uid>.lock`. Compilation used a fresh build directory,
not the checkout's existing packaged artifact. Logs and exact command/environment
records are in `compilation-summary.json` and adjacent named logs under `$out`.
Application tests: **4 tests, 0 failures**. Build and structural inspection: **0**.
Static `current-core` inspection: **1**, reporting 49 known unsupported findings
and 154 unresolved imports. That static profile is conservative and includes
`make_fun3`, which the additive runtime now admits in a bounded local-fun profile;
it is not the authoritative runtime positive list or an execution test.

The declared entrypoint is
`Elixir.HelloWorker:__midgard_call__/1`, protocol `midgard.bytes.v1`.
The trusted SDK constructed a POST `/echo` request with body
`real Midgard application` and checked the expected status-200 response on ordinary
OTP. Exact generation commands and scope are retained in
`wire-reference-command.json` / `wire-reference.log`; `request.bin` and
`expected-response.bin` are separate host-owned wire files. That reference result
is **not** reported as a libbeam result.

## Actual Isolate attempt

The existing `libbeam/tests/api_world_test.cpp` now has a generic
`--application` mode. It is an admission/execution witness, not another runtime,
a manifest parser or a fixture evaluator. It creates the actual public Engine and
Isolate, loads each supplied BEAM without changing its internal name, attempts the
declared binary arity-one entrypoint, checks a returned binary against independently
supplied expected bytes if one is returned, and physically reclaims/shuts down.
The normal regression harness still uses its original mode. A supported
`async_slice:identity/1` invocation tests this generic path on the real shared worker.

After a full build, invoke the artifact using all verified files:

```sh
build=/tmp/libbeam-midgard-boundary-integrated-final/build
"$build/api_world_test" --application \
  Elixir.HelloWorker __midgard_call__ \
  "$out/request.bin" "$out/expected-response.bin" \
  "$out/hello_worker.midgard/modules/"*.beam
```

Result in fresh **Debug, Release and UBSan** binaries: exit **1**, all 32 module
loads refused, no application module published. The actual `start` attempt returns
`unknown arity-one MFA`; no guest application result exists. Each attempt ends with
`loaded=0 refused=32 returned_binary=false expected_match=false reclaimed=true
engine_shutdown=true`. There is no serving OTP VM, module renaming or invented
reply. The supported-mode check exits 0, proving this is not a control-only facade.

Final exact commands/results are in `$out/final-isolate-summary.json` and
`real-isolate-final-{build,release,engine-ubsan}.log`. Component loader diagnostics
are separate from public-Isolate evidence:

- `Elixir.HelloWorker`: status 9, `link/admission`, offset 702.
- `Elixir.Midgard.Protocol`: status 9, `link/admission`, offset 2110.
- Both component rejection tests verify unchanged atom/allocation state.

## Concrete capability gaps

`concrete-capabilities.log` records imports and ordinary OTP disassembly from the
exact artifact. The application entry requires `get_map_elements`, `put_map_assoc`
and `try`/`try_case`/`try_end`, which are outside the admitted map/exception profile.
It imports unavailable `Elixir.Map:get/3`, `Elixir.URI:decode_query/1` and
`elixir_erl_pass:no_parens_remote/2`, as well as packaged modules which cannot yet
publish because their own required profiles/imports are unsupported.

The SDK requires `bs_start_match4`, match-position operations and binary extraction
beyond the current bounded integer/equality matching profile. It also imports
`Elixir.Enum`, `Elixir.String`, `binary:match/2`, `re:import/1` and unavailable native
services such as `byte_size/1`, `length/1`, `iolist_to_binary/1` and subtraction.
Jason adds larger map literals/operations, dynamic protocol dispatch and wider
binary/numeric/exception requirements. Alphabetical manifest order is not a load
plan; the current eager linker also lacks an unresolved multi-module batch protocol.
No general Elixir/OTP admission follows from local-fun or small-flatmap support.
These are concrete retained failures, not a reason to add unrelated opcode breadth
under this acceptance goal.

## Boundary regression found and fixed

The first actual artifact load aborted in Debug while decoding an empty `MAP_EXT`:
the already-admitted zero-entry map path called the native positive-arity tuple
header constructor for its empty key tuple. This was an internal loader defect,
not an acceptable unsupported-profile response. `beam_literals.c` now uses the
native zero-arity header, just like the existing empty-tuple producer; no larger-map
capability was added. A worker error/3/monitor fixture with empty error-info metadata
covers literal admission, copying, trace delivery and every allocation prefix.
The same trace shape passes the ordinary OTP reference under the explicit
no-line-info policy.

First abort logs/command records were preserved separately with `-first` suffixes.
After the fix the artifact consistently refuses through the normal API, reclaims
cleanly and leaves no falsely successful execution result. Full fresh additive
Debug/Release/UBSan validation passes after both the regression and generic harness
changes. No Linux, JIT, security, performance, density or quota claims are made.
