// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#include "preloaded_generator.hpp"
#include <gtest/gtest.h>
#include "libbeam/tests/test_process.hpp"
#include "bazel/preloaded_fixture.inc"
#include <fstream>
#include <iterator>

namespace {
std::string compiled_beam_path;
TEST(PreloadedGenerator, RealCompilerBeamSurvivesEmbeddingAndCxxCompilation) {
    std::ifstream input(compiled_beam_path, std::ios::binary);
    ASSERT_TRUE(input.good());
    const std::string beam{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    ASSERT_GT(beam.size(), 12u);
    EXPECT_EQ(beam.substr(0, 4), "FOR1");
    EXPECT_EQ(beam.substr(8, 4), "BEAM");
    const std::string embedded(reinterpret_cast<const char*>(lb_preloaded_erlang), sizeof(lb_preloaded_erlang));
    EXPECT_EQ(embedded, beam);
}
const std::string source =
    "-module(erlang).\n"
    "spawn(F) when erlang:is_function(F) ->\n    retained(F);\nspawn(F) -> badarg_with_info([F]).\n"
    "unselected() -> forbidden.\n"
    "spawn_link(F) when erlang:is_function(F, 0) ->\n    linked_body(F);\nspawn_link(F) -> badarg_with_info([F]).\n"
    "spawn_monitor(F) when erlang:is_function(F, 0) ->\n    monitor_body(F);\nspawn_monitor(F) -> badarg_with_info([F]).\n"
    "badarg_with_info(Args) ->\n    erlang:error(badarg, Args, [{error_info, #{module => erl_erts_errors}}]).\n";
TEST(PreloadedGenerator, RetainsAllClausesAndHelperBodyWithoutUnselectedCode) {
    const auto result = libbeam_build::extract_preloaded(source);
    EXPECT_NE(result.find("spawn(F) when erlang:is_function(F) ->\n    retained(F);\nspawn(F) -> badarg_with_info([F]).\n"), std::string::npos);
    EXPECT_NE(result.find("spawn_monitor(F) when erlang:is_function(F, 0) ->\n    monitor_body(F);\nspawn_monitor(F) -> badarg_with_info([F]).\n"), std::string::npos);
    EXPECT_NE(result.find("erlang:error(badarg, Args, [{error_info, #{module => erl_erts_errors}}]).\n"), std::string::npos);
    EXPECT_NE(result.find("spawn_link(F) when erlang:is_function(F, 0) ->\n    linked_body(F);\nspawn_link(F) -> badarg_with_info([F]).\n"), std::string::npos);
    EXPECT_EQ(result.find("unselected"), std::string::npos);
    EXPECT_NE(result.find("-compile(no_auto_import)."), std::string::npos);
}
TEST(PreloadedGenerator, RefusesMissingAmbiguousAndTruncatedFunctions) {
    EXPECT_THROW(libbeam_build::extract_preloaded(""), std::runtime_error);
    EXPECT_THROW(libbeam_build::extract_preloaded(source + source), std::runtime_error);
    EXPECT_THROW(libbeam_build::extract_preloaded(source.substr(0, source.size() - 2)), std::runtime_error);
}
TEST(PreloadedGenerator, EmbedsUnsignedBytesExactlyAndDeterministically) {
    const char raw[] = {'F','O','R','1',0,0,0,6,'B','E','A','M',0,static_cast<char>(255)};
    const std::string beam(raw, sizeof(raw));
    const auto result = libbeam_build::embed_preloaded(beam);
    EXPECT_NE(result.find("70,79,82,49,0,0,0,6,66,69,65,77,0,255,\n};\n"), std::string::npos);
    EXPECT_EQ(libbeam_build::embed_preloaded(beam), result);
}
TEST(PreloadedGenerator, RejectsNonBeamAndIncorrectContainerLength) {
    EXPECT_THROW(libbeam_build::embed_preloaded(""), std::runtime_error);
    EXPECT_THROW(libbeam_build::embed_preloaded("FOR1xxxxBEAM"), std::runtime_error);
    const char truncated[] = {'F','O','R','1',0,0,0,8,'B','E','A','M'};
    EXPECT_THROW(libbeam_build::embed_preloaded(std::string(truncated, sizeof(truncated))), std::runtime_error);
}
} // namespace
int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    if (argc != 2) return 2;
    compiled_beam_path = libbeam_test::runfile(argv[1]);
    return RUN_ALL_TESTS();
}
