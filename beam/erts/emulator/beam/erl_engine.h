/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef ERL_ENGINE_H__
#define ERL_ENGINE_H__

#include "erl_embed.h"

/* Native engine ownership, not a bag of former VM globals.
 *
 * Once per engine: lifecycle, shared scheduling/worker infrastructure, clocks,
 * thread progress and immutable native metadata. Components migrate here only
 * when their access paths also receive an explicit owner.
 *
 * Per isolate, NOT here: mutable atom/code/export/fun namespaces, registrations,
 * ETS/persistent terms, application processes/heaps and their timers/messages.
 * Shared queues may carry such work, but must retain its isolate owner.
 *
 * The intrusive namespace ownership tree is managed on the control thread.
 * Namespace containers are not yet runnable public Isolates. Remaining VM
 * globals still prevent independent runtime initialization.
 */
struct ErtsSchedulerThreadGroup;
struct ErtsIoPollGroup;
struct ErtsThreadProgressDomain;
struct ErtsAllocatorDomain;
struct ErtsEngineThreadKeys;
struct ErtsIsolateNamespaceState;
struct ErtsEngine {
    enum ErlRuntimeStartupPhase startup_phase;
    struct ErtsSchedulerThreadGroup *scheduler_threads;
    struct ErtsIoPollGroup *io_poll_group;
    struct ErtsThreadProgressDomain *thread_progress;
    struct ErtsAllocatorDomain *allocators;
    struct ErtsEngineThreadKeys *thread_keys;
    struct ErtsIsolateNamespaceState *namespace_states;
    struct ErtsIsolateNamespaceState *diagnostic_namespace;
    size_t namespace_count;
    int namespace_admission_closed;
};

#endif
