// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#include "test_process.hpp"
#include <gtest/gtest.h>

namespace {
struct Execution {
    const char* program;
    std::vector<std::string> modules;
    std::vector<std::string> required_markers;
};
class ExecutionEvidence : public testing::TestWithParam<Execution> {};
TEST_P(ExecutionEvidence, FixtureBranchesActuallyExecuted) {
    const auto& test = GetParam();
    std::vector<std::string> args;
    for (const auto& module : test.modules)
        args.push_back(libbeam_test::runfile("libbeam/fixtures/" + module + "_fixture/" + module + ".beam"));
    const auto result = libbeam_test::run(libbeam_test::runfile(std::string("libbeam/") + test.program), args);
    ASSERT_TRUE(result.exited(0)) << result.output;
    for (const auto& marker : test.required_markers)
        EXPECT_NE(result.standard_output.find(marker), std::string::npos) << "Missing " << marker << '\n' << result.output;
}
INSTANTIATE_TEST_SUITE_P(Native, ExecutionEvidence, testing::Values(
    Execution{"core_flatmap_test", {"binary_slice"}, {"CORE_BINARY_ERROR_PAYLOAD_OK"}},
    Execution{"core_binary_transform_test", {"binary_create_slice"}, {"CORE_BINARY_TRANSFORM_FIXTURE_OK"}},
    Execution{"core_binary_flow_test", {"binary_flow_slice"}, {"CORE_BINARY_FLOW_FIXTURE_OK"}},
    Execution{"core_writable_init_test", {"writable_init_slice"}, {"CORE_WRITABLE_INIT_EXECUTION_OK", "CORE_WRITABLE_INIT_WORKER_OK"}},
    Execution{"core_binary_build_test", {"binary_construct_slice", "binary_control_slice"}, {"CORE_BINARY_BUILD_EXECUTION_OK", "native=2:integer:size"}},
    Execution{"core_binary_emit_test", {"binary_create_slice", "binary_slice"}, {"CORE_BINARY_EMIT_FIXTURE_OK"}},
    Execution{"core_binary_size_test", {"binary_slice"}, {"CORE_BINARY_SIZE_EXECUTION_OK"}},
    Execution{"core_binary_vheap_test", {"binary_slice"}, {"CORE_BINARY_VHEAP_EXECUTION_OK"}},
    Execution{"core_exception_test", {"exception_slice"}, {"CORE_EXCEPTION_EXECUTION_OK"}},
    Execution{"core_binary_test", {"binary_slice", "first_slice", "code_peer"}, {
        "CORE_BINARY_EXECUTION_OK", "CORE_BINARY_RETIREMENT_OK", "CORE_WRITABLE_OK", "CORE_BINARY_BEGIN_OK",
        "CORE_BINARY_SHARED_EDGE_OK", "CORE_PRIVATE_APPEND_OK"}}
), [](const testing::TestParamInfo<Execution>& info) { return std::string(info.param.program); });
}  // namespace
