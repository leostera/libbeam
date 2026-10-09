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
independently owned Erlang execution worlds on demand. A C++ host would manage
isolate lifecycle; ordinary Erlang programs would not need isolate-management APIs.

**The embedding API and isolate proof are not implemented yet. This is not a
security boundary approved for untrusted workloads.**

## Layout

- [`beam/`](beam/): Erlang/OTP source snapshot and experimental emulator changes.
- [`libbeam/`](libbeam/): planned C++ embedding interface, host example and tests.
- [`docs/rfds/`](docs/rfds/): design proposals, contracts and historical evidence.

Start with [RFD 0002: libbeam isolates](docs/rfds/0002-libbeam-isolates.md).
It specifies the first executable proof: one native host, two simultaneously live
isolates with conflicting same-name modules, independent local state, and safe
teardown/recreation without restarting the engine. Its build commands are proposed
interfaces, not commands supported by the current checkout.

[RFD 0001](docs/rfds/0001-beam-realms.md) documents the earlier Realm experiment.
Its ownership/lifetime work informs this direction; its test results are not evidence
that the new embedding architecture exists. Older scripts, workflows and evidence
use the former repository-root OTP layout and require an explicit relocation pass
before they can be used as current validation. Previously configured local build
outputs also contain old paths: use a clean build for relocation validation.

## Source provenance and licenses

The OTP baseline is upstream commit `cca4e72510a97cfca6427602d3da8a22d5ff7a33`
(`30.0-rc0`), with the experimental Realm changes described in the RFDs. This
repository intentionally starts with one source snapshot rather than OTP's Git
history; old commit IDs in the historical evidence are not ancestors here.

OTP licenses, attribution and bundled third-party notices remain under
[`beam/LICENSE.txt`](beam/LICENSE.txt), [`beam/LICENSES/`](beam/LICENSES/) and the
individual source files. See [`beam/README.md`](beam/README.md) for upstream project
information.

OTP ignore rules live in `beam/.gitignore`. Generated runtime binaries, configured
build products and test-output trees are not part of the snapshot. Upstream-shipped
bootstrap/preloaded BEAM files and intentional binary test fixtures **are** source
inputs and are retained; locally regenerated `erts_internal.beam` is not included
in place of its upstream bootstrap version.
