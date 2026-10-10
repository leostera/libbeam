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

# libbeam

An experimental project to turn BEAM into an embeddable engine that creates fresh,
independently owned, reduced-profile Erlang execution worlds on demand. A C++ host
would manage isolate lifecycle; ordinary Erlang programs would not need isolate-
management APIs. Tenants supply BEAM bytecode only: no native extensions, ambient
filesystem/network access or full conventional OTP-node environment is promised.
We are now building libbeam additively from selected BEAM components, rather than
stripping a whole OTP runtime down. Preserving `erl` or full OTP behavior is
not a goal. Legacy failures matter only when they reveal a problem in required
runtime behavior or supported language semantics.

**The public C++ Engine/Isolate API and isolate proof are not implemented yet. An
experimental native startup entry runs BEAM and returns to its host, but cannot
shut it down safely. This is not a security boundary for untrusted workloads.**

## Layout

- [`beam/`](beam/): tracked OTP source/reference plus the prior subtractive experiment.
  This is modified source, not a pristine upstream checkout or a submodule.
- [`libbeam/`](libbeam/): the new additive C core under `core/`, C++ embedding
  interface, unchanged acceptance examples, tests and historical build/link probes.
- [`docs/rfds/`](docs/rfds/): design proposals, contracts and historical evidence.

Start with [RFD 0003: additive runtime construction](docs/rfds/0003-additive-runtime-construction.md).
The [runtime work inventory](docs/rfds/0003-additive-runtime-inventory.md) maps
remaining dependencies to first execution and the unchanged two-Isolate example.
RFD 0003 adopts a C ownership layer and C transplants, retaining the C++ host API and
[RFD 0002's isolate contract](docs/rfds/0002-libbeam-isolates.md).
That contract specifies the executable proof: one native host, two simultaneously live
isolates with conflicting same-name modules, independent local state, and safe
teardown/recreation without restarting the engine. The latency objective is
single-digit milliseconds to **first execution**, not application completion.
Concurrent density, execution budgets/eviction and suspend/resume have separate
tracked gates; 1,000 sequential proof cycles do not prove 1,000 resident isolates.
The [two-isolate C++ example](libbeam/examples/two_isolates.md) now specifies the
API we want, with same-module Erlang fixtures. Its CMake scaffold now links and
runs, explicitly failing at `Engine::create` until real behavior is implemented.
RFD 0003 supersedes the construction strategy in the earlier
[implementation plan](docs/rfds/0002-example-implementation-plan.md), keeping the
example—not broad runtime cleanup—as the development driver.
The [build/link probes](libbeam/README.md#build-and-link-probes) and
[real-runtime startup-return experiment](docs/rfds/0002-engine-start-evidence.md)
are implemented starting points; they do not initialize independent isolates.
There is now one runtime contract, with no embedding-mode switch. Native spawn/forker
and signal-administration implementations, Darwin driver takeover, and node/network
bootstrap services have been deleted; see
[the runtime reduction and evidence](docs/rfds/0002-single-runtime.md). Other native
effects remain, so this is not yet an enforced bytecode-only profile.

[RFD 0001](docs/rfds/0001-beam-realms.md) documents the earlier Realm experiment.
Its implementation has been removed from the active emulator. The complete previous
snapshot remains at `archive/realm-snapshot` (`eb019d92`) in this repository's history.
Historical source links and commands refer to that snapshot, not today's `beam/`.
No Realm APIs, quotas, resource guards or process boundaries are carried forward.
The old evidence is not acceptance for the clean-upstream implementation.

An ordinary clone includes the source. Build in a fresh detached repository worktree
as described in [`libbeam/README.md`](libbeam/README.md); do not reuse the old Realm
build outputs. The source-check workflow now includes standalone additive C
component tests in debug/release builds, alongside Python tooling and snapshot
layout checks. These are not Engine or Isolate acceptance.
Historical Realm and copied upstream workflows have been removed, not counted as passing.

## Source provenance and licenses

The OTP baseline is upstream commit `cca4e72510a97cfca6427602d3da8a22d5ff7a33`
(`30.0-rc0`), imported directly from `https://github.com/erlang/otp.git`. The only
initial libbeam-specific source change separated ordinary OTP startup phases in
`beam/erts/emulator/beam/erl_init.c`. Subsequent direct emulator edits add experimental
returning startup, not complete engine lifecycle or isolates.
See the [transition record](docs/rfds/0002-clean-upstream.md) for snapshot verification.
This repository retains its own earlier snapshot history, without importing upstream
Git history or maintaining a separate patch series.

OTP licenses, attribution and bundled third-party notices remain under
[`beam/LICENSE.txt`](beam/LICENSE.txt), [`beam/LICENSES/`](beam/LICENSES/) and the
individual source files. See [`beam/README.md`](beam/README.md) for upstream project
information.

Upstream-shipped bootstrap/preloaded BEAM files and intentional binary fixtures
remain exactly as upstream tracks them. Locally regenerated BEAMs and build output
belong only in build worktrees, not source commits.
