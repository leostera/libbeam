/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Poll-backend cleanup component test, NOT engine shutdown.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "sys.h"
#include "erl_vm.h"
#include "global.h"
#include "erl_poll.h"
#include "erl_embed.h"
#include "erl_engine.h"
#include "erl_io_poll_group.h"
#include "erl_check_io.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/resource.h>
#include <errno.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "poll cleanup check failed at %d: %s\n", __LINE__, #x); \
    return 1; } } while (0)
#define FD_SCAN 8192
static void snapshot(unsigned char *fds)
{
    int i;
    for (i = 0; i < FD_SCAN; ++i)
        fds[i] = fcntl(i, F_GETFD) >= 0;
}
static int descriptor_failure_rollback(void)
{
    struct rlimit original, limited;
    int held[256], count = 0, fd, i, group_successes = 0;
    unsigned char before[FD_SCAN], after[FD_SCAN];
    CHECK(getrlimit(RLIMIT_NOFILE, &original) == 0);
    limited = original;
    if (limited.rlim_cur > 128) limited.rlim_cur = 128;
    CHECK(limited.rlim_cur >= 32);
    CHECK(setrlimit(RLIMIT_NOFILE, &limited) == 0);
    while ((fd = open("/dev/null", O_RDONLY)) >= 0) {
        CHECK(count < 256);
        held[count++] = fd;
    }
    CHECK(errno == EMFILE && count > 0);
    /* No spare fd: first descriptor creation fails. One spare fd: the kernel
     * poll descriptor succeeds but creation of its wakeup pipe fails. */
    for (i = 0; i < 12; ++i) {
        ErtsIoPollGroup *group;
        snapshot(before);
        if (i < 2) {
            errno = 0;
            CHECK(erts_poll_create_pollset(-100) == NULL && errno == EMFILE);
#if ERTS_POLL_USE_FALLBACK && ERTS_KERNEL_POLL_VERSION
            errno = 0;
            CHECK(erts_poll_create_pollset_flbk(-101) == NULL && errno == EMFILE);
#endif
        }
        /* Progressively permit each primary/scheduler/fallback descriptor.
         * Earlier children must be released when a later child cannot open. */
        errno = 0;
        group = erts_io_poll_group_create(2, 2, ERTS_POLL_USE_SCHEDULER_POLLING);
        if (group) {
            ++group_successes;
            CHECK(erts_io_poll_group_discard(group) == 0);
        } else {
            CHECK(errno == EMFILE);
        }
        snapshot(after);
        CHECK(sys_memcmp(before, after, sizeof(before)) == 0);
        if (i != 11) {
            CHECK(count > 0);
            CHECK(close(held[--count]) == 0);
        }
    }
    CHECK(group_successes > 0);
    while (count) CHECK(close(held[--count]) == 0);
    CHECK(setrlimit(RLIMIT_NOFILE, &original) == 0);
    return 0;
}
int main(int argc, char **argv)
{
    ErtsEngine *engine = erl_engine_alloc();
    unsigned char before[FD_SCAN], after[FD_SCAN];
    ErtsPollSet *a, *b;
    ErtsPollInfo info;
    int i, pipefd[2], wake = 0;
    char c;
    CHECK(engine && erl_prepare_runtime(engine, argc, argv) == 0);
    {
        ErtsEngine *candidate = erl_engine_alloc();
        CHECK(candidate && erts_discard_check_io_unstarted(candidate) == 1);
        CHECK(erts_thr_progress_pre_init(candidate) != 0);
        CHECK(!candidate->thread_progress);
        CHECK(erl_engine_discard_uninitialized(candidate) == 0);
    }
    CHECK(erts_thr_progress_owned_bytes(engine) > 0);
    CHECK(erts_thr_progress_shared_bytes() == erts_thr_progress_owned_bytes(engine));
    CHECK(erts_thr_progress_discard_unstarted(engine) != 0);
    CHECK(pipe(pipefd) == 0 && pipefd[1] < FD_SCAN);
    snapshot(before);
    CHECK(erts_poll_discard_unstarted(NULL) == 1);
    CHECK(engine->io_poll_group && !erts_io_poll_group_can_discard(engine->io_poll_group));
    CHECK(erts_check_io_size() > 0);
    CHECK(!erts_io_poll_group_create(0, 1, 0) && errno == EINVAL);
    CHECK(!erts_io_poll_group_create(2, 1, 0) && errno == EINVAL);
    for (i = 0; i < 32; ++i) {
        ErtsIoPollGroup *ga = erts_io_poll_group_create(2, 2, 0);
        ErtsIoPollGroup *gb = erts_io_poll_group_create(1, 1, ERTS_POLL_USE_SCHEDULER_POLLING);
        CHECK(ga && gb);
        CHECK(!erts_io_poll_group_borrow_thread(ga, 2));
        CHECK(!erts_io_poll_group_borrow_thread(ga, -10));
        CHECK(erts_io_poll_group_discard(ga) == 0);
        CHECK(erts_io_poll_group_can_discard(gb));
        CHECK(erts_io_poll_group_discard(gb) == 0);
        a = erts_poll_create_pollset(100);
        b = erts_poll_create_pollset(101);
        CHECK(a && b);
        CHECK(erts_poll_discard_unstarted(a) == 0);
        erts_poll_info(b, &info);
        CHECK(info.poll_set_size == 1); /* Backend reports user count + wakeup slot. */
        CHECK(erts_poll_can_discard_unstarted(b));
        CHECK(erts_poll_discard_unstarted(b) == 0);
        a = erts_poll_create_pollset(-100); /* Owns a wakeup pipe as well. */
        CHECK(a && erts_poll_discard_unstarted(a) == 0);
#if ERTS_POLL_USE_FALLBACK && ERTS_KERNEL_POLL_VERSION
        a = erts_poll_create_pollset_flbk(102);
        CHECK(a && erts_poll_discard_unstarted_flbk(a) == 0);
#endif
        snapshot(after);
        CHECK(sys_memcmp(before, after, sizeof(before)) == 0);
    }
    a = erts_poll_create_pollset(103);
    erts_poll_info(a, &info);
    /* The lazy-update backend cannot drain registration changes without a poll
     * wait, which deliberately moves it outside this cleanup API's scope. */
    if (info.concurrent_updates) {
        CHECK(erts_poll_control(a, pipefd[0], ERTS_POLL_OP_ADD,
                               ERTS_POLL_EV_IN, &wake) != ERTS_POLL_EV_NVAL);
        CHECK(erts_poll_discard_unstarted(a) == 1);
        CHECK(write(pipefd[1], "x", 1) == 1);
        CHECK(read(pipefd[0], &c, 1) == 1 && c == 'x');
        (void) erts_poll_control(a, pipefd[0], ERTS_POLL_OP_DEL,
                                 ERTS_POLL_EV_NONE, &wake);
    }
    CHECK(erts_poll_discard_unstarted(a) == 0);
    snapshot(after);
    CHECK(sys_memcmp(before, after, sizeof(before)) == 0);
    CHECK(write(pipefd[1], "y", 1) == 1);
    CHECK(read(pipefd[0], &c, 1) == 1 && c == 'y');
    CHECK(descriptor_failure_rollback() == 0);
    snapshot(after);
    CHECK(sys_memcmp(before, after, sizeof(before)) == 0);
    CHECK(close(pipefd[0]) == 0 && close(pipefd[1]) == 0);
    {
        ErtsIoPollGroup *borrowed = erts_io_poll_group_create(1, 1, 0);
        CHECK(borrowed && erts_io_poll_group_borrow_thread(borrowed, 0));
        snapshot(before);
        CHECK(erts_io_poll_group_discard(borrowed) == 1);
        snapshot(after);
        CHECK(sys_memcmp(before, after, sizeof(before)) == 0);
        /* Worker-visible group deliberately retained until process exit. */
    }
    /* A past wait is enough to leave the supported cold-cleanup scope. This
     * final guarded object is deliberately retained until test-process exit. */
    {
        ErtsPollResFd event;
        int length = 1, result;
        a = erts_poll_create_pollset(104);
        CHECK(a);
        result = erts_poll_wait(a, &event, &length, NULL, ERTS_POLL_NO_TIMEOUT);
        CHECK(result == 0 || result == ETIMEDOUT || result == EINTR);
        CHECK(erts_poll_discard_unstarted(a) == 1);
    }
    {
        ErtsIoPollGroup *peer = erts_io_poll_group_create(1, 1, 0);
        ErlPreparedRuntimeInventory inventory;
        int removed = 0;
        CHECK(peer);
        snapshot(before);
        CHECK(erts_discard_check_io_unstarted(engine) == 0);
        CHECK(erl_runtime_startup_phase(engine) == ERL_RUNTIME_RELEASING);
        CHECK(!engine->io_poll_group);
        CHECK(erts_check_io_size() == 0);
        CHECK(erl_prepared_runtime_inventory(engine, &inventory) == 1);
        CHECK(erl_engine_discard_uninitialized(engine) == 1);
        CHECK(erl_start_embedded(engine, argc, argv) == 1);
        snapshot(after);
        for (i = 0; i < FD_SCAN; ++i) {
            CHECK(!after[i] || before[i]);
            removed += before[i] && !after[i];
        }
        CHECK(removed > 0);
        CHECK(erts_discard_check_io_unstarted(engine) == 1);
        CHECK(erts_io_poll_group_discard(peer) == 0);
    }
    /* Terminal component cleanup only. Remaining runtime allocations are
     * retained; no allocator or VM operation follows this point. */
    CHECK(erts_thr_progress_discard_unstarted(engine) == 0);
    CHECK(erts_thr_progress_owned_bytes(engine) == 0);
    CHECK(!engine->thread_progress);
    CHECK(erts_thr_progress_shared_bytes() == 0);
    puts("NATIVE_UNSTARTED_POLL_OK descriptor_balance=true peer_survival=true engine_shutdown=false");
    puts("NATIVE_COLD_STAGES_OK aggregate_poll_rollback=true io_release=true thread_progress_release=true engine_shutdown=false");
    fflush(stdout);
    _Exit(0);
}
