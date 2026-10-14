// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#include "test_process.hpp"
#include <gtest/gtest.h>
namespace {
std::string escript, beam;
TEST(GcRootsReference, RealOtpPreservesAllTenFixtureResults) {
    const auto result = libbeam_test::run(escript, {
        libbeam_test::runfile("libbeam/tests/fixtures/gc_roots_reference.escript"), beam});
    ASSERT_TRUE(result.exited(0)) << result.output;
    EXPECT_EQ(result.standard_output, "GC_REFERENCE_OK roots=10\n");
    EXPECT_TRUE(result.standard_error.empty()) << result.standard_error;
}
}
int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    if (argc != 3) return 2;
    escript = libbeam_test::runfile(argv[1]);
    beam = libbeam_test::runfile(argv[2]);
    return RUN_ALL_TESTS();
}
