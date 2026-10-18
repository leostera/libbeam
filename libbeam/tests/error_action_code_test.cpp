// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#include <libbeam/engine.hpp>
#include "test_process.hpp"
#include <gtest/gtest.h>
#include <fstream>
#include <iterator>
#include <string>
#include <variant>

namespace {
using namespace libbeam;
std::string beam_path;
std::string module_name;

TEST(ErrorActionCode, UncaughtGuestUndefBecomesException)
{
    std::ifstream input(beam_path, std::ios::binary);
    ASSERT_TRUE(input.good()) << beam_path;
    const Bytes beam{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};

    auto engine_result=Engine::create();
    ASSERT_TRUE(std::holds_alternative<Engine>(engine_result));
    Engine engine=std::move(std::get<Engine>(engine_result));
    {
        auto isolate_result=engine.create_isolate();
        ASSERT_TRUE(std::holds_alternative<Isolate>(isolate_result));
        Isolate isolate=std::move(std::get<Isolate>(isolate_result));
        ASSERT_FALSE(isolate.load_module({beam.data(),beam.size()}));

        const Bytes input_term{131,97,7}; // ETF integer 7
        auto call_result=isolate.start(module_name,"uncaught",{input_term.data(),input_term.size()});
        ASSERT_TRUE(std::holds_alternative<Call>(call_result));
        Call call=std::move(std::get<Call>(call_result));
        auto result=call.wait_until(std::chrono::steady_clock::now()+std::chrono::seconds(5));
        ASSERT_TRUE(std::holds_alternative<Error>(result));
        const Error& error=std::get<Error>(result);
        EXPECT_EQ(error.code,ErrorCode::exception);
        EXPECT_EQ(error.message,"guest exception");

        EXPECT_TRUE(std::holds_alternative<Reclamation>(isolate.stop()));
    }
    EXPECT_FALSE(engine.shutdown(std::chrono::steady_clock::now()+std::chrono::seconds(5)));
}
} // namespace

int main(int argc,char** argv)
{
    testing::InitGoogleTest(&argc,argv);
    if(argc!=3) return 2;
    beam_path=libbeam_test::runfile(argv[1]);
    module_name=argv[2];
    return RUN_ALL_TESTS();
}
