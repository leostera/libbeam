// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>

// Experimental real-runtime bring-up, NOT the public Engine/Isolate API.
#include "erl_embed.h"
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

static void host_sigchld(int) {}

int main(int argc, char** argv) {
    const char* control = std::getenv("LIBBEAM_PROBE_CONTROL_FD");
    if (!control) return 10;
    char* end = nullptr;
    errno = 0;
    const long fd = std::strtol(control, &end, 10);
    if (errno || !*control || *end || fd < 3 || fd > INT_MAX) return 11;

    struct sigaction action = {};
    action.sa_handler = host_sigchld;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGCHLD, &action, nullptr) != 0) return 15;

    // This genuinely starts the linked emulator; no helper VM is launched.
    if (erl_start_embedded(argc, argv) != 0) return 12;
    struct sigaction after = {};
    if (sigaction(SIGCHLD, nullptr, &after) != 0 ||
        after.sa_handler != host_sigchld) return 16;
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
    std::puts("HOST_NO_CHILDREN sigchld_preserved=true");
    std::puts("HOST_CONTROL_OK engine_shutdown=false isolates_created=0 process_exit=true");
    std::fflush(stdout);
    // There is no engine destructor yet. Explicit process exit is ONLY how this
    // bounded bring-up experiment ends; it is not lifecycle/reclamation evidence.
    std::_Exit(0);
}
