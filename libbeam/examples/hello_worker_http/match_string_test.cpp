// SPDX-License-Identifier: Apache-2.0
// Exercise OTP-compiled bs_match_string with aligned, misaligned and failure paths.
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
libbeam::Bytes bytes(const std::string& text) { return {text.begin(), text.end()}; }
libbeam::Bytes unaligned() {
    constexpr char literal[] = "qrstuvwxyzABCDEFGHIJ";
    libbeam::Bytes result(sizeof(literal) - 1 + 1, 0); // one leading and seven trailing bits
    for (std::size_t i = 0; i < sizeof(literal) - 1; ++i)
        for (unsigned bit = 0; bit < 8; ++bit) {
            std::size_t position = 1 + i * 8 + bit;
            result[position / 8] |= static_cast<std::uint8_t>(
                ((static_cast<unsigned char>(literal[i]) >> (7 - bit)) & 1u) << (7 - position % 8));
        }
    return result;
}
}

int main(int argc, char** argv) {
    if (argc != 2) { std::cerr << "Expected match_string_slice.beam path\n"; return 2; }
    std::ifstream file(argv[1], std::ios::binary);
    if (!file) { std::cerr << "Cannot open fixture\n"; return 2; }
    const libbeam::Bytes beam{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    if (beam.empty()) { std::cerr << "Empty fixture\n"; return 2; }
    std::optional<libbeam::Engine> engine;
    std::optional<libbeam::Isolate> isolate;
    bool okay = false;
    try {
        engine.emplace(take(libbeam::Engine::create()));
        isolate.emplace(take(engine->create_isolate()));
        require_ok(isolate->load_module({beam.data(), beam.size()}));
        const std::vector<std::pair<libbeam::Bytes, libbeam::Bytes>> cases{
            {bytes("abcdefghijklmnopTAIL"), bytes("TAIL")},
            {bytes("abcdefghijklmnXpTAIL"), bytes("miss")},
            {unaligned(), bytes("unaligned")},
            {bytes("abc"), bytes("miss")},
        };
        bool started = false;
        for (const auto& [input, expected] : cases) {
            auto call = take(started ? isolate->call("match_string_slice", "run", {input.data(), input.size()})
                                     : isolate->start("match_string_slice", "run", {input.data(), input.size()}));
            started = true;
            if (take(call.wait_until(deadline())) != expected)
                throw std::runtime_error("bs_match_string output differs from ordinary OTP");
        }
        okay = true;
    } catch (const std::exception& error) {
        std::cerr << "match string execution failed: " << error.what() << '\n';
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
    if (okay) std::cout << "MATCH_STRING_EXECUTION_OK aligned=true unaligned=true mismatch=true reclaimed=true\n";
    return okay ? 0 : 1;
}
