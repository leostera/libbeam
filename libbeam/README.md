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

This directory contains build/link probes and an
[API-first two-isolate example](examples/two_isolates.md). Its experimental C++
library and CMake target now build and run, but deliberately fail at the first
unfinished operation (`Engine::create`). No successful runtime behavior is faked.
The [implementation plan](../docs/rfds/0002-example-implementation-plan.md) tracks
the real engine, private worlds, execution and reclamation work ahead.
[`../beam/`](../beam/) is a tracked OTP snapshot. Edit emulator sources directly
and commit them alongside embedding changes; there is no submodule or patch series.
No Realm implementation is part of the active source.

[RFD 0002](../docs/rfds/0002-libbeam-isolates.md) defines the first proof and its
acceptance criteria. The C++ API scaffold is not a functioning isolate runtime or
stable SDK. The archive-link probe does not start an engine;
the separate experimental native entry below does, without shutdown or isolates.
The initial interface is intended to manage one engine and multiple freshly created
isolates with bounded binary requests/results and explicit stop/reclamation.
Tenants run a positive-list BEAM-bytecode profile, not a complete OTP node; no
arbitrary native extensions or ambient OS capabilities are part of that contract.
First-execution latency is distinct from completion time. CPU/deadline/memory
budgets and suspend/resume remain explicit follow-on gates, not implemented controls.

## Build and link probes

Use a fresh detached **libbeam repository** worktree for reproducible builds.
These native probes are separate from the new CMake API scaffold:

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
requires clean, unconfigured sources. It configures/builds `beam/`, regenerates
preloads, rebuilds, then runs a toolchain startup smoke profile (three fresh VMs).
Use `--legacy-diagnostics` to additionally run 84 focused upstream cases and 35
resource cases when useful. Those suites are opt-in diagnostics, not compatibility
requirements. There are no Realm suites or patch-application steps. The baseline
alone does not test host control or the reduced-operation boundaries. It shares the user-wide validation lock and records source/tool
identity, commands, logs, timeouts and child summaries. The lock does not coordinate
arbitrary unrelated user processes or other users.

The Unix debug-interpreter link probe requires that baseline to have passed. It
recreates **only the generated `bin/<target>/libbeam.a`** to avoid mixed-variant
archive members, copies the archive to its output directory, and uses OTP's generated
Makefile link settings to compile `examples/archive_link_probe.cpp`. It checks the
linked `erl_start` symbol and runs the host **without invoking it**. Success is
`linked_not_initialized`; neither engine lifecycle nor P0-03 is thereby accepted.
The probe is experimental and not a portable library packaging interface yet.

## Experimental real-runtime startup

After building a current snapshot, this POSIX bring-up probe links the actual emulator,
calls `erl_start_embedded`, receives control back in C++, observes ordinary Erlang
execution in the same PID, and rejects a second startup. It is **not** a stable API.

```sh
python3 -B libbeam/tools/run_engine_start_probe.py \
  --root /tmp/libbeam-baseline-source/beam \
  --output /tmp/libbeam-engine-start-results --iterations 3
```

The native contract is in `beam/erts/emulator/beam/erl_embed.h`. Call once on the host
main thread. Normal startup preserves the host's tested signal state, but fatal
crash-dump/exit behavior remains unsafe. There is no embedding-mode switch: the
forker/spawn drivers, standalone signal dispatcher and Darwin driver-pump APIs have
been deleted. Spawn-port forms fail before effects; BINDIR is not required.
Other port/native capabilities remain.
Asynchronous boot is not acknowledged
by the native return itself; the test waits for a bytecode witness through stdout.
A private inherited FD controls only the test host. No tenant transport is implemented.

**There is no engine destructor, restart or isolate creation.** The experiment ends
with explicit OS process exit, not engine shutdown. Darwin wx/Cocoa main-thread
callbacks are unsupported. Trusted fixtures only; not a reduced-profile runtime.
The probe checks nine spawn-port denials, deleted command/signal APIs, absence of
node bootstrap services and forbidden native symbols, no children at acknowledgement,
and preserved host signal dispositions, mask and alternate stack. Retained FD
EOF/close and float operations are exercised too. Four additional PTY cases check
that intentional process exit does not change host stdin flags or terminal
attributes; see [terminal-ownership evidence](../docs/rfds/0002-terminal-ownership.md).
Those executable-host tests are not engine-destruction evidence.
Its three BINDIR cases are absent, nonexistent, and a sentinel-writing canary helper;
none should execute a helper or command. See the
[current reduction evidence](../docs/rfds/0002-single-runtime.md) and
[historical first-start evidence](../docs/rfds/0002-engine-start-evidence.md).

Tests: `python3 -B -m unittest discover -s libbeam/tools -p 'test_*.py' -v`.
Historical Realm validation evidence predates this migration and does not certify
these sources; see [the clean-upstream transition](../docs/rfds/0002-clean-upstream.md).
See [initial source findings](../docs/rfds/0002-engine-seams.md) for why linking is
already possible but host-safe startup, shutdown and instance ownership need work.

Keep HTTP routing and deployment orchestration above this library. Do not implement
"isolates" by spawning `erl` subprocesses, assigning prebooted instances or renaming
application modules.
