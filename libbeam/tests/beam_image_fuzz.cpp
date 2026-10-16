// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#include <libbeam/engine.hpp>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

extern "C" std::size_t LLVMFuzzerMutate(std::uint8_t*, std::size_t, std::size_t, unsigned);

namespace {
using namespace libbeam;

[[noreturn]] __attribute__((noinline)) void fail() { std::abort(); }
Deadline deadline() { return std::chrono::steady_clock::now() + std::chrono::seconds(2); }
ByteView view(const Bytes& bytes) { return {bytes.data(), bytes.size()}; }
std::string fixture_path(const char* path) {
    if (const char* directory = std::getenv("TEST_SRCDIR")) {
        const char* workspace = std::getenv("TEST_WORKSPACE");
        if (!workspace) fail();
        return std::string(directory) + "/" + workspace + "/" + path;
    }
    if (const char* directory = std::getenv("RUNFILES_DIR"))
        return std::string(directory) + "/_main/" + path;
    fail();
}
Bytes read_fixture(const char* path) {
    std::ifstream input(fixture_path(path), std::ios::binary);
    if (!input) fail();
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
void finish_stop(Isolate& isolate, Result<Reclamation> stopped) {
    if (auto* reclamation = std::get_if<Reclamation>(&stopped)) {
        if (reclamation->wait_until(deadline())) fail();
        return;
    }
    const auto& error = std::get<Error>(stopped);
    auto usage = isolate.resource_usage();
    if (error.code != ErrorCode::closed || !std::holds_alternative<ResourceUsage>(usage) ||
        !std::get<ResourceUsage>(usage).closed) fail();
}
std::uint32_t read_u32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
           (std::uint32_t(p[2]) << 8) | std::uint32_t(p[3]);
}
bool code_payload(const std::uint8_t* data, std::size_t size,
                  std::size_t* begin, std::size_t* end) {
    if (size < 12 || std::string_view(reinterpret_cast<const char*>(data), 4) != "FOR1" ||
        std::string_view(reinterpret_cast<const char*>(data + 8), 4) != "BEAM" ||
        read_u32(data + 4) != size - 8) return false;
    for (std::size_t offset = 12; offset + 8 <= size;) {
        const std::uint32_t chunk_size = read_u32(data + offset + 4);
        const std::size_t payload = offset + 8;
        if (chunk_size > size - payload) return false;
        if (std::string_view(reinterpret_cast<const char*>(data + offset), 4) == "Code") {
            // Keep the Code chunk header and mutate only compact instruction bytes.
            if (chunk_size < 20) return false;
            *begin = payload + 20;
            *end = payload + chunk_size;
            return *begin < *end;
        }
        const std::size_t padded = (std::size_t(chunk_size) + 3u) & ~std::size_t(3u);
        if (padded > size - payload) return false;
        offset = payload + padded;
    }
    return false;
}
void exercise(const std::uint8_t* data, std::size_t size) {
    constexpr std::size_t kFuzzInputLimit = 256u * 1024u;
    if (!size || size > kFuzzInputLimit) return;
    const auto reference = read_fixture("libbeam/fixtures/probe_a_fixture/probe.beam");
    auto engine_result = Engine::create();
    if (!std::holds_alternative<Engine>(engine_result)) fail();
    auto engine = std::get<Engine>(std::move(engine_result));
    {
        auto a_result = engine.create_isolate();
        auto b_result = engine.create_isolate();
        if (!std::holds_alternative<Isolate>(a_result) || !std::holds_alternative<Isolate>(b_result)) fail();
        auto a = std::get<Isolate>(std::move(a_result));
        auto b = std::get<Isolate>(std::move(b_result));
        const auto image = ByteView{data, size};
        const auto load_a = a.load_module(image);
        const auto load_b = b.load_module(image);
        if (bool(load_a) != bool(load_b)) fail();
        if (load_a && load_a->code != load_b->code) fail();

        // A rejected mutation must not leave either namespace half-published.
        if (load_a) {
            if (a.load_module(view(reference)) || b.load_module(view(reference))) fail();
        } else {
            // Accepted mutations are executable bytecode, not just verifier
            // inputs. Exercise the same admitted image in independent worlds.
            auto start_a = a.start("probe", "boot", {nullptr, 0});
            auto start_b = b.start("probe", "boot", {nullptr, 0});
            if (std::holds_alternative<Call>(start_a) != std::holds_alternative<Call>(start_b)) fail();
            if (std::holds_alternative<Error>(start_a) &&
                std::get<Error>(start_a).code != std::get<Error>(start_b).code) fail();
            if (std::holds_alternative<Call>(start_a)) {
                auto call_a = std::get<Call>(std::move(start_a));
                auto call_b = std::get<Call>(std::move(start_b));
                (void)call_a.wait_until(deadline());
                (void)call_b.wait_until(deadline());
                (void)call_a.cancel();
                (void)call_b.cancel();
            }
        }
        finish_stop(a, a.stop());
        finish_stop(b, b.stop());
    }
    if (engine.shutdown(deadline())) fail();
}
} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    exercise(data, size);
    return 0;
}

extern "C" std::size_t LLVMFuzzerCustomMutator(std::uint8_t* data, std::size_t size,
                                                std::size_t max_size, unsigned seed) {
    std::size_t begin = 0, end = 0;
    if (code_payload(data, size, &begin, &end)) {
        std::uint32_t state = seed ? seed : 1;
        auto next = [&]() {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return state;
        };
        const std::size_t mutations = 1 + next() % 4;
        for (std::size_t i = 0; i < mutations; ++i) {
            const std::size_t at = begin + next() % (end - begin);
            switch (next() % 3) {
            case 0: data[at] ^= std::uint8_t(1u << (next() % 8)); break;
            case 1: data[at] = static_cast<std::uint8_t>(next()); break;
            case 2: ++data[at]; break;
            }
        }
        return size;
    }
    return LLVMFuzzerMutate(data, size, max_size, seed);
}
