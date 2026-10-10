// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
// Acceptance target, not an expected-failure test. No OTP bootstrap or isolates.
#include <libbeam/engine.hpp>
#include <chrono>
#include <iostream>
#include <utility>

int main() {
    for (int iteration = 0; iteration != 2; ++iteration) {
        auto result = libbeam::Engine::create();
        if (auto* error = std::get_if<libbeam::Error>(&result)) {
            std::cerr << "create " << iteration << ": " << error->message << '\n';
            return 1;
        }
        auto engine = std::move(std::get<libbeam::Engine>(result));
        if (auto error = engine.shutdown(std::chrono::steady_clock::now()
                                         + std::chrono::seconds(5))) {
            std::cerr << "shutdown " << iteration << ": " << error->message << '\n';
            return 1;
        }
        // Destroy the first owning handle before attempting the second create.
    }
    std::cout << "ENGINE_LIFECYCLE_OK create_shutdown_create=true\n";
}
