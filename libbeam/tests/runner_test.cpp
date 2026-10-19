// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#include "test_process.hpp"
#include <gtest/gtest.h>

namespace {
using libbeam_test::run;
using libbeam_test::runfile;

std::string fixture(const std::string& module) {
    return runfile("libbeam/fixtures/" + module + "_fixture/" + module + ".beam");
}

TEST(Runner, Help) {
    const auto result = run(runfile("libbeam/run_beam"), {"--help"});
    EXPECT_TRUE(result.exited(0)) << result.output;
    EXPECT_NE(result.output.find("--isolate"), std::string::npos);
}

TEST(Runner, RejectsDependencyWithoutIsolate) {
    const auto result = run(runfile("libbeam/run_beam"), {"--dependency", "missing.beam"});
    EXPECT_TRUE(result.exited(2)) << result.output;
}

TEST(Runner, EagerDependencyGroupsAndRepeatedRequests) {
    const auto result = run(runfile("libbeam/run_beam"), {
        "--entry", "batch_alpha", "entry",
        "--isolate", fixture("batch_alpha"), "--dependency", fixture("batch_beta"),
        "--isolate", fixture("batch_alpha"), "--dependency", fixture("batch_beta"),
        "--call", "payload", "--expect", "payload", "--call", "repeated", "--expect", "repeated",
    });
    ASSERT_TRUE(result.exited(0)) << result.output;
    EXPECT_NE(result.output.find("BEAM_RUNNER_DONE isolates=2 success=true reclaimed=true engine_shutdown=true"), std::string::npos) << result.output;
}

TEST(Runner, MissingDependencyIsNotLazyLoaded) {
    const auto result = run(runfile("libbeam/run_beam"), {
        "--entry", "batch_alpha", "entry", "--isolate", fixture("batch_alpha"),
        "--call", "must-not-run",
    });
    EXPECT_TRUE(result.exited(1)) << result.output;
    EXPECT_NE(result.output.find("success=false reclaimed=true engine_shutdown=true"), std::string::npos) << result.output;
}

TEST(Runner, MissingDependencyRaisesGuestExceptionAfterAdmission) {
    const auto result = run(runfile("libbeam/run_beam"), {
        "--entry", "batch_alpha", "entry", "--call", "dependency",
        "--isolate", fixture("batch_alpha"),
    });
    EXPECT_TRUE(result.exited(1)) << result.output;
    EXPECT_NE(result.output.find("admitted=true"), std::string::npos) << result.output;
    EXPECT_NE(result.output.find("error=guest exception"), std::string::npos) << result.output;
}

TEST(Runner, UnusedOnLoadRefusesWholeModuleBeforeAnyCallResult) {
    const auto result = run(runfile("libbeam/run_beam"), {
        "--entry", "module_on_load_refused", "identity", "--call", "must-not-run",
        "--isolate", fixture("module_on_load_refused"),
    });
    EXPECT_TRUE(result.exited(1)) << result.output;
    EXPECT_NE(result.output.find("stage=on_load"), std::string::npos) << result.output;
    EXPECT_EQ(result.output.find("bytes="), std::string::npos) << result.output;
}

TEST(Runner, FileInputIsExecutedIndependentlyInBothIsolates) {
    const auto input = runfile("libbeam/tests/fixtures/application-input.txt");
    const auto result = run(runfile("libbeam/run_beam"), {
        "--entry", "async_slice", "identity", "--call-file", input, "--expect-file", input,
        "--isolate", fixture("async_slice"), "--isolate", fixture("async_slice"),
    });
    ASSERT_TRUE(result.exited(0)) << result.output;
    EXPECT_NE(result.output.find("BEAM_RUNNER_DONE isolates=2 success=true reclaimed=true engine_shutdown=true"), std::string::npos) << result.output;
}

TEST(Runner, OriginalPeerDependencyGroupsExecuteOnBothIsolates) {
    const auto result = run(runfile("libbeam/run_beam"), {
        "--entry", "first_slice", "identity", "--call", "dependency", "--expect", "dependency",
        "--isolate", fixture("code_peer"), "--dependency", fixture("first_slice"),
        "--isolate", fixture("code_peer"), "--dependency", fixture("first_slice"),
    });
    ASSERT_TRUE(result.exited(0)) << result.output;
    EXPECT_NE(result.output.find("BEAM_RUNNER_DONE isolates=2 success=true reclaimed=true engine_shutdown=true"), std::string::npos) << result.output;
}

TEST(Runner, StatefulBootAndRepeatedCallsRemainIndependent) {
    const auto probe = runfile("libbeam/fixtures/probe_a_fixture/probe.beam");
    const auto result = run(runfile("libbeam/run_beam"), {
        "--entry", "probe", "request", "--boot", "boot", "", "--expect-boot", "A:ready",
        "--call", "next", "--expect", "A:1", "--call", "next", "--expect", "A:2",
        "--call", "read", "--expect", "A:2", "--isolate", probe, "--isolate", probe,
    });
    ASSERT_TRUE(result.exited(0)) << result.output;
    EXPECT_NE(result.output.find("BEAM_RUNNER_DONE isolates=2 success=true reclaimed=true engine_shutdown=true"), std::string::npos) << result.output;
}

TEST(Runner, UnusedUnsupportedFunctionRefusesWholeModule) {
    const auto result = run(runfile("libbeam/run_beam"), {
        "--entry", "batch_unsupported", "identity", "--isolate", fixture("batch_unsupported"),
        "--call", "must-not-run",
    });
    EXPECT_TRUE(result.exited(1)) << result.output;
    EXPECT_NE(result.output.find("success=false reclaimed=true engine_shutdown=true"), std::string::npos) << result.output;
}

struct LanguageCase { const char* module; const char* function; const char* input; const char* expected; };
class LanguageRunner : public testing::TestWithParam<LanguageCase> {};
TEST_P(LanguageRunner, ExecutesRealClosureShapeOrDecimalConversion) {
    const auto test = GetParam();
    const auto result = run(runfile("libbeam/run_beam"), {
        "--entry", test.module, test.function, "--call", test.input, "--expect", test.expected,
        "--isolate", fixture(test.module),
    });
    ASSERT_TRUE(result.exited(0)) << result.output;
    EXPECT_NE(result.output.find("success=true reclaimed=true engine_shutdown=true"), std::string::npos) << result.output;
}
INSTANTIATE_TEST_SUITE_P(LanguageFeatures, LanguageRunner, testing::Values(
    LanguageCase{"fun_slice", "nested", "captured", "captured"},
    LanguageCase{"shape_slice", "list_binary", "shape", "shape"},
    LanguageCase{"decimal_slice", "negative", "", "-18446744073709551617"}
));

class MapRunner : public testing::TestWithParam<std::pair<const char*, const char*>> {};
TEST_P(MapRunner, ExecutesAndReclaims) {
    const auto [function, expected] = GetParam();
    const auto result = run(runfile("libbeam/run_beam"), {
        "--entry", "map_slice", function, "--call", "payload", "--expect", expected,
        "--isolate", fixture("map_slice"),
    });
    ASSERT_TRUE(result.exited(0)) << result.output;
    EXPECT_NE(result.output.find("success=true reclaimed=true engine_shutdown=true"), std::string::npos) << result.output;
}
INSTANTIATE_TEST_SUITE_P(NativeMaps, MapRunner, testing::Values(
    std::make_pair("new_binary", "map-ok"),
    std::make_pair("add_created_binary", "assoc-ok"),
    std::make_pair("update_created_binary", "exact-ok"),
    std::make_pair("single_created_binary", "single-ok"),
    std::make_pair("dynamic_binary", "dynamic-ok")
));

TEST(Lifecycle, IndependentStateReplacementAndShutdown) {
    const auto result = run(runfile("libbeam/two_isolates"), {
        runfile("libbeam/fixtures/probe_a_fixture/probe.beam"),
        runfile("libbeam/fixtures/probe_b_fixture/probe.beam"),
    });
    ASSERT_TRUE(result.exited(0)) << result.output;
    EXPECT_NE(result.output.find("two isolates, independent state, replacement, clean shutdown: OK"), std::string::npos) << result.output;
}

TEST(Lifecycle, RejectsMalformedBeamBeforeExecution) {
    const auto malformed = runfile("libbeam/tests/fixtures/malformed-beam.txt");
    const auto result = run(runfile("libbeam/two_isolates"), {malformed, malformed});
    EXPECT_TRUE(result.exited(1)) << result.output;
    EXPECT_NE(result.standard_error.find("invalid BEAM image"), std::string::npos) << result.output;
    EXPECT_EQ(result.output.find(": OK"), std::string::npos) << result.output;
}

TEST(Lifecycle, CreateShutdownCreate) {
    const auto result = run(runfile("libbeam/engine_lifecycle"));
    ASSERT_TRUE(result.exited(0)) << result.output;
    EXPECT_NE(result.output.find("ENGINE_LIFECYCLE_OK create_shutdown_create=true"), std::string::npos) << result.output;
}
}  // namespace
