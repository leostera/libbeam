// SPDX-License-Identifier: Apache-2.0
// Exercises the pre-existing binary-comprehension fixture on a real Isolate.
// This witness must pass before accepting interprocedural private-append proof.
#include <libbeam/engine.hpp>

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {
#if defined(LB_CALLBACK_APPEND_FIXTURE)
constexpr char module_name[] = "callback_append_slice";
constexpr char function_name[] = "run";
#elif defined(LB_REMOTE_APPEND_FIXTURE)
constexpr char module_name[] = "remote_append_slice";
constexpr char function_name[] = "run";
#else
constexpr char module_name[] = "binary_create_slice";
constexpr char function_name[] = "private";
#endif

libbeam::Deadline deadline() { return std::chrono::steady_clock::now() + std::chrono::seconds(10); }

template<class T> T take(libbeam::Result<T> result) {
    if (auto* error = std::get_if<libbeam::Error>(&result))
        throw std::runtime_error(error->message);
    return std::get<T>(std::move(result));
}

void require_ok(libbeam::Status status) {
    if (status) throw std::runtime_error(status->message);
}
}

int main(int argc, char** argv) {
    if (argc != 2) { std::cerr << "Expected private-append fixture BEAM path\n"; return 2; }
    std::ifstream file(argv[1], std::ios::binary);
    if (!file) { std::cerr << "Cannot open unchanged fixture\n"; return 2; }
    const std::vector<std::uint8_t> beam{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    if (beam.empty()) { std::cerr << "Empty fixture\n"; return 2; }
    std::optional<libbeam::Engine> engine;
    std::optional<libbeam::Isolate> isolate;
    bool okay = false;
    try {
        engine.emplace(take(libbeam::Engine::create()));
        isolate.emplace(take(engine->create_isolate()));
        require_ok(isolate->load_module({beam.data(), beam.size()}));
        for (const auto& input : {libbeam::Bytes{0, 10, 255}, libbeam::Bytes{7, 8}}) {
            auto call = take(okay ? isolate->call(module_name, function_name, {input.data(), input.size()})
                                  : isolate->start(module_name, function_name, {input.data(), input.size()}));
            const auto output = take(call.wait_until(deadline()));
            if (output != input) throw std::runtime_error("private append output differs from ordinary OTP");
            okay = true;
        }
    } catch (const std::exception& error) {
        std::cerr << "append inference execution failed: " << error.what() << '\n';
        okay = false;
    }
    if (isolate) {
        try {
            auto receipt = take(isolate->stop());
            require_ok(receipt.wait_until(deadline()));
            const auto usage = take(isolate->resource_usage());
            if (!usage.closed || usage.memory_current != 0)
                throw std::runtime_error("Isolate did not reclaim memory");
        } catch (const std::exception& error) {
            std::cerr << "reclamation failed: " << error.what() << '\n';
            okay = false;
        }
        isolate.reset();
    }
    if (engine) {
        if (auto error = engine->shutdown(deadline())) {
            std::cerr << "engine shutdown failed: " << error->message << '\n';
            okay = false;
        }
        engine.reset();
    }
    if (okay) std::cout << "APPEND_INFERENCE_EXECUTION_OK isolate=true bytes=true reclaimed=true\n";
    return okay ? 0 : 1;
}
