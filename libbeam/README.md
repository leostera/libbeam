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

# C core and C++ embedding interface (in development)

**Current direction:** [RFD 0003](../docs/rfds/0003-additive-runtime-construction.md)
replaces whole-OTP reduction with additive construction in C. See [`core/`](core/)
for bootstrap allocation ownership and the first owned BEAM-image parser. These
components do not yet execute BEAM code or back the public factory. The native archive work below is preserved
as diagnostic/reference evidence, not a fallback runtime for the new core.

This directory contains build/link probes and an
[API-first two-isolate example](examples/two_isolates.md). Its experimental C++
library and CMake target now build and run, but deliberately fail at the first
unfinished operation (`Engine::create`). No successful runtime behavior is faked.
RFD 0003 supersedes the construction strategy in the earlier
[implementation plan](../docs/rfds/0002-example-implementation-plan.md), while
retaining its real-engine, private-world, execution and reclamation goals.
[Native archive integration](../docs/rfds/0002-native-api-link.md) now connects the
C++ target to actual ERTS and retains joinable scheduler-thread handles. The public
factory still refuses initialization until ownership/cleanup exists.
[Unbooted preparation](../docs/rfds/0002-unbooted-preparation.md) now initializes
native substrate without starting OTP processes or workers. The engine path must
not reuse whole-world startup.
[Owned cold startup stages](../docs/rfds/0002-owned-cold-startup-stages.md) add
aggregate poll-group rollback, cold I/O release and owned thread-progress/TLS
release. [Allocator ownership](../docs/rfds/0002-owned-allocator-domain.md) now
puts major allocator/backing/mapper storage under the engine and retains aligned
allocation bases for explicit release. Complete Engine initialization/shutdown
remains unfinished. The [partial ownership-tree sweep](../docs/rfds/0002-ownership-tree-sweep.md)
adds Engine namespace membership, namespace-owned registered names, owned I/O
event state and an early ERTS TLS-key ledger. Process/port tables, ETS, persistent
terms, timers and complete retirement remained outstanding at that checkpoint.
The [integration sweep progress](../docs/rfds/0002-integration-sweep-progress.md)
now adds the real Engine constant arena and namespace-owned persistent-term
storage, traps and deferred cleanup. The sweep is still incomplete.
The [current ownership inventory](../docs/rfds/0002-current-ownership-inventory.md)
separates completed storage/access migrations from remaining globals and lifecycle work.
`engine_lifecycle` is the explicit create → shutdown → create executable target;
it currently exits 1 at the first factory call, in both native and scaffold builds.
[Native ownership migration](../docs/rfds/0002-native-ownership.md) introduces an
explicit `ErtsEngine`, engine-owned lifecycle/handle-registry state, and scheduler
owner references. Remaining globals are classified by engine versus isolate
ownership; multiple initialized runtimes/private worlds are not implemented.
[Owned module tables](../docs/rfds/0002-owned-module-tables.md) are the first
namespace-storage extraction: independent metadata records and unpublished table
disposal work. [Private atom namespace state](../docs/rfds/0002-owned-atom-state.md)
now owns atom indices, locks and reclaimable name storage alongside module metadata.
[Diagnostic startup](../docs/rfds/0002-diagnostic-namespace-state.md) now uses that
same state-owned atom/module storage; cached global atom-name literals are gone.
The [code-space ownership batch](../docs/rfds/0002-owned-code-space.md) now includes
exports/literals, funs, records, catches, ranges, old-code locks, unsealed tracking
and coordinated table transactions. Diagnostic execution uses those components.
Private loader/process context propagation, executable/literal retirement and
public Engine/Isolate construction remain unfinished.
[Namespace lifetime flow](../docs/rfds/0002-namespace-lifetime-flow.md) now connects
process/spawn affinity and loader/code/literal leases, but is only a partial
checkpoint of that migration.
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
