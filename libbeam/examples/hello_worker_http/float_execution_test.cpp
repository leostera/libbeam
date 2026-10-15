// SPDX-License-Identifier: Apache-2.0
// OTP-compiled floating subtraction and dynamic float-load execution witnesses.
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
libbeam::Deadline deadline() { return std::chrono::steady_clock::now() + std::chrono::seconds(10); }
template<class T> T take(libbeam::Result<T> result) {
    if (auto* error = std::get_if<libbeam::Error>(&result)) throw std::runtime_error(error->message);
    return std::get<T>(std::move(result));
}
void require_ok(libbeam::Status status) { if (status) throw std::runtime_error(status->message); }
libbeam::Bytes read_beam(const char* path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Cannot open float fixture");
    libbeam::Bytes result{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    if (result.empty()) throw std::runtime_error("Empty float fixture");
    return result;
}
}

int main(int argc, char** argv) {
    if (argc != 3) { std::cerr << "Expected two float BEAM fixture paths\n"; return 2; }
    std::optional<libbeam::Engine> engine;
    std::optional<libbeam::Isolate> isolate;
    bool okay = false;
    try {
        auto sub = read_beam(argv[1]);
        auto load = read_beam(argv[2]);
        engine.emplace(take(libbeam::Engine::create()));
        isolate.emplace(take(engine->create_isolate()));
        require_ok(isolate->load_modules({{sub.data(), sub.size()}, {load.data(), load.size()}}));
        // Ordinary OTP: 3.25 - 2.5 = 0.75, big-endian IEEE-754 3fe8000000000000.
        const libbeam::Bytes expected{0x3f, 0xe8, 0, 0, 0, 0, 0, 0};
        const std::string input = "3.25";
        bool started = false;
        for (const char* module : {"float_sub_slice", "float_load_slice"}) {
            auto bytes = libbeam::ByteView{reinterpret_cast<const std::uint8_t*>(input.data()), input.size()};
            auto call = take(started ? isolate->call(module, "run", bytes)
                                     : isolate->start(module, "run", bytes));
            started = true;
            if (take(call.wait_until(deadline())) != expected)
                throw std::runtime_error("float output differs from ordinary OTP");
        }
        okay = true;
    } catch (const std::exception& error) {
        std::cerr << "float execution failed: " << error.what() << '\n';
    }
    if (isolate) {
        try {
            auto receipt = take(isolate->stop());
            require_ok(receipt.wait_until(deadline()));
            auto usage = take(isolate->resource_usage());
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
    if (okay) std::cout << "FLOAT_EXECUTION_OK fsub=true fload_dynamic=true bytes=true reclaimed=true\n";
    return okay ? 0 : 1;
}
