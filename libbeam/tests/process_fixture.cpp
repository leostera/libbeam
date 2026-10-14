// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#include <csignal>
#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::string mode = argv[1];
    if (mode == "exit-seven") return 7;
    if (mode == "signal") { std::raise(SIGTERM); return 99; }
    if (mode == "output") {
        std::fputs("stdout\n", stdout);
        std::fflush(stdout);
        std::fputs("stderr\n", stderr);
        return 0;
    }
    if (mode == "large-both-streams") {
        const std::string output(100000, 'x'), error(100000, 'y');
        if (std::fwrite(output.data(), 1, output.size(), stdout) != output.size()) return 1;
        if (std::fwrite(error.data(), 1, error.size(), stderr) != error.size()) return 1;
        return 0;
    }
    if (mode == "large-output") {
        for (size_t i = 0; i < 100000; ++i) std::putchar('x');
        return std::ferror(stdout) ? 1 : 0;
    }
    return 2;
}
