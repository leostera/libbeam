<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 Leandro Ostera <leandro@ostera.io> -->

# Native engine ownership: first state migration

The unchanged acceptance target is [`two_isolates.cpp`](../../libbeam/examples/two_isolates.cpp):
one engine, two concurrently resident private worlds, independent execution, and
physical reclamation/replacement. An engine is not an OTP application world.
This checkpoint starts changing ownership inside `beam/`; it does not complete
Engine initialization or introduce working isolates.

## Concrete changes

[`erl_engine.h`](../../beam/erts/emulator/beam/erl_engine.h) now defines an explicit
native `ErtsEngine`. It owns:

- The startup phase formerly stored in a file-static variable in `erl_init.c`.
- An opaque scheduler-thread handle registry. Its array, count and capacity are
  no longer file-static variables in `erl_process.c`.

Construction uses system allocation before the ERTS allocator exists. Uninitialized
control objects can be discarded symmetrically. Discard rejects prepared/running
objects without freeing them: native initialization allocations and running
threads still have no safe shutdown/reclamation path.

Preparation, diagnostic OTP bootstrap, scheduling initialization and worker launch
receive an explicit engine pointer. Normal and dirty scheduler data acquire their
engine association during construction, before worker launch; debug launch checks
verify that association. These are owner references, not private copies of the VM.
The scheduler arrays/run queues themselves remain global in this slice.

The experimental C diagnostic interface now requires an engine argument for
preparation, startup, phase and inventory queries. There is **no current-engine
getter, TLS-selected engine, macro alias to a singleton, or global-table swap**.

A temporary `claimed_engine` pointer in `erl_init.c` prevents a second attempt to
initialize the unmigrated globals and retains the initialized control object. The
prepared inventory still reads global tables, only for this claimed owner and only
before concurrent execution. It is an explicit migration bridge, not evidence of
private tables. `erl_runtime_is_claimed()` exposes only the admission guard, not
an implicit owner for runtime operations.

Multiple uninitialized control objects are therefore possible. **Multiple
initialized runtimes are not.** That is also not the isolate architecture: isolates
will share an engine, rather than each initializing the engine's native substrate.

## Initial ownership classification

This is a starting ledger, not an exhaustive global-state/effect audit. “Target”
means intended ownership, not an assertion that migration has happened.

| State / source anchors | Target owner | Current migration status |
| --- | --- | --- |
| Startup phase, `erl_init.c` | Engine lifecycle | Moved into `ErtsEngine`; explicit access |
| Retained scheduler/dirty/aux/poll/supervisor handles, `erl_process.c` | Engine scheduler component | Moved into an engine-owned registry; not stop/join |
| `ErtsSchedulerData` | Engine scheduler component | Owner association added; arrays/storage still global |
| Scheduler topology, run queues, sleep/wakeup state, `erl_process.c` | Engine scheduler component | Not moved; do not add per-isolate worker pools |
| Async workers/queues, `erl_async.c`; native system-message dispatcher, `erl_trace.c` | Engine worker components | Not moved; callback work must retain its world owner where applicable |
| Thread-progress participants/deferred callbacks, `erl_thr_progress.c` | Engine coordination | Not moved; deferred references must keep isolate resources alive until safe release |
| Monotonic clock and poll backend, `erl_time_sup.c`, platform poll code | Engine/platform infrastructure | Not moved; clock/poller ownership does not confer ambient guest authority |
| `erts_atom_table`, atom allocation/text/limits, `atom.c` | Isolate namespace; immutable predefined atom vocabulary may be shared | Not moved; dynamic intern state must not become an Engine field |
| `module_tables`, old-code locks, module limits/accounting, `module.c` | Isolate code space | Not moved |
| Export/import/fun entries, code indices, catches/ranges/literal roots; `export.c`, `code_ix.c`, `erl_fun.c`, loader | Isolate code space | Not moved; all lookup/publication/retirement paths need the same owner |
| Registrations, ETS, persistent terms; `register.c`, `erl_db.c`, `erl_bif_persistent.c` | Isolate world | Not moved |
| Processes, heaps, mailboxes, links/monitors, timer entries | Isolate execution state | Not moved; shared physical scheduling/index storage must still preserve explicit immutable world ownership |
| PID/index allocation, global process/port tables | Mixed physical indexing and logical world ownership | Must split responsibilities; globally unique identity is not authority to address another world |
| Allocator arenas/caches versus allocations charged to a world | Mixed engine allocator substrate and isolate ownership/accounting | Must split; reclamation cannot free shared backing still used by another world |
| `init`, boot arguments, initial/system-process roots, Kernel/code-server/application bootstrap | Diagnostic launch request or isolate-local world services | Not Engine configuration merely because the old launcher stored them globally; no implicit OTP boot in Engine creation |
| Immutable opcode/BIF implementations and compile-time tables | Shared immutable implementation data | May remain shared; mutable export targets and namespace metadata may not |
| Ports, file/network/native effects, tracing and host descriptors | Explicit host capability/resource owner, or excluded profile surface | Unresolved/effect audit still required; never inherit ambient access by copying globals into Engine |

Physical storage and semantic ownership are distinct. A shared scheduler or allocator
may serve many isolates; processes, code roots and queued work must retain their
particular isolate owner. Likewise, an Engine struct containing a pointer to all
old globals would not create independent worlds.

## Tests and evidence

On the incremental macOS ARM64 debug-interpreter snapshot, under the shared
validation lock:

- Control-object test allocates/discards 128 pairs. This exercises only control
  allocation/free, **not engine startup or isolate churn**.
- Preparation cases (`-A 0`, `1`, `4`) retain the prior zero-process/port/BEAM-code/
  system-root/new-thread assertions. A second control object remains unclaimed,
  cannot initialize the shared globals, cannot inspect the first object's prepared
  inventory, and can be discarded. The initialized owner refuses discard.
- Actual diagnostic startup verifies that the running engine's retained handles
  are absent from an uninitialized candidate's inventory. The candidate can be
  discarded without stopping the live runtime. Normal/dirty owner assertions run
  in the debug emulator.
- The host runner requires the owner witness exactly once; missing/duplicate
  witnesses are rejected by tooling tests.
- Both CMake variants retain the explicit two-isolate example failure at
  `Engine::create`. No successful Engine/Isolate handle is manufactured.

Evidence paths:

- `/tmp/libbeam-engine-owner-native/summary.json`: preserved initial build failure
  when Makefile regeneration passed an unsupported target argument to the snapshot's
  `config.status`. Full default regeneration fixed the build; no runtime shim added.
- `/tmp/libbeam-engine-owner-native-retry/summary.json`: three native startup trials
  and four PTY cases; real linked emulator and ownership checks.
- `/tmp/libbeam-engine-owner-package/manifest.json`: frozen archive/header/dependencies.
- `/tmp/libbeam-engine-owner-checkpoint/summary.json`: native/scaffold builds,
  five native CTests, one scaffold CTest and 29 tooling tests. Four CMake-linked
  startup trials also pass (three six-handle cases and one eight-handle case).
  Both example builds retain the expected factory failure; native source hashes
  match the development tree.
- `/tmp/libbeam-engine-owner-control/summary.json`: standalone control-object test
  passes plain and UBSan builds with strict warnings. ASan+UBSan execution timed out;
  an empty-main ASan+UBSan control also timed out. **No ASan pass or full-VM sanitizer
  coverage is claimed.**

## Next slice

Introduce isolate-owned code-space state, beginning with atom/module/export/code-index
initialization and explicit loader ownership. Trace those accesses into execution and
attach isolate ownership to processes. Keep the end-to-end example unchanged, with
focused construction/loading tests beneath it.

Do not make migration/destruction of every legacy global or the running diagnostic
OTP world a prerequisite. Engine resources still need safe ownership when exposed
through the public factory, but this is not a reason to defer private namespace work.
No private namespaces, recoverable full initialization, shutdown, restart, unload,
untrusted-bytecode safety or performance acceptance are established here.
