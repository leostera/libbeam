<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 Leandro Ostera <leandro@ostera.io> -->

# Additive runtime inventory and implementation order

Initial inventory baseline: **`8d7a4a67`**; updated with the
[A01/A02 term-and-atom slice](0003-terms-and-atoms.md) and the
[A03–A07 loader checkpoint](0003-loader-program.md) and
[first selected-profile execution](0003-first-execution.md) and
[owned ordinary binaries](0003-owned-binaries.md) and
[C Engine lifetime](0003-engine-lifecycle.md) and
[owned worker execution](0003-owned-executor.md). Direction: [RFD 0003](0003-additive-runtime-construction.md).
First transplant: [owned BEAM images](0003-first-loader-slice.md).

This is a source-grounded dependency and acceptance inventory, not a declaration-
by-declaration audit of all OTP or a claim that these boundaries are already
independent libraries. The next implementation pass should follow the actual
execution path through this graph, not implement every row as a disconnected
container. New runtime implementation stays in C; language translation is not a
parallel objective.

## 1. What exists, and what does not

| Item | Actual additive-core status |
|---|---|
| Bootstrap allocation domain | Implemented, fallible, exact-base release, busy refusal, owner-local failure injection |
| Owned BEAM image | Implemented structural preparation: copied bytes, UTF-8 file atom names, import/export metadata, code header, opaque chunks |
| Runtime atom identities / Erlang terms | Selected 64-bit representation, private namespaces/bindings and provisional code-load transactions implemented; broader term operations pending |
| Opcode generation / operand decoding | Pinned full generation; metadata, whole selected transform cases and generated dispatch bodies now consumed by real execution |
| Literal / type / lambda preparation | Admitted literals have runtime/GC consumers; attributes/compile metadata decoded for execution; ordinary immutable offheap binaries now loaded/copied/collected; maps/funs and runtime lambda registration pending |
| Transformation / executable code | Selected profile transforms, emits, eagerly links, publishes and physically retires real BEAM words; full helper/fixup/consumer breadth pending |
| Process context / heap / GC / interpreter execution | Explicit C context, bounded generated interpreter, selected full-copy GC and two real module-info BIFs; no scheduler/process identities or broad exception handling |
| C Engine / Isolate construction and retirement | Engine owns allocation/native substrate and lazy worker; private C worlds own code/calls and physically reclaim admitted storage; stateful process machinery remains outstanding |
| Public C++ API | Engine/Isolate/Call/Reclamation connected to C; real selected-profile worker execution, copied transport, capacity, timeout/retry and once-only results |
| Engine lifetime / two-Isolate acceptance | Unchanged G1 passes; public selected execution passes; G3 still refuses unsupported stateful module admission |

The older `beam/` ownership migration is **research and transplant material**, not
additional completed rows in this table. Its [inventory](0002-current-ownership-inventory.md)
remains useful for finding consumers and retirement obligations. Earlier uncommitted
lifecycle work is preserved and is not being continued by this inventory pass.

## 2. Executable gates

| Gate | Witness | What success must mean |
|---|---|---|
| G0 — image preparation | `core_beam_image_test`, fresh compiler fixture | Owned structural metadata and balanced failure/release; already demonstrated, not execution |
| G1 — actual Engine lifetime — **selected substrate demonstrated** | Unchanged `engine_lifecycle.cpp`, plus failure injection | Real shared execution infrastructure, no implicit world; create/shutdown/create and failed-init/retry without retained singleton or host exit |
| G2 — first execution | `first_slice.erl`, `core_code_test`, `core_world_test`, `api_world_test`; [public selected-profile witness](0003-public-isolates.md) demonstrated | Execute real BEAM through transplanted decoding/emission/interpreter machinery; explicit contexts, ordinary terms, collection, errors and complete teardown |
| G3 — independent worlds | Unchanged `two_isolates.cpp` and its existing fixtures | Same-name modules/registrations coexist; persistent processes; one world physically reclaimed while peer runs; replacement and clean Engine shutdown |
| G4 — supported profile | Explicit language/BIF/service conformance suite | Deliberate supported behavior and fail-closed excluded effects, beyond the example |
| Separate production gates | Security, quotas, performance, density, suspend/resume, platform/JIT coverage | No inference from G0–G3; evidence and decisions required separately |

G1 must not be made green by publishing the allocation-domain handle as an Engine.
G2's dependencies should determine the shared infrastructure G1 constructs. Internal
component tests may precede those gates, but are not substitutes for them.

## 3. Fixture-derived requirements

Fresh stock-OTP compilation/disassembly was performed under the validation lock.
Evidence and source hashes:
`/var/folders/v0/6x4x9vzn10gbdxpzdnsfyxwh0000gn/T/libbeam-additive-inventory-wt2vsa5l/`.
Reference: **OTP 28 / ERTS 16.3**, not a fresh OTP 30 compiler build. Both existing
`probe` variants were inspected. This is dependency evidence, not libbeam execution.

### G2: the first execution fixture

| Function | Observed families / consequences |
|---|---|
| `value/0`, `identity/1` | Labels, function/line metadata, move, return; real entry/continuation and register semantics |
| `pair/1` | `test_heap`, `put_tuple2`; heap roots and a collection path, not just decoding an integer |
| `unicode/0` | Atom-valued operands; UTF-8 names must become actual namespace-owned identities |
| `literal/0` | Literal-valued move; ETF/literal decoding, compressed literal storage and off-heap ownership cannot remain opaque |
| Generated `module_info/0,1` | External tail calls importing `erlang:get_module_info/1,2`, even in the smallest normal compiled module |

Disassembly names are **families**, not the complete encoded opcode/operand
inventory: `beam_disasm` normalizes some instructions. Generic-to-specific
transformations can introduce fused instructions absent from this list. The
upstream generator and transformation tables, not a handwritten list, remain the
source of truth for opcode number/arity/semantics.

A first internal return-only witness can precede tuple/literal execution, but the
next pass must not declare G2 complete at that intermediate point. It must also
exercise collection and retirement rather than relying on a generous fixed heap.

### G3: the unchanged stateful fixture is substantially larger

Observed imports (same interface for A and B):

- `erlang:'+'/2`, `integer_to_binary/1`.
- `self/0`, `make_ref/0`, `register/2`.
- `spawn_monitor/1`, `demonitor/2`, `exit/2`, `error/1`.
- Compiler-generated `get_module_info/1,2`.

Observed families additionally include stack allocation/initialization/trim,
local/external calls and tail calls, `make_fun3`, GC-aware BIF calls, tuple tests and
selection, binary match/construction, send, selective receive, receive markers,
message removal, wait, timeout, and badmatch. Some required behavior is implemented
by instructions, not imported BIFs.

Consequences: closure environments, persistent child processes, PID/ref identity,
registered-name routing, monitors and DOWN/flush behavior, selective mailboxes,
monotonic timers, binary lifetimes, integer formatting, exception unwinding and
fair scheduling are **required for G3**, not optional OTP services. No ETS, ports,
NIFs, distribution, files, `on_load`, application controller or code-server process
is requested by this fixture. Do not bootstrap those to obtain unrelated helpers.

## 4. Required dependency clusters

Source anchors below are relative to `beam/erts/emulator/beam/` except where noted.
All rows are outstanding in the additive implementation unless explicitly marked.
“Owner” includes borrowers that must be retired, not just where a struct lives.

### A. First real execution and Engine foundation

| ID / cluster | Source anchors and dependencies | Owner / lifetime obligation | Gate and evidence |
|---|---|---|---|
| A01 Platform/representation contract — **selected 64-bit boundary implemented** | `erl_vm.h`, `erl_term.h`, selected `sys.h` definitions; word width, endian, alignment, tags | Immutable definitions; no wholesale platform pre-init | G2: layout/tag boundaries and checked arithmetic, no alternate term representation |
| A02 Term operations and atoms — **atoms/basic-term slice implemented; broader term operations pending** | `erl_term.*`, `atom.*`, existing namespace adaptations; predefined identities, NIL, smalls, tuples/lists | Isolate atom identity/storage; explicit context; no interning into old global table | G2: atom operands and tuple roots; G3: equal spellings in independent worlds |
| A03 Opcode generation — **generated metadata, selected transforms and dispatch integrated** | `../utils/beam_makeops`, compiler `genop.tab` (full path below); common and `emu/` predicate/generator/ops/instruction tables; Makefile generation rules | Shared immutable generated implementation data; reproducible input record | G2: derive decoder/transform/emitter/dispatch from matching inputs, no opcode renumbering or mock loop |
| A04 Full operand reader / temporary operations — **owned decoding path implemented with explicit limits** | `beam_file.c:beamcodereader_read_next`, tagged-number reader, integer/allocation-list marshalling, `BeamOpAllocator`, function-boundary synthesis | Prepared-module owner; fallible op/argument growth; drop temporary operations on every failure | G2: real compact encodings, signs/bignums, registers, labels, extended lists/type hints/literal indices and malformed boundaries |
| A05 Runtime literal/type/lambda/line data — **supported literals now executed/collected; ordinary immutable offheap binaries integrated; writable/fun/map/line breadth pending** | Remaining `beam_file.c` parsers, `external.c`, `big.*`, `beam_types.*`; optional compressed payload helpers | Isolate/module literal storage and off-heap resources; validation before publication | G2: actual literal fixture; G3: lambda metadata; explicit treatment of optional type/line/debug chunks |
| A06 Transform and emit — **selected whole cases, word emission and fixups execute; broader helpers/consumers pending** | `beam_load.c:load_code`, `emu/emu_load.c`, `emu/load.h`, generated transformation helpers | Unpublished prepared-code transaction; labels, imports, strings, literals, catch/fun patches; exact allocation bases | G2: resolved code and failure at each preparation/fixup stage; never invoke whole-world preload |
| A07 Code publication and entry resolution — **real selected-profile publication, entry/frame/import retention and retirement; fun/catch consumers pending** | `module.*`, `export.*`, `beam_code.h`, `code_ix.*`, catch/range/fun metadata and prior code-space adaptations | Isolate-owned namespace; code remains retained through frames, closures and literals | G2: resolve own entry; G3: conflicting modules coexist and retirement releases references |
| A08 Process execution context — **bounded context, local calls/private-entry yields and saved Y roots implemented; full process machinery pending** | `erl_process.h`, relevant `erl_process.c`, `erl_message.h`; X/Y registers, stack, continuation, reductions, exception roots | Isolate-owned process; Engine execution context borrows it; no scheduler-data TLS tenant selector | G2: bounded entry/run/yield/return using real context, no global process-table initialization |
| A09 Heap, GC and off-heap cleanup — **selected full-copy collector plus real binary reference sweep; message/fun/other-offheap breadth pending** | `erl_gc.*`, term-copy helpers, `erl_binary.*`, `erl_message.*:erts_factory_*` where needed | Process heap/stack, message fragments, module literals and binary refs with explicit roots | G2: forced collection preserving arguments/tuples/literals and exact cleanup; failure at heap growth |
| A10 Interpreter boundary — **69 selected generated NO_JUMP_TABLE cases including local calls/Y moves execute; instruction breadth pending** | `emu/beam_emu.c:process_main`, generated hot/warm/cold code, `emu/instrs.tab`, `emu/macros.tab` | Engine execution machinery; explicit process/Isolate context; caller sees bounded return/yield/error | G2: transplanted execution, no `erts_schedule`/OTP-bootstrap dependency smuggled in; cancellation-safe points |
| A11 Builtin dispatch and errors — **real get_module_info/1,2 and error roots; broader builtins/unwind pending** | `bif.*`, BIF instruction tables, `error.h`, exception/stacktrace paths | Shared approved implementations; Isolate-local terms/exports/traps; invocation-owned results | G2: explicit supported/unsupported imports, exceptions and cleanup, not fatal host exit |
| A12 C lifecycle + C++ adapter — **selected-profile Engine/Isolate/call/reclamation implemented; stateful child/drain integration pending** | New C Engine/Isolate ownership graph, `libbeam/src/engine.cpp`, existing API | Engine shared resources; Isolate children; handle/tombstone storage distinct from reclaimed VM state | G1/G2: real factory, same rollback/destruction operations, repeat creation, no hidden process-lifetime roots |

A03's compiler input path is `beam/lib/compiler/src/genop.tab`; the generator is
`beam/erts/emulator/utils/beam_makeops`. List actual files explicitly when admitting
this cluster instead of copying the old Makefile wholesale.

### B. Stateful independent worlds and the host contract

| ID / cluster | Source anchors and dependencies | Owner / lifetime obligation | Gate and evidence |
|---|---|---|---|
| B01 Execution scheduling and wakeup — **lazy shared worker executes retained contexts; full process scheduling pending** | Relevant `erl_process.c` queues/reductions, interpreter yield boundary; narrow host thread/wait primitives if needed | Engine driver/workers, Isolate process queues; queued work retains users until drained | G3: both admitted calls progress regardless of wait order; peer progresses during stop; stop/join without ethread global startup |
| B02 Process identity and spawn/exit | `erl_process.*`, `erl_ptab.*`, spawn BIF paths | Isolate process directory and generations; explicit destination/owner checks | G3: ordinary child process outlives boot call; no cross-world PID resolution or recycled-handle alias |
| B03 Funs and environments | `erl_fun.*`, lambda parser, `make_fun3`/call paths | Closure process/heap owner plus retained defining module/code | G3: captured Parent in spawned fun; free closure/code only after last physical reference |
| B04 References, links, monitors and signals | `erl_bif_unique.*`, `erl_monitor_link.*`, `erl_proc_sig_queue.*` | Identity generation can be shared; resolution/relationships/payloads are isolate-private | G3: `spawn_monitor`, DOWN, `demonitor(...,[flush])`, exit/error cleanup; no foreign-world resolution |
| B05 Mailboxes and selective receive | `erl_message.*`, receive instructions in `emu/msg_instrs.tab`, signal handling | Destination process/Isolate owns queued messages and fragments; binary/code refs retained | G3: order, selective scan, receive markers, send-to-dead behavior, drain on stop |
| B06 Registered names | `register.*` and previous explicit registry adaptation | Isolate registry; entries removed on physical process/port retirement as applicable | G3: both `probe_state` registrations coexist; replacement does not inherit entries |
| B07 Timers and waits | Timer wheel/high-level timer helpers, receive timeout instructions; narrow monotonic clock adapter | Engine clock/wakeup execution, process/Isolate timer registrations and payloads | G3: five-second receive deadlines, cancellation/expiry races, no callbacks to reclaimed worlds |
| B08 Binary/numeric/BIF slice — **ordinary binary storage and copied-byte invocation integrated; bit-syntax/numeric/BIF breadth pending** | `erl_binary.*`, `erl_bits.*`, binary instruction tables; integer arithmetic/formatting helpers | Process/module/message binary refs, heap terms and scratch storage; shared pure implementation | G3: input comparisons, binary construction, count increment and integer conversion; expand numeric semantics deliberately, never silently wrap |
| B09 Host invocation/completion — **selected-profile copied transport, limits, deadlines and once-only results implemented** | Existing `Call`/`Reclamation` API; new C admission/completion state | Copied input/MFA, ordinary invocation process, reserved terminal capacity, host-owned result | G3: 64 KiB payload/result, 64 outstanding, 1 MiB queued payload; once-only completion; timeout is NOT cancellation |
| B10 World retirement — **selected code/process world physically reclaimed; future spawned-process/signal/timer/mailbox drain pending** | Admission, scheduler, processes/signals, timers, registry, messages, code, literals and allocator domains above | Quiesce/drain each retaining user, then destroy children; preserve closed generation-stable tombstones | G3: A reclaimed while B continues; replacement fresh; late work cannot reach freed state |
| B11 Engine shutdown and exceptional paths — **worker join and selected-world retirement implemented; ownership checks serialized with worker retirement** | C lifecycle coordinator plus C++ move/destructor/error paths | Join/drain shared users before final release; handles/children enforce busy; no force-free or host termination | G1/G3: repeated lifetime, busy refusal, dropping handles, errors/timeouts, partial start and failed publication recovery |

The image parser's 64 MiB cap and the example host's 8 MiB fixture-read limit are
not the invocation limits above and are not a VM memory quota. Preserve those
separate meanings.

## 5. Decision register before importing dependent code

| Decision | Constraint / recommended next action |
|---|---|
| Generated dispatch initialization | `process_main` currently also initializes dispatch state and enters `erts_schedule`. Separate that setup from scheduling and inventory any mutable instruction state; do not import its startup loop unchanged. |
| Unsupported functions in an otherwise readable module | Decide publication/link policy explicitly. Normal compiler output includes `module_info`; no success-shaped BIF stubs. Either implement admitted behavior or retain an explicit unsupported/unresolved-call failure path consistent with the profile. Do not quietly assume unused functions do not need validation. |
| Optional chunks | Image preservation is not interpretation. Specify ignore/fallback/reject/decode per type/line/debug/lambda/record/literal chunk before code preparation consumes it. Type-hinted operands require the actual upstream fallback rules. |
| Literal compression and ETF | Decide the approved decompressor dependency and resource bounds. ETF parsing must not accidentally admit native resources, foreign PIDs/funs or other excluded authority. Structural image success is not permission to execute. |
| First execution driver | Prefer a bounded explicit-context executor entry usable by the eventual scheduler. A deterministic internal test driver is not a second VM implementation or an excuse to change the public asynchronous contract. |
| Scheduling for G3 | Select host-driven progress or owned joinable workers against actual wait/progress guarantees. Do not inherit OTP's async/dirty/poll/thread-library families merely to get a queue. All implemented workers need rollback, stop and join. |
| Runtime allocator strategy | Bootstrap domains support construction, not a new GC or a performant term allocator. Admit actual heap/GC backing required by execution; defer carrier pools/mappers unless the selected path needs them. |
| Reclamation synchronization | Prove which execution/queue/GC users can still hold each object. Use the necessary leases/barriers/drain protocol; do not import global thread-progress initialization just because a transplanted helper calls it. No post-start code loading simplifies G3, but does not eliminate process/message/literal lifetime obligations. |
| Handle destruction / failure cleanup | Define what retains the Engine when handles are dropped out of order. Test exceptional paths, not only the example's orderly stop/shutdown sequence. Retention must be owned and finite, not delegated to process exit. |
| Thread safety | Initial control-thread serialization is not worker quiescence. State exact admission, borrower and publication rules before adding concurrent execution. |
| Compiler/version baseline | Current evidence uses OTP 28 against a port based on OTP 30 source. Record supported format/opcode expectations and add the pinned compiler witness; do not represent the reference disassembler as a validator for our runtime. |

## 6. Deliberately outside the first execution/example closure

| Area | Disposition |
|---|---|
| JIT / executable-memory management | Defer; interpreter first. Revisit W^X, code retirement, unwind metadata and platform coverage as its own admission. |
| ETS and persistent terms | Not needed by current fixtures. Later isolate-owned state with continuation/deferred-delete lifetime, not shared global caches. Prior experiments are reference only. |
| Hot loading, old code, purge, on-load execution | No requirement to support post-start loading in the current API. Still retire loaded code safely on stop. Reload/purge/on-load need separate protocols before admission. |
| OTP applications/services | No implicit application controller, code server, logger, file server, root node or init process. Admit specific services only for an explicit supported use case. |
| Ports, filesystem, sockets, DNS, distribution, OS process spawning, signals/TTY administration | Exclude ambient authority from this profile. Unsupported operations fail before effects. Host file reads in fixtures grant no guest authority. |
| NIFs/drivers/dynamic native loading | Tenant-native extensions excluded. Pure required builtin implementation is not permission to expose a native-extension loader. |
| Tracing/profiling/debugger/crash dumps | Keep unsupported paths from reintroducing global service roots or host takeover. Minimal bounded host diagnostics are a separate requirement. |
| Broader language/BIF coverage | Maps, float/bignum breadth, complex bit syntax, timers/APIs and other families need explicit conformance as admitted. A finite fixture pass is not complete Erlang compatibility. Some helpers may become dependencies earlier; record that promotion. |
| Security and resource enforcement | Positive-listed effects and structurally safe parsing are necessary but insufficient. Isolation, heap/CPU budgets, interruption, adversarial inputs and native-surface audits remain dedicated gates. |
| Performance, density, suspend/resume, additional platforms | Separate gates after correctness/lifetime foundations; no performance claim from component-test timing. |

## 7. Recommended next long implementation pass

**Target the A-cluster as a coherent first-execution path, not another standalone
ownership-inventory sweep.**

1. Preserve the connected first-execution path: all `first_slice` functions now
   run through real transformation/emission, private publication and generated
   dispatch. Do not replace it with a generic-operation interpreter or legacy fallback.
2. Complete remaining A05–A07 breadth alongside real execution consumers: maps,
   closure and catch state, writable binaries, missing generated helpers and required
   fixups. Every constructor gets failure injection and physical rollback before
   admission. Unsupported imports/functions currently reject the entire module.
3. Extend A08–A11 from the selected roots/GC/builtin/dispatch profile, preserving
   collection at host safepoints, actual frame/literal retention and bounded progress.
   Add the pinned compiler witness and explicit conformance; G2 is not all Erlang.
4. Preserve the demonstrated G1 and selected-profile public Isolate/call/reclamation
   ownership (A12). Extend resources only with real consumers and failure/retirement
   paths; keep copied results independent from physically reclaimed VM storage.
5. Admit the remaining B-cluster semantics and drive the unchanged G3 example.
   [Local calls and saved Y roots](0003-local-calls.md) now have actual private-entry
   reduction, native-GC, failure and public worker consumers; process identities,
   closure/message/signal/registry/timer consumers remain outstanding.
   After M0–M4, attempt a complete pure-Elixir application via the Midgard project;
   compiler success and actual Isolate execution remain distinct witnesses.

A rows form dependency cycles: decoded literals are terms, code owns literals,
GC needs code/literal roots, and interpreter paths need process and builtin state.
Work across those boundaries in one integration slice. If the pass stops at a
reader or emitter checkpoint, report that honestly as partial—not first execution.

## 8. Definition of done for each admitted cluster

- Provenance, licenses and explicit build inputs; avoid unrelated language rewrites.
- Actual runtime accesses use the declared owner, not a parallel mock container.
- Every allocation failure unwinds its completed prefix; a later attempt succeeds
  in the same host with preexisting peers intact.
- No fatal allocator/loader/user-error path or inherited once-only initialization
  is accepted merely because the success fixture does not exercise it.
- Real resource balance and safe retirement for both unpublished and published
  users; stop acknowledgement and thread handles are not reclamation/join evidence.
- Fresh serialized builds, source hashes, retained failures and reference semantics;
  compiler-produced fixtures stay outside source control.
- Examples remain unchanged. Component tests, reference OTP execution, UBSan and
  symbol inspection remain distinct evidence, not substitutes for runtime acceptance.

This document is the work inventory. Update statuses against executable evidence;
do not mark entire OTP files/subsystems complete merely because part was extracted.
