// SPDX-License-Identifier: Apache-2.0
// OTP-compiled gc_bif bor/2 takes both the small fast path and native big fallback.
#include <libbeam/engine.hpp>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>

namespace {
libbeam::Deadline deadline() { return std::chrono::steady_clock::now() + std::chrono::seconds(10); }
template<class T> T take(libbeam::Result<T> result) {
    if (auto* error = std::get_if<libbeam::Error>(&result)) throw std::runtime_error(error->message);
    return std::get<T>(std::move(result));
}
void require_ok(libbeam::Status result) { if (result) throw std::runtime_error(result->message); }
libbeam::Bytes read_beam(const char* path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Cannot open bitwise OR fixture");
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
libbeam::ByteView view(const libbeam::Bytes& bytes) { return {bytes.data(), bytes.size()}; }
}

int main(int argc, char** argv) {
    if (argc != 2) { std::cerr << "Expected bitwise OR fixture BEAM path\n"; return 2; }
    std::optional<libbeam::Engine> engine;
    std::optional<libbeam::Isolate> isolate;
    bool okay = false;
    try {
        auto beam = read_beam(argv[1]);
        if (beam.empty()) throw std::runtime_error("Empty bitwise OR fixture");
        engine.emplace(take(libbeam::Engine::create()));
        isolate.emplace(take(engine->create_isolate()));
        require_ok(isolate->load_modules({{beam.data(), beam.size()}}));
        const libbeam::Bytes small{0x3c, 0x0f};
        auto first = take(isolate->start("bor_slice", "run", view(small)));
        if (take(first.wait_until(deadline())) != libbeam::Bytes{0x3f})
            throw std::runtime_error("small bitwise OR differs from OTP");
        const libbeam::Bytes text{'a', 'b', 'c'};
        auto fallback = take(isolate->call("bor_slice", "big", view(text)));
        const libbeam::Bytes expected{0, 0, 0, 0, 0, 0, 0, 0x40, 0, 0, 0, 0, 0, 0, 0, 3};
        if (take(fallback.wait_until(deadline())) != expected)
            throw std::runtime_error("bignum bitwise OR differs from OTP");
        auto invalid = take(isolate->call("bor_slice", "bad", view(text)));
        auto result = invalid.wait_until(deadline());
        const auto* error = std::get_if<libbeam::Error>(&result);
        if (!error || error->code != libbeam::ErrorCode::exception)
            throw std::runtime_error("non-integer bitwise OR did not yield a guest exception");
        okay = true;
    } catch (const std::exception& error) {
        std::cerr << "bitwise OR test failed: " << error.what() << '\n';
    }
    if (isolate) {
        try {
            auto receipt = take(isolate->stop());
            require_ok(receipt.wait_until(deadline()));
            auto usage = take(isolate->resource_usage());
            if (!usage.closed || usage.memory_current != 0)
                throw std::runtime_error("Isolate did not reclaim memory");
        } catch (const std::exception& error) {
            std::cerr << "bitwise OR teardown failed: " << error.what() << '\n';
            okay = false;
        }
        isolate.reset();
    }
    if (engine) {
        if (auto error = engine->shutdown(deadline())) {
            std::cerr << "bitwise OR engine shutdown failed: " << error->message << '\n';
            okay = false;
        }
        engine.reset();
    }
    if (okay) std::cout << "BOR_EXECUTION_OK small=true big=true bad_type=true reclaimed=true\n";
    return okay ? 0 : 1;
}
