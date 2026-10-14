// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#include <libbeam/engine.hpp>
#include "test_process.hpp"
#include <fstream>
#include <gtest/gtest.h>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string_view>

namespace {
struct Fixture {
    std::string path;
    std::string module;
    std::string function;
} fixture;

libbeam::Deadline deadline() {
    return std::chrono::steady_clock::now() + std::chrono::seconds(5);
}
template<class T> T take(libbeam::Result<T> result) {
    if (const auto* error = std::get_if<libbeam::Error>(&result))
        throw std::runtime_error(error->message);
    return std::get<T>(std::move(result));
}
void append_atom(libbeam::Bytes& bytes, std::string_view atom) {
    if (atom.empty() || atom.size() > 255) throw std::runtime_error("Invalid test atom length");
    bytes.push_back(119); // SMALL_ATOM_UTF8_EXT
    bytes.push_back(static_cast<std::uint8_t>(atom.size()));
    bytes.insert(bytes.end(), atom.begin(), atom.end());
}
libbeam::Bytes request(std::string_view function, std::string_view mode) {
    libbeam::Bytes bytes{131, 104, 3}; // ETF version, small tuple, arity three
    append_atom(bytes, function);
    append_atom(bytes, mode);
    bytes.insert(bytes.end(), {97, 7}); // opaque numeric seed: SMALL_INTEGER_EXT
    return bytes;
}
void execute_contract(std::string_view mode) {
    SCOPED_TRACE(std::string(mode));
    std::ifstream input(fixture.path, std::ios::binary);
    ASSERT_TRUE(input.good()) << fixture.path;
    const libbeam::Bytes beam{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    auto engine = take(libbeam::Engine::create());
    {
        auto isolate = take(engine.create_isolate());
        const auto loaded = isolate.load_module({beam.data(), beam.size()});
        ASSERT_FALSE(loaded) << (loaded ? loaded->message : "");
        const auto payload = request(fixture.function, mode);
        {
            auto call = take(isolate.start(fixture.module, "entry", {payload.data(), payload.size()}));
            const auto result = call.wait_until(deadline());
            const auto* error = std::get_if<libbeam::Error>(&result);
            ASSERT_EQ(error, nullptr) << (error ? error->message : "");
            EXPECT_EQ(std::get<libbeam::Bytes>(result), (libbeam::Bytes{'o', 'k'}));
        }
        // A successful contract must not leave the world unusable or replace code.
        {
            auto call = take(isolate.call(fixture.module, "identity", {payload.data(), payload.size()}));
            EXPECT_EQ(take(call.wait_until(deadline())), payload);
        }
        auto receipt = take(isolate.stop());
        ASSERT_FALSE(receipt.wait_until(deadline()));
        const auto usage = take(isolate.resource_usage());
        EXPECT_TRUE(usage.closed);
        EXPECT_EQ(usage.memory_current, 0u);
    }
    EXPECT_FALSE(engine.shutdown(deadline()));
}
TEST(ExactMfa, DirectCompilerProducedCall) { execute_contract("direct"); }
TEST(ExactMfa, DynamicExportedCall) { execute_contract("dynamic"); }
} // namespace

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    if (argc != 4) { std::cerr << "Expected BEAM runfile, module and function\n"; return 2; }
    fixture = {libbeam_test::runfile(argv[1]), argv[2], argv[3]};
    return RUN_ALL_TESTS();
}
