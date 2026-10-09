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
 * Only the first two engine-owned components are migrated in this slice. The
 * remaining VM globals still prevent independent runtime initialization. There
 * is deliberately no default/current-engine accessor or fake isolate container.
 */
struct ErtsSchedulerThreadGroup;
struct ErtsEngine {
    enum ErlRuntimeStartupPhase startup_phase;
    struct ErtsSchedulerThreadGroup *scheduler_threads;
};

#endif
