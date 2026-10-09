<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 Leandro Ostera <leandro@ostera.io> -->

# Two isolates: the API we want

[`two_isolates.cpp`](two_isolates.cpp) is the executable specification we will work
toward. The [experimental API](../include/libbeam/engine.hpp) now has a
[buildable scaffold](../src/engine.cpp). **It links and runs, then exits 1 at
`not implemented: Engine::create`.** Every unfinished API reports an explicit
error. A [native package build](../../docs/rfds/0002-native-api-link.md) now links
real ERTS and reports the more specific cooperative stop/join blocker. Neither
build starts a VM through this API or simulates a successful engine/isolate/result.
See the [milestone implementation plan](../../docs/rfds/0002-example-implementation-plan.md).

## Read the example as the goal

```cpp
auto engine = require(beam::Engine::create());
auto a = require(engine.create_isolate());
auto b = require(engine.create_isolate());
require(a.load_module(view(code_a)));
require(b.load_module(view(code_b)));
auto ready_a = require(a.start("probe", "boot", view("")));
auto ready_b = require(b.start("probe", "boot", view("")));
expect(ready_a, "A:ready");
expect(ready_b, "B:ready");
```

Both fixtures are named `probe.beam`, declare `-module(probe)`, export the same
functions, and register a persistent process as `probe_state`. Only their version
tag differs. The full example checks:

1. One engine, two simultaneously live fresh isolates; both starts admitted before
   waiting, with invocation results correlated independently of waiting order.
2. The same `probe:request/1` resolves to different code in A and B.
3. A's counter reaches 2 while B's remains 1; both local registrations coexist.
4. B accepts work across A's stop/reclamation, and still works afterwards.
5. A's closed handle rejects admission, rather than binding to a new generation.
6. A fresh replacement registers the same local name and starts at counter zero;
   B retains its own state and code.
7. All worlds are physically reclaimed, handles released, and engine shutdown
   returns before success is printed. A host-owned reply survives both isolate
   reclamation and engine shutdown.

The persistent server is deliberately **unlinked from the invocation process**.
Returning from `boot/1` must not destroy it, and stopping the isolate must find and
reclaim it. This cannot be implemented as the lifetime of just one host call.

No separate VMs, OS processes, independent emulator-library copies, renamed modules,
or swapping one global namespace between calls count as the implementation.

## API decisions to implement

- Host supplies module bytes; filenames and filesystem authority stay in C++.
  Loading derives the module identity from BEAM. The example releases B's input
  bytes before starting it.
- `start` and `call` admit asynchronous binary arity-one invocations. Their
  move-only handles correlate completion. Waiting blocks the control thread, not
  the engine's schedulers; other isolates keep running.
- This proposes a small **wait-handle facade**, rather than exposing the polling
  loop in the earlier RFD pseudocode. No arbitrary callbacks under VM locks.
  Completion queues must still obey the RFD's bounds and exactly-once rules.
- `stop` closes admission immediately and returns a reclamation handle. Successful
  waiting must mean actual release of the world, not merely stopping its root PID.
  Timeout means pending work, never permission to force-free reachable state.
- Explicit `shutdown` requires reclaimed worlds and released handles and must
  return without terminating the host. Destructors must remain safe during error
  unwinding; their eventual cleanup coordination is not implemented or certified
  by this successful-path example.

The header is under `include/libbeam/`, in namespace `libbeam`, but remains an
experimental, unstable API. The comments specify target semantics, not implemented
behavior. There is no mode option that restores a full OTP node.

## Implementation order driven by this program

| Example operation | Real work required |
| --- | --- |
| `Engine::create` | Own the shared runtime/thread infrastructure; keep host control and errors explicit. |
| `create_isolate` | Create fresh owned world state and retain ownership across processes and deferred work. |
| `load_module` | Private atom/code/export/import/fun resolution and transactional publication. Both code versions must remain resident. |
| `start` / `call` | Minimal host-driven bootstrap, per-isolate process creation and registry lookup, copied binary inputs and bounded correlated completions. |
| `stop` / replacement | Quiesce/drain processes, queues, timers and code references; reclaim safely; protect stale generations. |
| `shutdown` | Join engine threads and release engine-owned state without process exit. |

Remove legacy machinery when it blocks one of these operations. Broad cleanup is
not a prerequisite. Explicit `not_implemented` stubs now mark the frontier for the
new API; they must never manufacture success. This supersedes the earlier
no-link proposal and does not restore deleted OTP compatibility functions.

This is the first behavioral slice, not the entire RFD acceptance suite. Private
atom visibility, ETS/persistent-term/timer boundaries, failure rollback, saturation,
allocation/resource census, 1,000-cycle churn, latency, density, budgets and security
still need separate witnesses. A successful reclamation status alone is not an
independent physical-release measurement. Trusted bytecode only initially.

## Checks available now

Build and test the scaffold (no OTP build required):

```sh
cmake -S libbeam -B /tmp/libbeam-api-build
cmake --build /tmp/libbeam-api-build
ctest --test-dir /tmp/libbeam-api-build --output-on-failure
python3 -B -m unittest discover -s libbeam/tools -p 'test_*.py' -v
```

Compile the fixtures using a trusted external OTP compiler, under the shared OTP
validation lock if other local OTP jobs may be running. Choose a fresh directory:

```sh
out=$(mktemp -d)
mkdir "$out/a" "$out/b"
erlc -Werror -o "$out/a" libbeam/tests/fixtures/two_isolates/a/probe.erl
erlc -Werror -o "$out/b" libbeam/tests/fixtures/two_isolates/b/probe.erl
/tmp/libbeam-api-build/two_isolates "$out/a/probe.beam" "$out/b/probe.beam"
# Expected current result: exit 1, "not implemented: Engine::create".
# A passing scaffold test asserts this failure; it is NOT isolate acceptance.
```

Both variants have been compiled and independently smoke-tested using stock OTP:
module identity/exports, bootstrap, zero state, increment and read. This stock-OTP
smoke does not establish bytecode compatibility with the fork; the actual engine
acceptance run must use the checkout compiler as required by RFD 0002. Evidence:
`/var/folders/v0/6x4x9vzn10gbdxpzdnsfyxwh0000gn/T/libbeam-api-example-m9ez7yj1/summary.json`,
status `fixture_smoke_only_not_isolate_acceptance`. Those separate compiler/test
processes are **fixture checks, not a two-isolate demonstration**. No generated
BEAM files are committed.
