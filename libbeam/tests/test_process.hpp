// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#pragma once
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <poll.h>
#include <spawn.h>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
#include <variant>
#include <vector>

extern char** environ;

namespace libbeam_test {
struct Exited { int code; };
struct Signaled { int signal; };
struct ProcessResult {
    std::variant<Exited, Signaled> termination;
    std::string standard_output;
    std::string standard_error;
    // Combined diagnostic text, not a claim about cross-stream ordering.
    std::string output;
    bool exited(int code) const {
        const auto* exit = std::get_if<Exited>(&termination);
        return exit && exit->code == code;
    }
};

inline std::string runfile(const std::string& relative) {
    const char* directory = std::getenv("TEST_SRCDIR");
    const char* workspace = std::getenv("TEST_WORKSPACE");
    if (!directory || !workspace) throw std::runtime_error("Bazel runfiles environment is missing");
    return std::string(directory) + "/" + workspace + "/" + relative;
}

namespace detail {
inline void checked(int error) {
    if (error) throw std::runtime_error(std::strerror(error));
}
struct Pipe {
    int descriptors[2];
    Pipe() { if (pipe(descriptors)) checked(errno); }
    Pipe(const Pipe&) = delete;
    Pipe& operator=(const Pipe&) = delete;
    void close_end(unsigned index) {
        if (descriptors[index] >= 0) { close(descriptors[index]); descriptors[index] = -1; }
    }
    ~Pipe() { close_end(0); close_end(1); }
};
struct Actions {
    posix_spawn_file_actions_t value;
    Actions() { checked(posix_spawn_file_actions_init(&value)); }
    ~Actions() { posix_spawn_file_actions_destroy(&value); }
    Actions(const Actions&) = delete;
    Actions& operator=(const Actions&) = delete;
};
struct Child {
    pid_t pid = -1;
    Child() = default;
    Child(const Child&) = delete;
    Child& operator=(const Child&) = delete;
    ~Child() {
        // Exception paths must not leave a child blocked on a full pipe.
        if (pid > 0) {
            kill(pid, SIGKILL);
            while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {}
        }
    }
    int wait() {
        int status;
        pid_t waited;
        do { waited = waitpid(pid, &status, 0); } while (waited < 0 && errno == EINTR);
        const int error = errno;
        if (waited == pid || (waited < 0 && error == ECHILD)) pid = -1;
        if (waited < 0) checked(error);
        return status;
    }
};
}  // namespace detail

// Spawn only the actual program under test. Bazel owns the overall timeout.
// Drain both pipes concurrently: waiting for stdout EOF before reading stderr
// deadlocks when a child fills stderr while still holding stdout open.
inline ProcessResult run(const std::string& executable, std::vector<std::string> arguments = {}) {
    arguments.insert(arguments.begin(), executable);
    std::vector<char*> argv;
    for (auto& argument : arguments) argv.push_back(argument.data());
    argv.push_back(nullptr);
    detail::Pipe out, err;
    detail::Actions actions;
    detail::checked(posix_spawn_file_actions_adddup2(&actions.value, out.descriptors[1], STDOUT_FILENO));
    detail::checked(posix_spawn_file_actions_adddup2(&actions.value, err.descriptors[1], STDERR_FILENO));
    for (auto* pipe : {&out, &err}) for (int descriptor : pipe->descriptors)
        detail::checked(posix_spawn_file_actions_addclose(&actions.value, descriptor));
    detail::Child child;
    pid_t spawned;
    detail::checked(posix_spawn(&spawned, executable.c_str(), &actions.value, nullptr, argv.data(), environ));
    child.pid = spawned;
    out.close_end(1);
    err.close_end(1);
    std::string streams[2];
    pollfd readers[2]{{out.descriptors[0], POLLIN, 0}, {err.descriptors[0], POLLIN, 0}};
    unsigned remaining = 2;
    while (remaining) {
        int ready;
        do { ready = poll(readers, 2, -1); } while (ready < 0 && errno == EINTR);
        if (ready < 0) detail::checked(errno);
        for (unsigned i = 0; i < 2; ++i) {
            if (!readers[i].revents) continue;
            char buffer[4096];
            ssize_t count;
            do { count = read(readers[i].fd, buffer, sizeof(buffer)); } while (count < 0 && errno == EINTR);
            if (count < 0) detail::checked(errno);
            if (count > 0) streams[i].append(buffer, static_cast<size_t>(count));
            else { readers[i].fd = -1; --remaining; }
        }
    }
    const int status = child.wait();
    ProcessResult result{Exited{0}, std::move(streams[0]), std::move(streams[1]), {}};
    result.output = result.standard_output + result.standard_error;
    if (WIFEXITED(status)) result.termination = Exited{WEXITSTATUS(status)};
    else if (WIFSIGNALED(status)) result.termination = Signaled{WTERMSIG(status)};
    else throw std::runtime_error("Child did not terminate");
    return result;
}
}  // namespace libbeam_test
