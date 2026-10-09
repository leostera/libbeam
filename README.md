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

**The embedding API and isolate proof are not implemented yet. This is not a
security boundary approved for untrusted workloads.**

## Layout

- [`beam/`](beam/): tracked upstream OTP snapshot plus direct libbeam emulator changes.
  Edit and commit these files normally; there is no submodule or patch-application step.
- [`libbeam/`](libbeam/): build/link probes and the planned C++ embedding interface,
  two-isolate host example and tests.
- [`docs/rfds/`](docs/rfds/): design proposals, contracts and historical evidence.

Start with [RFD 0002: libbeam isolates](docs/rfds/0002-libbeam-isolates.md).
It specifies the first executable proof: one native host, two simultaneously live
isolates with conflicting same-name modules, independent local state, and safe
teardown/recreation without restarting the engine. The latency objective is
single-digit milliseconds to **first execution**, not application completion.
Concurrent density, execution budgets/eviction and suspend/resume have separate
tracked gates; 1,000 sequential proof cycles do not prove 1,000 resident isolates.
Its two-isolate CMake commands are proposed interfaces, not supported targets yet.
The [clean-baseline and archive-link probes](libbeam/README.md#build-and-link-probes)
are implemented starting points; they do not initialize independent isolates.

[RFD 0001](docs/rfds/0001-beam-realms.md) documents the earlier Realm experiment.
Its implementation has been removed from the active emulator. The complete previous
snapshot remains at `archive/realm-snapshot` (`eb019d92`) in this repository's history.
Historical source links and commands refer to that snapshot, not today's `beam/`.
No Realm APIs, quotas, resource guards or process boundaries are carried forward.
The old evidence is not acceptance for the clean-upstream implementation.

An ordinary clone includes the source. Build in a fresh detached repository worktree
as described in [`libbeam/README.md`](libbeam/README.md); do not reuse the old Realm
build outputs. The active workflow checks Python tooling and snapshot layout only.
Historical Realm and copied upstream workflows have been removed, not counted as passing.

## Source provenance and licenses

The OTP baseline is upstream commit `cca4e72510a97cfca6427602d3da8a22d5ff7a33`
(`30.0-rc0`), imported directly from `https://github.com/erlang/otp.git`. The only
initial libbeam-specific source change separates ordinary OTP startup phases in
`beam/erts/emulator/beam/erl_init.c`; it provides neither engine lifecycle nor isolates.
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
