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

# C++ embedding layer (in development)

This directory contains initial build/link probes and will contain libbeam's public
host interface, implementation, two-isolate example and integration tests. Core emulator changes belong in
[`../beam/`](../beam/), not in a second copy of the VM here.

[RFD 0002](../docs/rfds/0002-libbeam-isolates.md) defines the first proof and its
acceptance criteria. No engine header, CMake target or runnable two-isolate example
exists yet. The archive-link probe below is not an engine or isolate implementation.
The initial interface is intended to manage one engine and multiple freshly created
isolates with bounded binary requests/results and explicit stop/reclamation.
Tenants run a positive-list BEAM-bytecode profile, not a complete OTP node; no
arbitrary native extensions or ambient OS capabilities are part of that contract.
First-execution latency is distinct from completion time. CPU/deadline/memory
budgets and suspend/resume remain explicit follow-on gates, not implemented controls.

## Build and link probes

Use a fresh detached worktree so that stale pre-relocation outputs cannot influence
the baseline. These commands are implemented (unlike the RFD's future CMake targets):

```sh
git worktree add --detach /tmp/libbeam-baseline-source HEAD
python3 -B libbeam/tools/build_baseline.py \
  --source-root /tmp/libbeam-baseline-source \
  --output /tmp/libbeam-baseline-results --variant debug-emu --jobs 4
# On macOS with Homebrew OpenSSL, add:
# --configure-arg=--with-ssl=/opt/homebrew/opt/openssl@3

python3 -B libbeam/tools/link_archive_probe.py \
  --baseline /tmp/libbeam-baseline-results/summary.json \
  --output /tmp/libbeam-archive-probe
```

Both output directories must be new and outside the source worktree. The baseline
requires clean, unconfigured sources; it configures/builds OTP, regenerates preloads,
rebuilds, then runs strict standalone legacy regressions. It does not exercise the
new reduced profile. It shares the user-wide validation lock and records source/tool
identity, commands, logs, timeouts and child summaries. The lock does not coordinate
arbitrary unrelated user processes or other users.

The Unix debug-interpreter link probe requires that baseline to have passed. It
recreates **only the generated `bin/<target>/libbeam.a`** to avoid mixed-variant
archive members, copies the archive to its output directory, and uses OTP's generated
Makefile link settings to compile `examples/archive_link_probe.cpp`. It checks the
linked `erl_start` symbol and runs the host **without invoking it**. Success is
`linked_not_initialized`; neither engine lifecycle nor P0-03 is thereby accepted.
The probe is experimental and not a portable library packaging interface yet.

Tests: `python3 -B -m unittest discover -s libbeam/tools -p 'test_*.py' -v`.
See [initial source findings](../docs/rfds/0002-engine-seams.md) for why linking is
already possible but host-safe startup, shutdown and instance ownership need work.

Keep HTTP routing and deployment orchestration above this library. Do not implement
"isolates" by spawning `erl` subprocesses, assigning prebooted instances or renaming
application modules.
