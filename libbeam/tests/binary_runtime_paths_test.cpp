// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#include <libbeam/engine.hpp>
#include "test_process.hpp"
#include <fstream>
#include <gtest/gtest.h>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
std::string module_path;
std::string shim_path;
libbeam::Deadline deadline() {
    return std::chrono::steady_clock::now() + std::chrono::seconds(10);
}
template<class T> T take(libbeam::Result<T> result) {
    if (const auto* error = std::get_if<libbeam::Error>(&result))
        throw std::runtime_error(error->message);
    return std::get<T>(std::move(result));
}
libbeam::Bytes beam(const char* path) {
    std::ifstream input(libbeam_test::runfile(path), std::ios::binary);
    if (!input) throw std::runtime_error(std::string("cannot open ") + path);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
void append_atom(libbeam::Bytes& bytes, std::string_view atom) {
    bytes.push_back(119); // SMALL_ATOM_UTF8_EXT
    bytes.push_back(static_cast<std::uint8_t>(atom.size()));
    bytes.insert(bytes.end(), atom.begin(), atom.end());
}
libbeam::Bytes request(std::string_view function, std::string_view mode) {
    libbeam::Bytes bytes{131, 104, 3}; // ETF version, tuple arity three
    append_atom(bytes, function);
    append_atom(bytes, mode);
    bytes.insert(bytes.end(), {97, 7});
    return bytes;
}

TEST(BinaryRuntimePaths, UtfFloatUnicodeCounterAndGcExecute) {
    const auto module = beam(module_path.c_str());
    const auto shim = beam(shim_path.c_str());
    auto engine = take(libbeam::Engine::create());
    {
    auto isolate = take(engine.create_isolate());
    const std::vector<libbeam::ByteView> modules = {
        {shim.data(), shim.size()}, {module.data(), module.size()}};
    const auto loaded = isolate.load_modules(modules);
    ASSERT_FALSE(loaded) << (loaded ? loaded->message : "");

    const auto input = request("runtime_paths", "direct");
    auto call = take(isolate.start("binary_runtime_paths", "entry", {input.data(), input.size()}));
    const auto result = call.wait_until(deadline());
    const auto* error = std::get_if<libbeam::Error>(&result);
    ASSERT_EQ(error, nullptr) << (error ? error->message : "");
    EXPECT_EQ(std::get<libbeam::Bytes>(result), (libbeam::Bytes{'o', 'k'}));

    auto identity = take(isolate.call("binary_runtime_paths", "identity", {input.data(), input.size()}));
    EXPECT_EQ(take(identity.wait_until(deadline())), input);
    auto receipt = take(isolate.stop());
    ASSERT_FALSE(receipt.wait_until(deadline()));
    EXPECT_EQ(take(isolate.resource_usage()).memory_current, 0u);
    }
    {
        libbeam::ResourceOverrides overrides;
        overrides.invocation_work_limit = 1;
        auto isolate = take(engine.create_isolate(overrides));
        const std::vector<libbeam::ByteView> modules = {
            {shim.data(), shim.size()}, {module.data(), module.size()}};
        const auto loaded = isolate.load_modules(modules);
        ASSERT_FALSE(loaded) << (loaded ? loaded->message : "");
        const auto input = request("runtime_paths", "direct");
        auto started = isolate.start("binary_runtime_paths", "entry", {input.data(), input.size()});
        if (const auto* error = std::get_if<libbeam::Error>(&started)) {
            EXPECT_EQ(error->code, libbeam::ErrorCode::work_limit);
        } else {
            auto call = std::get<libbeam::Call>(std::move(started));
            const auto result = call.wait_until(deadline());
            const auto* wait_error = std::get_if<libbeam::Error>(&result);
            ASSERT_NE(wait_error, nullptr);
            EXPECT_EQ(wait_error->code, libbeam::ErrorCode::work_limit);
        }
        auto receipt = take(isolate.stop());
        ASSERT_FALSE(receipt.wait_until(deadline()));
    }
    EXPECT_FALSE(engine.shutdown(deadline()));
}
} // namespace

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    if (argc != 3) return 2;
    module_path = argv[1];
    shim_path = argv[2];
    return RUN_ALL_TESTS();
}
