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

**The public C++ Engine now passes create → shutdown → create over the actual
additive C execution substrate. Public Isolates, copied calls and physical reclamation
execute a limited BEAM profile on the Engine worker, with copying GC and owned
code/binary retirement. The unchanged bounded-profile stateful two-Isolate proof
passes native tests. Whole Midgard application execution remains unfinished.
This is not a security boundary for untrusted workloads.**

## Layout

- [`beam/`](beam/): tracked OTP source/reference plus the prior subtractive experiment.
  This is modified source, not a pristine upstream checkout or a submodule.
- [`libbeam/`](libbeam/): the new additive C core under `core/`, C++ embedding
  interface, unchanged acceptance examples and native Bazel tests.

The experimental API is in [`engine.hpp`](libbeam/include/libbeam/engine.hpp).
Executable contracts and real compiler fixtures live in [`libbeam/tests/`](libbeam/tests/).
Historical full-ERTS/Realm tooling is retired, not an alternate runtime backend.

## Build and test

Use Bazel **9.3.0**, a native C/C++ toolchain, Erlang/OTP 28, Elixir >= 1.19,
Perl, zlib and native binutils. Reuse incremental outputs:

```sh
bazel build //libbeam:libbeam
bazel test //...
bazel test --config=release //...
bazel test --config=ubsan //...
```

The normal loop is change → focused native regression → fix → continue. Broader
runs retain the shared validation lock and a final read-only freshness check.
Native tools and retained OTP Perl generators supply build inputs; no Python or
libbeam CMake/CTest path remains. CI runs native Bazel tests in all three configurations.
Historical full-ERTS/Realm results are not current-runtime acceptance.

## Source provenance and licenses

The OTP baseline is upstream commit `cca4e72510a97cfca6427602d3da8a22d5ff7a33`
(`30.0-rc0`), imported directly from `https://github.com/erlang/otp.git`. The only
initial libbeam-specific source change separated ordinary OTP startup phases in
`beam/erts/emulator/beam/erl_init.c`. Subsequent direct emulator edits add experimental
returning startup, not complete engine lifecycle or isolates.
This repository retains its own earlier snapshot history, without importing upstream
Git history or maintaining a separate patch series.

OTP licenses, attribution and bundled third-party notices remain under
[`beam/LICENSE.txt`](beam/LICENSE.txt), [`beam/LICENSES/`](beam/LICENSES/) and the
individual source files. See [`beam/README.md`](beam/README.md) for upstream project
information.

Upstream-shipped bootstrap/preloaded BEAM files and intentional binary fixtures
remain exactly as upstream tracks them. Locally regenerated BEAMs and build output
belong only in build worktrees, not source commits.
