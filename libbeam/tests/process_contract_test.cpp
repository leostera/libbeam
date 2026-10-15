// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#include <libbeam/engine.hpp>
#include "test_process.hpp"
#include <algorithm>
#include <fstream>
#include <gtest/gtest.h>
#include <iterator>
#include <stdexcept>
namespace {
using namespace libbeam;
std::string path, function;
Deadline deadline() { return std::chrono::steady_clock::now() + std::chrono::seconds(10); }
template<class T> T take(Result<T> result) {
    if (const auto* error = std::get_if<Error>(&result)) throw std::runtime_error(error->message);
    return std::get<T>(std::move(result));
}
void stop(Isolate& isolate) {
    auto receipt = take(isolate.stop());
    EXPECT_FALSE(receipt.wait_until(deadline()));
    const auto usage = take(isolate.resource_usage());
    EXPECT_TRUE(usage.closed);
    EXPECT_EQ(usage.memory_current, 0u);
}
TEST(ProcessContract, RealEffectsSingleCompletionAndPeerSurvival) {
    SCOPED_TRACE(function);
    std::ifstream file(path, std::ios::binary);
    ASSERT_TRUE(file.good());
    Bytes beam{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    auto engine = take(Engine::create());
    {
        auto isolate = take(engine.create_isolate());
        auto peer = take(engine.create_isolate());
        for (auto* target : {&isolate, &peer}) {
            const auto loaded = target->load_module({beam.data(), beam.size()});
            ASSERT_FALSE(loaded) << (loaded ? loaded->message : "");
        }
        std::fill(beam.begin(), beam.end(), 0xa5);
        const Bytes input{0, 1, 255, 127, 0, 42};
        const ByteView payload{input.data(), input.size()};
        {
            auto pending = take(isolate.start("process_contracts", function, payload));
            auto other = take(peer.start("process_contracts", "identity", payload));
            EXPECT_EQ(take(other.wait_until(deadline())), input);
            const auto completed = pending.wait_until(deadline());
            const auto* completed_error = std::get_if<Error>(&completed);
            ASSERT_EQ(completed_error, nullptr) << (completed_error ? completed_error->message : "");
            EXPECT_EQ(std::get<Bytes>(completed), (Bytes{'o','k'}));
            const auto consumed = pending.wait_until(deadline());
            const auto* error = std::get_if<Error>(&consumed);
            ASSERT_NE(error, nullptr);
            EXPECT_EQ(error->code, ErrorCode::closed);
        }
        {
            auto reused = take(isolate.call("process_contracts", "identity", payload));
            EXPECT_EQ(take(reused.wait_until(deadline())), input);
        }
        stop(isolate);
        {
            auto survived = take(peer.call("process_contracts", "identity", payload));
            EXPECT_EQ(take(survived.wait_until(deadline())), input);
        }
        stop(peer);
    }
    EXPECT_FALSE(engine.shutdown(deadline()));
}
}
int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    if (argc != 3) return 2;
    path = libbeam_test::runfile(argv[1]);
    function = argv[2];
    return RUN_ALL_TESTS();
}
