// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#include <libbeam/engine.hpp>
#include "test_process.hpp"
#include <fstream>
#include <gtest/gtest.h>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {
std::string beam_path;
constexpr auto module = "bif_load_module_2";
libbeam::Deadline deadline() { return std::chrono::steady_clock::now() + std::chrono::seconds(5); }
template<class T> T take(libbeam::Result<T> result) {
    if (const auto* error = std::get_if<libbeam::Error>(&result)) throw std::runtime_error(error->message);
    return std::get<T>(std::move(result));
}
TEST(LoadModule2, AcknowledgesOnlyPublishedCodeAndDoesNotInstallPayload) {
    std::ifstream input(beam_path, std::ios::binary);
    ASSERT_TRUE(input.good());
    const libbeam::Bytes beam{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    auto engine = take(libbeam::Engine::create());
    {
        auto isolate = take(engine.create_isolate());
        const auto loaded = isolate.load_module({beam.data(), beam.size()});
        ASSERT_FALSE(loaded) << (loaded ? loaded->message : "");
        const libbeam::Bytes not_beam{'n', 'o', 't', ' ', 'B', 'E', 'A', 'M'};
        const libbeam::ByteView bytes{not_beam.data(), not_beam.size()};
        {
            auto present = take(isolate.start(module, "present", bytes));
            const auto response = take(present.wait_until(deadline()));
            EXPECT_EQ(std::string(response.begin(), response.end()), "already loaded");
        }
        {
            auto absent = take(isolate.call(module, "absent", bytes));
            const auto result = absent.wait_until(deadline());
            ASSERT_TRUE(std::holds_alternative<libbeam::Error>(result));
            EXPECT_EQ(std::get<libbeam::Error>(result).code, libbeam::ErrorCode::capability_denied);
            EXPECT_EQ(std::get<libbeam::Error>(result).message, "erlang:load_module/2 denied: CODE_LOADING");
        }
        {
            auto invalid = take(isolate.call(module, "bad_type", bytes));
            const auto result = invalid.wait_until(deadline());
            ASSERT_TRUE(std::holds_alternative<libbeam::Error>(result));
            EXPECT_EQ(std::get<libbeam::Error>(result).code, libbeam::ErrorCode::exception);
        }
        {
            auto retained = take(isolate.call(module, "identity", bytes));
            EXPECT_EQ(take(retained.wait_until(deadline())), not_beam);
        }
        auto receipt = take(isolate.stop());
        ASSERT_FALSE(receipt.wait_until(deadline()));
        const auto usage = take(isolate.resource_usage());
        EXPECT_TRUE(usage.closed);
        EXPECT_EQ(usage.memory_current, 0u);
    }
    EXPECT_FALSE(engine.shutdown(deadline()));
}
}  // namespace
int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    if (argc != 2) { std::cerr << "Expected BEAM runfile\n"; return 2; }
    beam_path = libbeam_test::runfile(argv[1]);
    return RUN_ALL_TESTS();
}
