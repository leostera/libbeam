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

**Fast, resource-limited BEAM isolates for embedded Erlang workloads.** Scale a C++
host across independent Erlang worlds without booting a full BEAM VM for each one.
A shared immutable `Engine` creates `Isolate`s with private module namespaces,
process and application state, atoms, accounting, and reclamation lifecycle. Load
verified BEAM module groups and exchange bounded binary requests and responses.

## libbeam Isolates start about 850× faster

| Startup operation | Median | p95 |
| --- | ---: | ---: |
| **libbeam Isolate creation** | **188 µs** | **274–284 µs** |
| Fresh Erlang VM boot | 159 ms | 181–198 ms |

In this preliminary local comparison, libbeam's median Isolate startup was about
**850× faster** than booting a fresh Erlang VM. Measured in two runs of 30 samples on
an Apple M4 Pro / macOS 26.6.2, using an optimized libbeam build and OTP 29. Startup
only; no application or module workload was included. Treat these as indicative
measurements, not a cross-machine guarantee.

## Finite resource budgets

Isolates have configurable finite resource limits. Defaults are **256 MiB** of
accounted memory and **10 million invocation work units**; work units estimate
runtime effort and are not a wall-clock CPU-time guarantee. Request and response
payloads are bounded as well. This gives hosts explicit controls over embedded BEAM
workloads while keeping each Isolate's state and reclamation lifecycle independent.

libbeam is built additively from selected BEAM components, rather than by stripping
a whole OTP runtime down. It implements a selected BEAM profile—not a full OTP node—and
grants no ambient filesystem, network, port, distribution, host-control, or
native-library authority. The C core and thin C++ interface provide an experimental
embedding surface, not a security boundary for untrusted workloads.

## Layout

- [`beam/`](beam/): tracked OTP source/reference plus the prior subtractive experiment.
  This is modified source, not a pristine upstream checkout or a submodule.
- [`libbeam/`](libbeam/): the new additive C core under `core/`, C++ embedding
  interface, unchanged acceptance examples and native Bazel tests.

The experimental API is in [`engine.hpp`](libbeam/include/libbeam/engine.hpp).
Executable contracts and real compiler fixtures live in [`libbeam/tests/`](libbeam/tests/).
Historical full-ERTS/Realm tooling is retired, not an alternate runtime backend.

## Build and test

Use Bazel **9.3.0**, a native C/C++ toolchain, Erlang/OTP 29 or newer, Elixir >= 1.19,
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
returning-startup mechanics; the current Engine and Isolate runtime lives in `libbeam/`,
not in a full OTP node lifecycle. This repository retains its own earlier snapshot
history, without importing upstream Git history or maintaining a separate patch series.

OTP licenses, attribution and bundled third-party notices remain under
[`beam/LICENSE.txt`](beam/LICENSE.txt), [`beam/LICENSES/`](beam/LICENSES/) and the
individual source files. See [`beam/README.md`](beam/README.md) for upstream project
information.

Upstream-shipped bootstrap/preloaded BEAM files and intentional binary fixtures
remain exactly as upstream tracks them. Locally regenerated BEAMs and build output
belong only in build worktrees, not source commits.
