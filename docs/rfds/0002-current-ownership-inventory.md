# Current Engine/isolate ownership inventory

Runtime baseline: **`921fc4ad`**, including the ownership-tree pass (`27ff6518`)
and constant-arena/persistent-term integration (`921fc4ad`). This is the current
subsystem inventory; earlier RFDs retain their checkpoint-specific evidence.
It is not an exhaustive declaration-by-declaration or platform audit.

**Ownership migration is not lifecycle acceptance.** A container can own its
storage while execution references, deferred work, shutdown or restart remain
unfinished. Namespace containers are not yet runnable public Isolates.

## Changes since the previous inventory

| Item | Updated status | Still required |
|---|---|---|
| Engine constants | **Arena storage and runtime allocation path moved.** `ErtsEngine.global_literals` owns chunks, lock, construction state and accounting; original debug-aligned allocation bases are retained. | Retire bound consumers and clear borrowed constant views during Engine teardown. Verify namespace-independent atom identities. Carrier unmapping and recoverable OOM are not provided by unpublished disposal. |
| Persistent terms | **Store, BIF access paths and deferred ownership moved.** Namespace-owned table, update permission/waiters, fast-update state, later operation, deletion queue and locks. Native getter takes an owner; traps check provenance. | Running store retirement, draining through the shared collector, diagnostic trap/export/dump/debug boundaries and actual private-world execution. |
| Persistent-term lifetime | Permission, queued deletion and magic-binary contexts retain the namespace through their respective cleanup; context leases survive physical binary free. Existing literal-area leases remain. | Integrate this protocol into isolate stop/reclamation rather than relying on diagnostic retention. |
| Lifecycle acceptance target | **Added** `libbeam/examples/engine_lifecycle.cpp`: create → shutdown → destroy handle → create → shutdown. Built in native and scaffold configurations. | Still exits 1 at first creation. Initialization-failure recovery also remains unimplemented/unproven. |

Persistent-term storage must therefore no longer be listed as a wholly unmigrated
global subsystem. Its remaining work is retirement/integration and residual
boundaries, not re-extracting the table and queues.

## Engine infrastructure

Paths below are under `beam/erts/emulator/` unless otherwise stated.

| Subsystem / source anchors | Ownership already present | Remaining work |
|---|---|---|
| Lifecycle / `beam/erl_engine.*`, `beam/erl_init.c` | Engine control, startup phase, namespace membership, diagnostic child reference and namespace admission closure. | Shared/world initialization split, transactional rollback, complete destruction and safe reset of `claimed_engine`. Admission closure is not process stop. |
| Scheduling / `beam/erl_process.c` | Scheduler-family handles and scheduler Engine affinity; major backing allocations recorded by allocator ownership. | Scheduler domain for `schdlr_sspnd`, `balance_info`, run queues, scheduler/sleep arrays, supervision, aux state and configuration; stop/join/drain. Queued isolate work must retain its owner. |
| Async / `beam/erl_async.c` | Existing startup/worker integration. | Replace the implicit `async` root with complete Engine pool/queue ownership, job lifetime accounting and stop/join/disposal. |
| Allocators / `beam/erl_alloc.c`, `beam/erl_alloc_util.c`, `sys/common/erl_mseg.c`, `erl_mmap.c` | Domain-owned dispatch, strategy/instance backing, pool roots, segment/mapper controls, TLS and allocation-base ledgers. | Remaining flags/options/atom caches/init locks; deferred frees, fixed caches, carriers, segment caches and mappings; ordered allocator stop. |
| I/O / `sys/common/erl_check_io.c`, `erl_poll.c` | Poll group owns pollsets, thread/result storage and driver-event state/locks. Aggregate cold preflight and continuous-FD POSIX release exist. | Running poll/driver/task quiescence, remaining backend policy/configuration, and other-platform cleanup. Shared descriptors do not establish isolate authority. |
| Thread progress / `beam/erl_thr_progress.c` | Domain owns backing and raw TLS key; narrow terminal cold release exists. | Remaining state such as `erts_thr_prgr__`, managed unregister, pending callbacks and running drain; integration with allocator/scheduler teardown. |
| TLS / `beam/erl_engine_thread_keys.c`, thread/driver libraries | Early ERTS wrapper-created keys are tracked. Allocator and TP raw keys have separate domain owners. | TLS values, all users, raw ethread/library/driver-NIF keys and associated thread-library resources. Never bulk-delete live keys. |
| Time/topology / `beam/erl_time_sup.c`, `erl_cpu_topology.c` | Some backing is allocator-accounted. | Engine clock/correction/init state, topology arrays, `cpuinfo`, `scheduler2cpu_map`, locks and callbacks. Guest subscriptions remain isolate-owned. |
| Platform startup / `beam/erl_init.c`, `sys/unix/sys.c`, poll backends | Explicit preparation/boot/worker phases; prior removal of standalone host takeover paths. | Remaining boot/configuration, exit/crash state, descriptor limits and platform initialization resources. Moving a field does not localize host-wide effects. |
| Constant arena / `beam/erl_global_literals.c` | Engine-owned real arena; unpublished cleanup releases off-heap references, exact allocation bases, lock and control. | Bound-consumer retirement, borrowed-view reset and aggregate teardown. |
| Helper/native metadata / e.g. `beam/erl_bif_re.c`, BIF trap caches | Native code implementations can be shared. | Own PCRE/helper contexts and mutable runtime caches; separate Engine infrastructure from namespace-bound exports/terms. |

## Isolate-private worlds

The Engine owns these **through isolate children**, not through one shared world.

| Subsystem / source anchors | Ownership already present | Remaining work |
|---|---|---|
| Atom/code namespaces / `beam/erl_isolate_state.c`, atom/module/export/code-table implementations | Atoms, modules, export literals/tables, fun/record/catch/range state and code-space coordination are namespace-owned. Processes, prepared/published code and detached literals carry lifetime ownership. | Complete loader/ETF/import/fixup/lookup/interpreter/JIT propagation and executable retirement. Diagnostic access remains; private admission is blocked. |
| Registered names / `beam/register.c` | Namespace-owned hash/lock; process-owner routing, target-owner checks and entry provenance; port-close follows entry owner. | Private process/port integration, residual NULL-process/diagnostic boundaries and running retirement. Bound registries refuse unpublished disposal. |
| Persistent terms / `beam/erl_bif_persistent.c` | Namespace-owned store, update/deletion work, locks, traps and lifetime leases; actual BIF/native access paths updated. | Stop/clear/drain protocol, shared collector integration and residual diagnostic metadata. Empty unpublished disposal is not live-store reclamation. |
| Processes / `beam/erl_process.c` | `Process.namespace_owner` and physical lifetime leases. | Global `erts_proc` table, PID lookup/enumeration, signal/admission paths and isolate-scoped retirement. Owning the Process namespace does not isolate global PID lookup. |
| Ports / `beam/io.c`, `erl_port_task.c` | Shared I/O substrate has an Engine owner; registered entries carry registry provenance. | Global `erts_port`, port resources/authority, pending tasks, callbacks and close/reclamation. Private port registration is refused. |
| ETS / `beam/erl_db.c` and backends | Existing individual table/process relationships. | `meta_name_tab`, name locks, discovery, limits/accounting, owner/heir transfers, continuations and delayed deletion beneath isolates. |
| Purging/literal collection / `beam/beam_bif_load.c` | Published code and detached literal allocations retain namespaces; persistent-term deletion now carries ownership. | `erts_code_purger`, `erts_literal_area_collector`, shared service state, queues and later jobs need isolate-specific retirement/provenance. Execution/barrier infrastructure may stay Engine-owned. |
| Tracing/monitoring/logging / `beam/erl_trace.c`, `erl_process.c` | Existing process-local attachments. | Session/default roots, `system_seq_tracer`, `system_profile`, `system_logger`, monitor configuration and system-message destinations/queues. Split shared dispatch machinery from guest state. |
| Node/distribution / `beam/erl_node_tables.c`, `dist.c` | Distribution's persistent-term getter now names the diagnostic namespace explicitly. | `erts_this_node`, node/dist tables, identity/reference semantics and node monitors where supported. Unsupported distribution stays excluded, not shared across tenants. |
| OTP services / `beam/erl_init.c`, retained Kernel services | Diagnostic bootstrap separated from worker launch/preparation phases. | Isolate-owned init/application/service roots (`erts_init_process_id`, code server, logger, file services, etc.); no implicit world during Engine creation. |

### Timers, messages and references: split ownership

- **Timers:** scheduler data already contains timer-wheel/service pointers; this
  is not just one file-static table to relocate. Engine owns clock/execution
  machinery. Isolates own registrations, destinations, payloads and cancellation;
  outstanding work must retain its owner through retirement.
- **Messages, monitors and links:** much storage is already process-attached.
  Complete owner-aware routing, cross-owner checks and deferred lifetimes rather
  than merely wrapping existing objects in another container.
- **Magic references / `erl_bif_unique.c`:** `magic_ref_table` and generation state
  remain shared roots. Engine-wide uniqueness may be shared; resource resolution,
  access and lifetime must enforce provenance. Persistent-context checks do not
  solve general reference isolation.
- **Drivers/NIFs / `io.c`, `erl_nif.c`:** split approved shared implementations
  from isolate module bindings, mutable instances, resource types/objects and
  callbacks. `driver_list`, `resource_type_list`, `opened_rt_list`,
  `erts_nif_call_tab` and halt hooks remain migration/audit targets. Unsupported
  effects should be removed/refused, not silently shared.

## Globals that are aliases, versus globals that can remain

`diagnostic_atoms`, `diagnostic_modules`, `diagnostic_exports`,
`diagnostic_allocator`, `diagnostic_io`, `erts_io_event_state`, `bound_keys`,
`diagnostic_literals` and exported constant terms are fixed views into owned
storage, not evidence that those storage migrations are absent.

They still impose work: Engine bindings need safe reset after complete release;
private execution must not use diagnostic namespace views. No global-root
swapping or current-isolate TLS selector is permitted. Crash/debug scratch and
trap/export caches also remain lifecycle/context audit targets.

Genuinely immutable opcode/native implementation tables, compile-time strings
and constant lookup data may remain process-global. Dynamically allocated
"initialize once" storage still needs an owner. Cached atoms/terms/exports are
not automatically namespace-independent.

## Acceptance and order of work

1. Finish shared Engine domains, separate world construction and implement
   dependency-ordered initialization/unwind/teardown.
2. Make `engine_lifecycle` genuinely pass, then demonstrate failed-init recovery.
3. Complete process/port/ETS/timer/service ownership and private execution paths.
4. Integrate all retirement protocols and pass unchanged `two_isolates.cpp`.

Both executable acceptance targets remain blocked at `Engine::create`.
Latest component evidence: nine native probes, seven CTests and 29 tooling tests,
plus returning diagnostic hosts, on the configured macOS ARM64 debug interpreter.
These are not private-world, shutdown, JIT, clean-bootstrap, other-platform,
security or performance acceptance.

Details and exact artifacts:
[ownership-tree checkpoint](0002-ownership-tree-sweep.md),
[integration sweep progress](0002-integration-sweep-progress.md),
[allocator domain](0002-owned-allocator-domain.md),
[cold stages](0002-owned-cold-startup-stages.md), and
[example-led plan](0002-example-implementation-plan.md).
