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
std::string beam_path, module, mfa, category;
libbeam::Deadline deadline() { return std::chrono::steady_clock::now() + std::chrono::seconds(5); }
template<class T> T take(libbeam::Result<T> result) {
    if (const auto* error = std::get_if<libbeam::Error>(&result)) throw std::runtime_error(error->message);
    return std::get<T>(std::move(result));
}
void reclaim(libbeam::Isolate& isolate) {
    auto receipt = take(isolate.stop());
    ASSERT_FALSE(receipt.wait_until(deadline()));
    const auto usage = take(isolate.resource_usage());
    EXPECT_TRUE(usage.closed);
    EXPECT_EQ(usage.memory_current, 0u);
}
TEST(ExactMfa, AdmissionSucceedsAndInvocationReportsItsCapabilityBoundary) {
    std::ifstream input(beam_path, std::ios::binary);
    ASSERT_TRUE(input.good()) << beam_path;
    const libbeam::Bytes beam{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    auto engine = take(libbeam::Engine::create());
    {
        auto isolate = take(engine.create_isolate());
        auto peer = take(engine.create_isolate());
        for (auto* world : {&isolate, &peer}) {
            const auto loaded = world->load_module({beam.data(), beam.size()});
            ASSERT_FALSE(loaded) << (loaded ? loaded->message : "");
        }
        {
            auto call = take(isolate.start(module, "entry", {nullptr, 0}));
            const auto result = call.wait_until(deadline());
            ASSERT_TRUE(std::holds_alternative<libbeam::Error>(result));
            const auto& error = std::get<libbeam::Error>(result);
            EXPECT_EQ(error.code, libbeam::ErrorCode::capability_denied);
            EXPECT_EQ(error.message, "erlang:" + mfa + " denied: " + category);
            const auto consumed = call.wait_until(deadline());
            ASSERT_TRUE(std::holds_alternative<libbeam::Error>(consumed));
            EXPECT_EQ(std::get<libbeam::Error>(consumed).code, libbeam::ErrorCode::closed);
        }
        EXPECT_FALSE(take(isolate.resource_usage()).closed);
        const libbeam::Bytes payload{'o', 'k'};
        {
            auto reused = take(isolate.call(module, "identity", {payload.data(), payload.size()}));
            EXPECT_EQ(take(reused.wait_until(deadline())), payload);
            auto unaffected = take(peer.start(module, "identity", {payload.data(), payload.size()}));
            EXPECT_EQ(take(unaffected.wait_until(deadline())), payload);
        }
        reclaim(isolate);
        reclaim(peer);
    }
    EXPECT_FALSE(engine.shutdown(deadline()));
}
}  // namespace

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    if (argc != 5) { std::cerr << "Expected BEAM runfile, module, exact MFA and denial category\n"; return 2; }
    beam_path = libbeam_test::runfile(argv[1]); module = argv[2]; mfa = argv[3]; category = argv[4];
    return RUN_ALL_TESTS();
}
