/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */

#ifndef ERL_EMBED_H__
#define ERL_EMBED_H__

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque native control object. Allocation alone is NOT a running Engine or an
 * isolate. Only one object may prepare the still-global substrate per process.
 * Access/query/discard operations require a live object on the control thread.
 */
typedef struct ErtsEngine ErtsEngine;
ErtsEngine *erl_engine_alloc(void); /* NULL on system-allocation failure. */
/* Frees only uninitialized control objects. Returns 1 without freeing prepared
 * or running state; this is NOT shutdown or a destructor for initialized VMs. */
int erl_engine_discard_uninitialized(ErtsEngine *engine);
/* Temporary process-wide admission guard for unmigrated globals, not an owner
 * lookup. It cannot select an engine for runtime operations. */
int erl_runtime_is_claimed(void);

/* Lifecycle phases, not embedded/standalone modes. Read on the one host control
 * thread only; concurrent preparation/startup/query is unsupported. */
enum ErlRuntimeStartupPhase {
    ERL_RUNTIME_UNCLAIMED,
    ERL_RUNTIME_PREPARING,
    ERL_RUNTIME_PREPARED,
    ERL_RUNTIME_OTP_BOOTSTRAPPED,
    ERL_RUNTIME_THREADS_STARTED
};
enum ErlRuntimeStartupPhase erl_runtime_startup_phase(const ErtsEngine *engine);

typedef struct {
    size_t processes;
    size_t ports;
    size_t loaded_code_bytes;
    int init_process_created;
    int system_process_roots;
} ErlPreparedRuntimeInventory;
/* Returns 0 only during PREPARED, before concurrent execution can mutate tables.
 * Returns 1 otherwise, leaving the caller's output untouched. */
int erl_prepared_runtime_inventory(const ErtsEngine *engine, ErlPreparedRuntimeInventory *out);

/* Thread handles retained by erts_start_schedulers. Async workers are tracked
 * separately by erl_async.c; this is NOT a census of all engine/native threads.
 * Query only on the startup/control thread, before startup or after it returns.
 * No concurrent startup/shutdown/query. Before startup the inventory is empty.
 * Joinable creation is not a stop protocol or evidence of physical reclamation.
 */
enum ErlSchedulerThreadKind {
    ERL_THREAD_SCHEDULER,
    ERL_THREAD_DIRTY_CPU,
    ERL_THREAD_DIRTY_IO,
    ERL_THREAD_AUXILIARY,
    ERL_THREAD_POLL,
    ERL_THREAD_RUNQ_SUPERVISOR,
    ERL_SCHEDULER_THREAD_KINDS
};
typedef struct {
    size_t counts[ERL_SCHEDULER_THREAD_KINDS];
    size_t total;
    int all_joinable;
} ErlSchedulerThreadInventory;
void erl_scheduler_thread_inventory(const ErtsEngine *engine, ErlSchedulerThreadInventory *out);

/* EXPERIMENTAL POSIX bring-up entry, not a stable libbeam API.
 *
 * Must run once on the host main thread, with no concurrent startup calls.
 * Uses the normal emulator command-line arguments; retain their mutable storage
 * for the lifetime of the process. Embedded startup omits the native forker;
 * executable ports (spawn/spawn_executable) return notsup before effects.
 * BINDIR is not required. Other native effects remain: this is NOT a complete
 * reduced bytecode profile or an approved isolation boundary.
 * Initializes the REAL global OTP runtime and
 * starts its threads, then returns instead of occupying the host main thread.
 * Return does not acknowledge completion of asynchronous OTP boot/application
 * execution. There are no tenant isolates and no local code/atom namespaces.
 *
 * Normal startup does not install standalone signal handlers, a signal dispatcher,
 * a Darwin main-thread pump, or a host alternate signal stack. os:set_signal/2
 * is unsupported. Runtime-owned threads still prepare their own signal stacks.
 * Only default untrapped floating-point environments have been tested. Fatal
 * crash dumping retains legacy signal behavior. CLI errors, boot failure,
 * halt and fatal runtime errors can still terminate the host. No engine shutdown,
 * restart, unload or physical reclamation is implemented.
 * Runtime threads remain live until PROCESS EXIT. Darwin wx/Cocoa main-thread
 * callbacks are unsupported. Trusted standalone bring-up fixtures only.
 *
 * Returns 0 after thread launch, 2 for a NULL engine, 1 for a second startup attempt
 * (including an attempt after ordinary erl_start). Failure is otherwise governed
 * by existing fatal OTP startup semantics, NOT recoverable C++ exceptions.
 */
#if !defined(_WIN32) && !defined(__WIN32__)
int erl_start_embedded(ErtsEngine *engine, int argc, char **argv);

/* Experimental unbooted preparation diagnostic. Initializes global substrate
 * and empty tables, but does NOT load preloaded BEAM code, create init/system
 * processes, or launch runtime threads. Does not construct an isolate or claim
 * private namespaces. Existing CLI parsing/native effects/fatal initialization
 * errors remain; mutable argv must stay alive. Preparation is once per process,
 * not rollback/restart capable. Returns 0 on preparation, 1 if already claimed,
 * 2 for a NULL engine. A rejected uninitialized candidate remains unclaimed.
 * No reclamation yet: allocated global state remains process-lifetime. Do NOT
 * use this as the public Engine factory before ownership/cleanup is implemented.
 */
int erl_prepare_runtime(ErtsEngine *engine, int argc, char **argv);
#endif

#ifdef __cplusplus
}
#endif

#endif
