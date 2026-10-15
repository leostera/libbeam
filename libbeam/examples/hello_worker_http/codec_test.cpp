// SPDX-License-Identifier: Apache-2.0
// Run the bridge's codec self-check as a Bazel test. This does not start an
// Isolate or claim that the full closure is admitted.
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <unistd.h>

int main() {
    const char* root = std::getenv("TEST_SRCDIR");
    const char* workspace = std::getenv("TEST_WORKSPACE");
    if (!root || !workspace) {
        std::cerr << "Bazel runfiles environment is missing\n";
        return 1;
    }
    const std::string bridge = std::string(root) + '/' + workspace +
        "/libbeam/examples/hello_worker_http/bridge";
    ::execl(bridge.c_str(), "bridge", "--self-check", static_cast<char*>(nullptr));
    std::cerr << "could not run bridge codec check: " << std::strerror(errno) << '\n';
    return 1;
}
