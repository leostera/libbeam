// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#include "test_process.hpp"
#include <csignal>
#include <gtest/gtest.h>

namespace {
using libbeam_test::run;
using libbeam_test::runfile;

TEST(Process, ExactExitStatus) {
    const auto result = run(runfile("libbeam/process_fixture"), {"exit-seven"});
    ASSERT_TRUE(std::holds_alternative<libbeam_test::Exited>(result.termination));
    EXPECT_EQ(std::get<libbeam_test::Exited>(result.termination).code, 7);
    EXPECT_TRUE(result.exited(7));
    EXPECT_FALSE(result.exited(0));
    EXPECT_TRUE(result.standard_output.empty());
    EXPECT_TRUE(result.standard_error.empty());
}
TEST(Process, SignalIsNotAnExitCode) {
    const auto result = run(runfile("libbeam/process_fixture"), {"signal"});
    ASSERT_TRUE(std::holds_alternative<libbeam_test::Signaled>(result.termination));
    EXPECT_EQ(std::get<libbeam_test::Signaled>(result.termination).signal, SIGTERM);
    EXPECT_FALSE(result.exited(SIGTERM));
    EXPECT_FALSE(result.exited(0));
}
TEST(Process, CapturesBothStreams) {
    const auto result = run(runfile("libbeam/process_fixture"), {"output"});
    EXPECT_TRUE(result.exited(0));
    EXPECT_EQ(result.standard_output, "stdout\n");
    EXPECT_EQ(result.standard_error, "stderr\n");
    EXPECT_EQ(result.output, "stdout\nstderr\n");
}
TEST(Process, DrainsOutputBeforeWaiting) {
    const auto result = run(runfile("libbeam/process_fixture"), {"large-output"});
    EXPECT_TRUE(result.exited(0));
    EXPECT_EQ(result.output, std::string(100000, 'x'));
}
TEST(Process, DrainsBothPipesWithoutLosingTheirIdentity) {
    const auto result = run(runfile("libbeam/process_fixture"), {"large-both-streams"});
    EXPECT_TRUE(result.exited(0));
    EXPECT_EQ(result.standard_output, std::string(100000, 'x'));
    EXPECT_EQ(result.standard_error, std::string(100000, 'y'));
}
TEST(Process, SpawnFailureIsNotSuccessfulExecution) {
    EXPECT_THROW(run(runfile("missing-executable")), std::runtime_error);
}
}  // namespace
