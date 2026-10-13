/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Shared-prefix and leaf-release witness, NOT complete Engine shutdown.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "sys.h"
#include "erl_vm.h"
#include "global.h"
#include "erl_engine.h"
#include "erl_isolate_state.h"
#include "erl_global_literals.h"
#include "erl_check_io.h"
#include <unistd.h>
#include <fcntl.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "shared preparation check failed at %d: %s\n", __LINE__, #c); _Exit(2); } } while (0)

int main(int argc, char **argv)
{
    ErtsEngine *engine = erl_engine_alloc();
    struct erl_off_heap_header **off_heap;
    Eterm *hp, term;
    int host_pipe[2];
    char byte;
    CHECK(engine && pipe(host_pipe) == 0);
    CHECK(erl_prepare_shared_runtime(engine, 0, NULL) == 2);
    CHECK(!erl_runtime_is_claimed());
    CHECK(engine->startup_phase == ERL_RUNTIME_UNCLAIMED && !engine->startup);
    CHECK(erl_complete_runtime_preparation(engine) == 1);
    CHECK(erl_prepare_shared_runtime(engine, argc, argv) == 0);
    CHECK(engine->startup_phase == ERL_RUNTIME_SHARED_PREPARED);
    CHECK(engine->allocators && engine->io_poll_group && engine->thread_progress);
    CHECK(engine->thread_keys && engine->global_literals && engine->startup && engine->early_schedulers);
    CHECK(erts_discard_early_scheduling(engine) == 1);
    CHECK(!engine->namespace_states && !engine->diagnostic_namespace && !engine->namespace_count);
    CHECK(!erts_diagnostic_namespace());
    CHECK(!erts_proc.r.o.tab && !erts_port.r.o.tab);
    CHECK(!erts_aligned_run_queues && !erts_aligned_scheduler_data);
    CHECK(!engine->scheduler_threads);
    CHECK(!erts_isolate_namespace_create(engine, 8192, 4096));
    CHECK(!erts_isolate_namespace_create_diagnostic(engine, 8192, 4096, 4096));
    CHECK(erl_engine_discard_uninitialized(engine) == 1);
    CHECK(erts_discard_check_io_unstarted(engine) == 1); /* constants first */
    hp = erts_global_literal_allocate(2, &off_heap);
    CHECK(hp);
    CHECK(erts_discard_shared_global_literals(engine) == 1);
    CHECK(!engine->namespace_admission_closed && engine->global_literals);
    term = TUPLE1(hp, make_small(42));
    erts_global_literal_register(&term);
    /* This native view is deliberately dropped before arena retirement. */
    term = THE_NON_VALUE;
    CHECK(erts_discard_shared_global_literals(engine) == 0);
    CHECK(!engine->global_literals && engine->namespace_admission_closed);
    CHECK(engine->startup_phase == ERL_RUNTIME_RELEASING);
    CHECK(engine->release_from == ERL_RUNTIME_SHARED_PREPARED);
    CHECK(is_non_value(ERTS_GLOBAL_LIT_EMPTY_TUPLE));
    CHECK(is_non_value(ERTS_GLOBAL_LIT_EMPTY_BINARY));
    CHECK(erts_discard_shared_global_literals(engine) == 1);
    CHECK(erl_complete_runtime_preparation(engine) == 1);
    CHECK(!engine->namespace_states && !erts_proc.r.o.tab && !erts_port.r.o.tab);
    CHECK(erts_discard_check_io_unstarted(engine) == 0);
    CHECK(!engine->io_poll_group && erts_check_io_size() == 0);
    CHECK(write(host_pipe[1], "x", 1) == 1);
    CHECK(read(host_pipe[0], &byte, 1) == 1 && byte == 'x');
    CHECK(close(host_pipe[0]) == 0 && close(host_pipe[1]) == 0);
    CHECK(erl_runtime_is_claimed()); /* do not clear claim with live backends */
    CHECK(erl_prepare_shared_runtime(engine, argc, argv) == 1);
    CHECK(erl_engine_discard_uninitialized(engine) == 1);
    CHECK(erts_discard_early_scheduling(engine) == 0);
    CHECK(!engine->early_schedulers);
    CHECK(erts_discard_early_scheduling(engine) == 1);
    /* Terminal component stage; allocator/thread-library resources remain.
     * There must be no VM/allocator operation after this test-only TP release. */
    CHECK(erts_thr_progress_discard_unstarted(engine) == 0);
    puts("NATIVE_SHARED_PREFIX_OK world_tables=false constants_release=true io_release=true early_scheduler_release=true thread_progress_release=true engine_shutdown=false");
    fflush(stdout);
    _Exit(0);
}
