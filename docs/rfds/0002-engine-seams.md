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

Status: **source investigation, not a complete P0-02 ownership map or implemented
embedding lifecycle**. Source inspected at `8a9327f8788e2766fe737e79082f04c6b0f15d81`;
none of the emulator files discussed here changed in this investigation.

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
        load_preloaded()
        publish staging code index
        create ordinary OTP init process
        create global system processes
        start scheduler threads
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
