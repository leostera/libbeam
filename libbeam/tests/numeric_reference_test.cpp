// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#include "test_process.hpp"
#include <gtest/gtest.h>
#include <iostream>
#include <regex>

namespace {
std::string escript;
TEST(NumericReference, CharacterizesInstalledFloatWithoutClaimingPinnedParity) {
    const auto result = libbeam_test::run(escript, {
        libbeam_test::runfile("libbeam/tests/fixtures/numeric_conversion_reference.escript"),
        libbeam_test::runfile("libbeam/tests/fixtures/float-vectors.txt"),
    });
    ASSERT_TRUE(result.exited(0)) << result.output;
    const std::regex report(R"(^NUMERIC_REFERENCE_CHARACTERIZATION otp=(\S+) erts=(\S+) float_vectors=(\d+) float_differences=(\d+) rounding_vectors_per_mode=(\d+) rounding_modes_passed=(\d+)$)", std::regex::multiline);
    std::smatch fields;
    ASSERT_TRUE(std::regex_search(result.standard_output, fields, report)) << result.output;
    EXPECT_EQ(std::stoul(fields[3]), 3979u);
    EXPECT_LE(std::stoul(fields[4]), 3979u);
    EXPECT_EQ(std::stoul(fields[5]), 751u);
    EXPECT_EQ(std::stoul(fields[6]), 4u);
    RecordProperty("otp_release", fields[1].str());
    RecordProperty("erts_version", fields[2].str());
    RecordProperty("float_differences", fields[4].str());
    if (std::stoul(fields[4]) != 0)
        EXPECT_NE(result.standard_output.find("INSTALLED_FLOAT_DIFFERENCE_FIRST"), std::string::npos);
}
}  // namespace

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    if (argc != 2) { std::cerr << "Expected declared escript runfile\n"; return 2; }
    escript = libbeam_test::runfile(argv[1]);
    return RUN_ALL_TESTS();
}
