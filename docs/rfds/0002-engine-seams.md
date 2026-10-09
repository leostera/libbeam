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

# RFD 0002: initial engine/library seam findings

**Source-transition notice:** the runtime evidence below belongs to the archived
Realm fork. The current `beam/` starts from a clean upstream snapshot, with only
the startup phase split carried forward as a direct source change. See the
[clean-upstream transition](0002-clean-upstream.md) for current evidence;
historical success is not carried over.

Status: **partial P0-02 investigation and internal startup refactoring, not an
implemented embedding lifecycle**. Initial source inspection was at
`8a9327f8788e2766fe737e79082f04c6b0f15d81`. The follow-up extracts internal startup
phases from `erl_start`, preserving their bodies and order. It does not make global
initialization reentrant, start an empty engine, or change shutdown semantics.

## 1. OTP already produces an emulator archive

[`erts/emulator/Makefile.in`](../../beam/erts/emulator/Makefile.in) defines
`EMULATOR_LIB = libbeam.a` on Unix and includes that target in `all`.
The archive contains `PRELOAD_OBJ` and `OBJS`, whereas the executable adds
`erl_main.o` via `INIT_OBJS`. The archive recipe does not include `DEPLIBS` inside
the archive: the host still needs the configured dependency libraries and linker
flags. Guessing those from a few common Linux libraries is not portable.

[`HOWTO/INSTALL-IOS.md`](../../beam/HOWTO/INSTALL-IOS.md) already describes static
packaging and a native wrapper calling `erl_start(argc, argv)`. Therefore creating
an archive is not the novel part of this project. Fresh independent instances and
host-safe initialization/shutdown are the missing contracts.

The archive filename does not distinguish interpreter/JIT or debug/optimized builds.
The recipe uses `ar rc`/`rcv`, which can retain members not replaced by a subsequent
variant. Before using an archive as embedding evidence, rebuild it from scratch
with the desired generated Makefile, `TYPE` and `FLAVOR`, record those settings and
copy it into a variant-qualified artifact. Do not confuse the last-built archive
with whichever executable happens to be selected by `erl`.

`libbeam/examples/archive_link_probe.cpp` is deliberately only a packaging witness:
it references the existing `erl_start` symbol through a volatile function pointer,
links the real archive and returns normally without calling that function. The
runner checks that the linked executable defines `erl_start`; it does not accept
an optimized-away reference. A successful run says **linked, not initialized**.
It cannot satisfy P0-03's engine-create/engine-destroy test or the two-isolate proof.

## 2. Startup currently combines engine and world construction

Observed call chain:

```text
sys/unix/erl_main.c:main
    sys_init_signal_stack()
    erl_start(argc, argv)
        early initialization and process-wide CLI configuration
        erl_init(...)
            scheduler/process/time/allocator support
            code indices, atoms, exports, module table, registry
            ETS, distribution, drivers, async I/O, native built-ins
        start_otp_world(init, boot_argc, boot_argv)
            load_preloaded()
            publish staging code index
            create ordinary OTP init process
            create global system processes
        start_runtime_threads()
            erts_start_schedulers()
            lock-count post-startup hook, when enabled
        erts_sys_main_thread()  // source explicitly says it may not return
```

Sources: [`erl_main.c`](../../beam/erts/emulator/sys/unix/erl_main.c) and
[`erl_init.c`](../../beam/erts/emulator/beam/erl_init.c), particularly `erl_init`,
`erl_start`, `load_preloaded` and the final startup sequence.

`erl_init` currently calls `init_dist`, `erts_ddll_init`, `erts_init_io` and
`erl_nif_init` along with the language runtime's required pieces. A reduced profile
cannot be implemented by merely omitting the `file` module from a bundle. These
initialization dependencies must be classified, removed where unnecessary, and
blocked at effectful entries where retained engine support is not tenant authority.

Startup creates global `erts_code_purger`, `erts_literal_area_collector`, dirty
process signal handlers and `erts_trace_cleaner`. The source explicitly treats their
termination as bringing down the whole VM. They cannot simply be copied into each
isolate and then terminated during eviction with unchanged failure semantics.
Decide which housekeeping is engine-wide with retained isolate provenance and which
is isolate-local; preserve correct code/literal reclamation in either design.

The extracted functions are **file-local**, not exported embedding entry points.
The existing `erl_init` still mixes shared support with world-specific initialization
and may already start native support threads; `start_runtime_threads` names the late
scheduler/auxiliary/poll launch phase, not every thread creation in the runtime.
Skipping `start_otp_world` is not currently supported or tested.

Three concrete lifecycle blockers are now tied to their implementing functions:

| Source function | Existing behavior | Embedding implication |
| --- | --- | --- |
| `erl_process.c:erts_start_schedulers` | Sets `opts.detached = 1`; launches normal/dirty schedulers, auxiliary/poll threads and optional run-queue supervision | A host destructor cannot join these threads as currently created; lifecycle work must cover each family, not just normal schedulers |
| `erl_process.c:erts_do_exit_process` | A process with `ERTS_STC_FLG_SYSTEM_PROC` calls `erts_exit(ERTS_DUMP_EXIT, ...)` on termination | Ordinary system-process exit cannot be used as isolate/engine cleanup |
| `sys/unix/sys.c:erts_sys_main_thread` | Initializes Darwin main-thread pipes, notifies signal setup, then waits; Darwin also supports wx/Cocoa main-thread stealing | Returning before this call would omit signal setup, not merely remove a blocking loop; embedding needs explicit signal ownership and no wx dependency |

The system-process regression fixture checks the existing six housekeeping processes:
one high-priority code purger, one high-priority literal-area collector, three dirty
signal handlers (normal/high/max), and one normal-priority trace cleaner. All have
off-heap message queues and system-process flags. It separately checks OTP init and
ordinary spawn/monitor progress. This protects standalone behavior while the startup
boundary changes; these are not six proposed per-isolate workers.

## 3. Shutdown is process termination, not an object destructor

In `erl_init.c`, `system_cleanup` coordinates one exiting thread and can make other
threads wait forever. It flushes async work and waits for NIF halt functionality.
`erts_exit_epilogue` eventually calls `exit`, `_exit`, `abort` or dump-exit logic;
its declarations are non-returning. This is not an engine destructor that can be
wrapped in a C++ RAII object.

Required separation:

1. Keep standalone `erl`'s CLI/process-exit behavior as a frontend policy.
2. Give library creation explicit success/failure/unwind instead of CLI usage/exit.
3. Introduce isolate stop/drain/reclaim without triggering global system-process
   death semantics or engine termination.
4. Implement engine shutdown as scheduler/async quiescence, joins and ownership
   release, returning to the host after all isolates/handles are released.
5. Retain an explicit process-fatal category for internal corruption/fatal OOM.
   Do not turn every fatal path into a recoverable error without a safety argument.

No change to `erts_exit`'s `noreturn` contract should be made in isolation. Auditing
its callers and platform signal/main-thread assumptions is prerequisite work.

## 4. First concrete world-global state anchors

| Source | Observed state | Required investigation |
| --- | --- | --- |
| [`atom.c`](../../beam/erts/emulator/beam/atom.c) | `erts_atom_table`; global insert/lookup/index operations | Dynamic atom namespace/lifetime, preloaded atom representation, terms and code embedding atom IDs |
| [`module.c`](../../beam/erts/emulator/beam/module.c) | `module_tables[ERTS_NUM_CODE_IX]` | Per-isolate code ownership and loading, not merely several global publication slots |
| [`code_ix.c`](../../beam/erts/emulator/beam/code_ix.c) | Active/staging indices, code permissions and deferred barriers | Publication scoped to a code world while physical thread progress may remain shared |
| [`register.c`](../../beam/erts/emulator/beam/register.c) | Static `process_reg` hash used for registration/lookups/enumeration | Local registry, including late-bound timer/name resolution |
| [`erl_init.c`](../../beam/erts/emulator/beam/erl_init.c) | Init PID and global housekeeping process pointers | Minimal bootstrap without mandatory full OTP node; housekeeping lifetime/provenance |
| [`erl_bif_persistent.c`](../../beam/erts/emulator/beam/erl_bif_persistent.c) | Global persistent-term table and reclamation | P0 requires actual local state; the historical restricted-profile denial does not satisfy it |

This is a starting map, not an exhaustive list. Process storage, run queues, funs,
imports/JIT dispatch, ETS, timer producers, allocator caches, native resources,
signals and every relevant deferred operation still need explicit owner and cleanup
records. The existing 570-BIF/180-NIF declaration inventory and native source scanner
remain unapproved discovery aids, not the new positive-list profile manifest.

## 5. Next implementation boundary

After the relocated build/test baseline, finish P0-02's initialization and effect
map before claiming a public `Engine::create()` implementation. Reuse OTP's actual
archive/link machinery for packaging; do not present the archive-link probe as an
engine. The next lifecycle witness must initialize an engine with **zero tenant
isolates**, return to host code, shut down, and return to host code again. Only then
should it become the base for fresh context creation and conflicting-MFA execution.

See [RFD 0002](0002-libbeam-isolates.md) for the still-open P0-02/P0-03 contracts.

## 6. Startup-phase refactor evidence

The extracted world-construction and thread-launch bodies compare byte-for-byte
with their previous bodies after trimming boundary whitespace. Their order and the
standalone main-thread handoff remain unchanged. No public symbol, ownership rule,
shutdown path or reduced-profile enforcement is added.

The configured detached worktree from P0-01 was advanced to
`320a5ac3b7de5971ebaccf5dcdfd454a11b2f49b` and the `erl_init.c` patch applied before
starting validation. This is an incremental, patch-bound development run, not a
second clean build or hosted matrix. Generated preload changes remain uncommitted.
Both variants were rebuilt and identity-probed by the existing validation runner:

| Variant | Focused regressions | Startup fixture |
| --- | --- | --- |
| optimized JIT | 148 passed, 0 failed/skipped | 3 fresh VMs passed |
| debug interpreter | 148 passed, 0 failed/skipped | 3 fresh VMs passed |

The startup fixture runs 20 spawn/monitor cycles per VM in addition to its init,
system-process membership, priority and queue assertions. Thus there are 296 focused
test executions and 6 startup runs, not 296 distinct cases. The 53 existing tooling
and 7 build/link-tool tests also pass. Inventory checks remain classification, not
policy approval; the BIF inventory's moved source line is refreshed.

The fixture is [`startup_probe.erl`](../../libbeam/tests/fixtures/startup_probe.erl).
After rebuilding the configured source, it can be run separately (serialize with
other OTP validation, and clear inherited `ERL_*` flags):

```sh
OTP=/tmp/libbeam-p0-01/source/beam
OUT=$(mktemp -d)
"$OTP/bin/erlc" -o "$OUT" libbeam/tests/fixtures/startup_probe.erl
"$OTP/bin/erl" -emu_type debug -emu_flavor emu -noshell -pa "$OUT" \
  -eval 'R=startup_probe:run(),debug=maps:get(build_type,R),emu=maps:get(flavor,R),io:format("~p~n",[R]),halt().'
```

Repeat with `opt`/`jit` in both the flags and assertions for optimized JIT. The local
experiment used a user-wide validation lock, fresh log directory, 60-second process-
group timeouts, strict result checks and recorded source/fixture identities. This
fixture is not yet part of the hosted matrix's acceptance schema.

| Evidence | SHA-256 |
| --- | --- |
| `/tmp/libbeam-startup-seam/regressions/summary.json` | `85e51461500add7eeacf53ca12af4b7aee62295c164727d5bcd43f13171460a8` |
| `/tmp/libbeam-startup-seam/startup/summary.json` | `0fc1571e155a47c1dc79aba3c74d27eceb1d8d372156510fbe7440531b16f4f0` |
| Tested `erl_init.c` (matches development source) | `0a2b3be714ce900f535c8b028da049bc4b8a014c6c04ee5f0918cca4e6a6d351` |
| Startup fixture | `a000c01a64384d9fe752a767e383751617e23595e740eadabc9db9e386711dd1` |

No empty-engine, host-return, teardown, isolate, latency or security acceptance is
inferred. In particular, launch threads are still detached and normal system-process
termination still kills the VM; the refactor does not repair either blocker.
