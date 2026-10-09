// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>

// Experimental real-runtime bring-up, NOT the public Engine/Isolate API.
#include "erl_embed.h"
#ifdef LIBBEAM_PROBE_CPP_PREFLIGHT
#include <libbeam/engine.hpp>
static bool factory_rejects(libbeam::ErrorCode expected) {
    auto result = libbeam::Engine::create();
    auto* error = std::get_if<libbeam::Error>(&result);
    return error && error->code == expected;
}
#endif
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

static volatile sig_atomic_t usr1_deliveries;
static void host_signal(int signal) {
    if (signal == SIGUSR1) ++usr1_deliveries;
}
static const int signals[] = {SIGINT, SIGQUIT, SIGTERM, SIGUSR1, SIGUSR2,
                             SIGTSTP, SIGPIPE, SIGCHLD, SIGFPE};
static struct sigaction saved_actions[sizeof(signals) / sizeof(signals[0])];
static stack_t saved_stack;
static sigset_t saved_mask;

static bool host_signal_state_preserved() {
    stack_t stack = {};
    sigset_t mask;
    if (sigaltstack(nullptr, &stack) != 0 ||
        sigprocmask(SIG_SETMASK, nullptr, &mask) != 0 ||
        stack.ss_sp != saved_stack.ss_sp || stack.ss_size != saved_stack.ss_size ||
        stack.ss_flags != saved_stack.ss_flags) return false;
    for (int sig = 1; sig < NSIG; ++sig)
        if (sigismember(&mask, sig) != sigismember(&saved_mask, sig)) return false;
    for (unsigned i = 0; i < sizeof(signals) / sizeof(signals[0]); ++i) {
        struct sigaction after = {};
        if (sigaction(signals[i], nullptr, &after) != 0 ||
            after.sa_handler != saved_actions[i].sa_handler ||
            after.sa_flags != saved_actions[i].sa_flags) return false;
        for (int sig = 1; sig < NSIG; ++sig)
            if (sigismember(&after.sa_mask, sig) !=
                sigismember(&saved_actions[i].sa_mask, sig)) return false;
    }
    return true;
}

int main(int argc, char** argv) {
    const char* control = std::getenv("LIBBEAM_PROBE_CONTROL_FD");
    if (!control) return 10;
    char* end = nullptr;
    errno = 0;
    const long fd = std::strtol(control, &end, 10);
    if (errno || !*control || *end || fd < 3 || fd > INT_MAX) return 11;

    saved_stack.ss_sp = std::malloc(128 * 1024);
    saved_stack.ss_size = 128 * 1024;
    saved_stack.ss_flags = 0;
    if (!saved_stack.ss_sp || sigaltstack(&saved_stack, nullptr) != 0) return 15;
    sigset_t unblocked;
    sigemptyset(&unblocked);
    sigaddset(&unblocked, SIGUSR1);
    if (sigprocmask(SIG_UNBLOCK, &unblocked, nullptr) != 0 ||
        sigprocmask(SIG_SETMASK, nullptr, &saved_mask) != 0) return 15;
    struct sigaction action = {};
    action.sa_handler = host_signal;
    action.sa_flags = SA_RESTART;
    sigemptyset(&action.sa_mask);
    sigaddset(&action.sa_mask, SIGUSR2);
    for (unsigned i = 0; i < sizeof(signals) / sizeof(signals[0]); ++i) {
        if (sigaction(signals[i], &action, nullptr) != 0 ||
            sigaction(signals[i], nullptr, &saved_actions[i]) != 0) return 15;
    }

#ifdef LIBBEAM_PROBE_CPP_PREFLIGHT
    if (!factory_rejects(libbeam::ErrorCode::not_implemented)) return 21;
#endif
    ErlSchedulerThreadInventory threads = {};
    erl_scheduler_thread_inventory(&threads);
    if (threads.total != 0) return 19;

    // This genuinely starts the linked emulator; no helper VM is launched.
    if (erl_start_embedded(argc, argv) != 0) return 12;
    erl_scheduler_thread_inventory(&threads);
    size_t total = 0;
    for (size_t count : threads.counts) total += count;
    if (!threads.all_joinable || total != threads.total ||
        threads.counts[ERL_THREAD_SCHEDULER] != 2 ||
        threads.counts[ERL_THREAD_DIRTY_CPU] != 1 ||
        threads.counts[ERL_THREAD_DIRTY_IO] != 1 ||
        threads.counts[ERL_THREAD_AUXILIARY] == 0 ||
        threads.counts[ERL_THREAD_POLL] == 0) return 20;
    std::printf("HOST_THREAD_HANDLES_OK total=%zu joinable=true stopped=false joined=false\n",
                threads.total);
#ifdef LIBBEAM_PROBE_CPP_PREFLIGHT
    if (!factory_rejects(libbeam::ErrorCode::invalid_state)) return 22;
    std::puts("HOST_CPP_PREFLIGHT_OK before=not_implemented after=invalid_state");
#endif
    if (!host_signal_state_preserved()) return 16;
    if (erl_start_embedded(argc, argv) != 1) return 13;
    std::printf("HOST_STARTUP_RETURNED pid=%ld second_start=rejected\n",
                static_cast<long>(getpid()));
    std::fflush(stdout);

    // A private host FD avoids confusing BEAM's process-wide standard I/O with
    // host control. The driver releases us only after observing actual bytecode
    // execution in this same PID. No guessed startup sleep is used.
    char command;
    ssize_t count;
    do { count = read(static_cast<int>(fd), &command, 1); }
    while (count < 0 && errno == EINTR);
    if (count != 1 || command != 'X') return 14;
    close(static_cast<int>(fd));
    int status;
    errno = 0;
    if (waitpid(-1, &status, WNOHANG) != -1 || errno != ECHILD) return 17;
    if (!host_signal_state_preserved() || raise(SIGUSR1) != 0 || usr1_deliveries != 1)
        return 18;
    std::puts("HOST_SIGNALS_OK dispositions=9 altstack=true mask=true usr1_delivered=true");
    std::puts("HOST_NO_CHILDREN sigchld_preserved=true");
    std::puts("HOST_CONTROL_OK engine_shutdown=false isolates_created=0 process_exit=true");
    std::fflush(stdout);
    // There is no engine destructor yet. Explicit process exit is ONLY how this
    // bounded bring-up experiment ends; it is not lifecycle/reclamation evidence.
    std::_Exit(0);
}
