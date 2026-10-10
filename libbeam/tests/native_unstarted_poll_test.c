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
    int held[256], count = 0, fd, i;
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
    for (i = 0; i < 2; ++i) {
        snapshot(before);
        errno = 0;
        CHECK(erts_poll_create_pollset(-100) == NULL && errno == EMFILE);
        snapshot(after);
        CHECK(sys_memcmp(before, after, sizeof(before)) == 0);
#if ERTS_POLL_USE_FALLBACK && ERTS_KERNEL_POLL_VERSION
        errno = 0;
        CHECK(erts_poll_create_pollset_flbk(-101) == NULL && errno == EMFILE);
        snapshot(after);
        CHECK(sys_memcmp(before, after, sizeof(before)) == 0);
#endif
        if (i == 0) CHECK(close(held[--count]) == 0);
    }
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
    CHECK(pipe(pipefd) == 0 && pipefd[1] < FD_SCAN);
    snapshot(before);
    CHECK(erts_poll_discard_unstarted(NULL) == 1);
    for (i = 0; i < 32; ++i) {
        a = erts_poll_create_pollset(100);
        b = erts_poll_create_pollset(101);
        CHECK(a && b);
        CHECK(erts_poll_discard_unstarted(a) == 0);
        erts_poll_info(b, &info);
        CHECK(info.active_fds == 0);
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
    puts("NATIVE_UNSTARTED_POLL_OK descriptor_balance=true peer_survival=true engine_shutdown=false");
    fflush(stdout);
    _Exit(0);
}
