// SPDX-License-Identifier: Apache-2.0
// Real-Isolate dynamic bit skips: aligned/unaligned, boundary, failure, reclamation.
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
    if (!file) throw std::runtime_error("Cannot open bit skip fixture");
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
libbeam::ByteView view(const libbeam::Bytes& bytes) { return {bytes.data(), bytes.size()}; }
}

int main(int argc, char** argv) {
    if (argc != 2) { std::cerr << "Expected bit skip fixture BEAM path\n"; return 2; }
    std::optional<libbeam::Engine> engine;
    std::optional<libbeam::Isolate> isolate;
    bool okay = false;
    try {
        auto beam = read_beam(argv[1]);
        if (beam.empty()) throw std::runtime_error("Empty bit skip fixture");
        engine.emplace(take(libbeam::Engine::create()));
        isolate.emplace(take(engine->create_isolate()));
        require_ok(isolate->load_modules({{beam.data(), beam.size()}}));
        // Ordinary OTP returned these byte-aligned results for the same inputs.
        const libbeam::Bytes first{8, 0xaa, 0xbb};
        auto initial = take(isolate->start("skip_bits_slice", "run", view(first)));
        if (take(initial.wait_until(deadline())) != libbeam::Bytes{0xbb})
            throw std::runtime_error("one-byte skip differs from OTP");
        const libbeam::Bytes zero{0, 0xaa, 0xbb};
        auto all = take(isolate->call("skip_bits_slice", "run", view(zero)));
        if (take(all.wait_until(deadline())) != libbeam::Bytes{0xaa, 0xbb})
            throw std::runtime_error("zero skip differs from OTP");
        const libbeam::Bytes boundary{16, 0xaa, 0xbb};
        auto end = take(isolate->call("skip_bits_slice", "run", view(boundary)));
        if (take(end.wait_until(deadline())) != libbeam::Bytes{})
            throw std::runtime_error("end-of-input skip differs from OTP");
        const libbeam::Bytes unaligned{0x03, 0x80, 0xaa};
        auto shifted = take(isolate->call("skip_bits_slice", "unaligned", view(unaligned)));
        if (take(shifted.wait_until(deadline())) != libbeam::Bytes{0xaa})
            throw std::runtime_error("unaligned cursor skip differs from OTP");
        const libbeam::Bytes too_short{24, 0xaa, 0xbb};
        auto invalid = take(isolate->call("skip_bits_slice", "run", view(too_short)));
        auto result = invalid.wait_until(deadline());
        const auto* error = std::get_if<libbeam::Error>(&result);
        if (!error || error->code != libbeam::ErrorCode::exception)
            throw std::runtime_error("out-of-bounds skip did not fail as a guest exception");
        okay = true;
    } catch (const std::exception& error) {
        std::cerr << "bit skip test failed: " << error.what() << '\n';
    }
    if (isolate) {
        try {
            auto receipt = take(isolate->stop());
            require_ok(receipt.wait_until(deadline()));
            auto usage = take(isolate->resource_usage());
            if (!usage.closed || usage.memory_current != 0)
                throw std::runtime_error("Isolate did not reclaim memory");
        } catch (const std::exception& error) {
            std::cerr << "bit skip teardown failed: " << error.what() << '\n';
            okay = false;
        }
        isolate.reset();
    }
    if (engine) {
        if (auto error = engine->shutdown(deadline())) {
            std::cerr << "bit skip engine shutdown failed: " << error->message << '\n';
            okay = false;
        }
        engine.reset();
    }
    if (okay) std::cout << "BIT_SKIP_OK OTP_bytes=true bounds=true reclaimed=true\n";
    return okay ? 0 : 1;
}
