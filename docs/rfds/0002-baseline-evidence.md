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

# RFD 0002: relocated baseline and first C++ archive-link evidence

## Scope and result

**Historical evidence for the archived Realm fork, not today's clean-upstream
snapshot.** See the [transition record](0002-clean-upstream.md). Paths and commands
below refer to the pre-transition snapshot; the current driver has no Realm profiles.

**At that checkpoint, P0-01's clean local relocated baseline passed.** This is a standalone OTP build
and selected regression baseline, not the reduced-profile isolate runtime.
**P0-02 remains open**; the [initial source findings](0002-engine-seams.md) are not
a complete ownership/positive-list manifest. **P0-03 remains open**: the C++ probe
links actual emulator code but deliberately does not initialize an engine.

The RFD revision `8a9327f8788e2766fe737e79082f04c6b0f15d81` was pushed to `main`
before these experiments. A detached worktree at that revision was created at
`/tmp/libbeam-p0-01/source`. Builds started with clean, unconfigured `beam/` sources;
no pre-relocation binaries or Makefiles were copied in. The new driver was run from
the development checkout, with its content hash recorded separately from that source
revision. Platform: `macOS-26.6.2-arm64-arm-64bit-Mach-O`.

## Reproduction

See [`libbeam/README.md`](../../libbeam/README.md#build-and-link-probes) for implemented
commands. The actual baseline invocation was:

```sh
python3 -B libbeam/tools/build_baseline.py \
  --source-root /tmp/libbeam-p0-01/source \
  --output /tmp/libbeam-p0-01/first \
  --configure-arg=--with-ssl=/opt/homebrew/opt/openssl@3 \
  --variant debug-emu --jobs 4
```

Configure, default bootstrap build, `update_preloaded --no-commit` and rebuild all
passed. The validation runner then built and identity-probed the debug interpreter
and ran these strict profiles:

| Profile | Passed | Failed | Skipped |
| --- | ---: | ---: | ---: |
| `all`: 64 Realm + 84 focused legacy cases | 148 | 0 | 0 |
| `resources`: 9 Realm + 7 atomics + 6 counters + 22 persistent-term | 44 | 0 | 0 |
| `public-api`: historical Realm wrappers | 5 | 0 | 0 |

These are 197 test executions, not 197 distinct cases: the resource Realm cases
are intentionally repeated across profiles. The bootstrap also builds an optimized
runtime, but no additional optimized-variant suite acceptance is inferred here.
Full OTP, other platforms, production security and first-execution latency are not
established by these selections. Build/test deprecation and Java compiler warnings
remain in the logs rather than being claimed absent.

Final worktree changes in the detached build are generated preloaded BEAMs only:
`erl_prim_loader`, `erlang`, `erts_code_purger`, `erts_internal` and `init`. None is
committed. Existing development-checkout generated `erts_internal.beam` also remains
uncommitted.

## C++ link witness and retained failed attempt

The first `link_archive_probe.py` attempt rebuilt a debug-interpreter archive using
OTP's own make target, linked the C++ executable, then failed its symbol assertion.
The assertion used `nm -g` and incorrectly required an exported `T` definition.
On this Mach-O build, `erl_start` is a hidden/local `t` definition. This was a probe
bug, not a link failure or an implemented engine lifecycle.

The corrected probe inspects all symbols and accepts defined `T`/`t` symbols with
ELF/Mach-O spelling, but rejects undefined `U` entries. A unit test covers both
positive forms and missing/undefined cases. The corrected fresh-output run reports:

```text
linked_not_initialized
LIBBEAM_ARCHIVE_LINK_OK engine_started=false isolates_created=0
```

It recreates only the generated `bin/<target>/libbeam.a`, preventing stale members
from another build variant, and copies it to `libbeam.debug.emu.a` in the evidence
directory. Its link flags/dependency libraries come from the configured emulator
Makefile. No isolate, engine object or runtime startup is simulated by the fixture;
there is no `Engine::create()` implementation yet.

## Tooling relocation and validation

- BIF/NIF, native-surface and source-review tools now read evidence from
  `docs/rfds/`, keeping source anchors relative to the OTP root `beam/`.
- Both Realm workflows now run their OTP commands under `beam/`; matrix aggregation
  runs from the repository root using `beam/scripts/realm-ci-report.py`.
- The collector permits only generated preload changes with either historical or
  relocated paths; source changes under `beam/` or `libbeam/` still fail validation.
- 53 existing-tooling tests and 7 new build/link-tool tests pass. All four inventory/
  review checks pass without regenerating or approving native policy records.
- The inherited license scanner initially failed because it treated the repository
  root as the OTP root and resolved vendor paths against the caller's directory.
  It now locates relocated OTP templates and resolves vendor paths against that root.
  The changed/new-file scan passes for 18 files with zero file warnings (the existing
  deprecated `exit/2` warning in the scanner remains). A standalone-layout fixture
  also passes with a valid header and fails with a missing header.
- Workflow YAML and documentation links validate. Hosted matrix execution is still
  unclaimed. Other inherited upstream workflows are not certified by these changes.

## Evidence identities

Raw logs stay local because native test output may contain node cookies and local
paths. Summaries contain commands, timings, driver identity and nested report hashes.
These identities allow matching local evidence; they are not independent attestation.

| Local summary | Status | SHA-256 |
| --- | --- | --- |
| `/tmp/libbeam-p0-01/first/summary.json` | `passed` | `253f1bab54d8056552e7cff2917f913c755e05af8468c3dcb5a8df057f4d0f21` |
| `/tmp/libbeam-p0-01/link-probe/summary.json` | `failed` (overstrict symbol check) | `9b2a1d76f7e0f753bf36898184c91506b36a801d8af223351927e1e46e11dd22` |
| `/tmp/libbeam-p0-01/link-probe-fixed/summary.json` | `linked_not_initialized` | `0f3c54b845ea7b052c93ad0fdb4eac9d6d42b5defcc3e240186ebb9df70c6c62` |

Baseline driver hash:
`59bf943fa9deafdbff62b661c5c03e95c03f3fbbb1ccc21ab38e45380d261058`.
Corrected link driver hash:
`3a9d147a11646b3c225223d0d50592ba332495b6075c11d8415e13f289f984ea`.
Linked host hash:
`15e30cc8fc91a32f3b33ed279090249f400480f8372d91b98773e6160087f7a5`.
Archive hash:
`bec6834f5a72a8c2f5c5de7d772e37e720156a2c09616abd68b529f07c451c84`.

Next: complete the engine/world initialization and native-effect map, then implement
real engine creation/shutdown returning to host code. The two-isolate conflicting-MFA
proof, closed bytecode profile, eviction and latency/density targets remain open.
