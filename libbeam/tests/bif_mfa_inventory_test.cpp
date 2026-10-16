// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#include "test_process.hpp"
#include <fstream>
#include <gtest/gtest.h>
#include <iostream>
#include <iterator>
#include <regex>
#include <set>
#include <string>

namespace {
std::string request_path, build_path;
TEST(MfaTargets, ExactlyOneRealTargetForEveryRequestedMfa) {
    std::ifstream request(request_path);
    ASSERT_TRUE(request.good());
    std::set<std::string> requested, expected_targets;
    for (std::string line; std::getline(request, line);) {
        if (line.empty() || line.front() == '#') continue;
        ASSERT_TRUE(requested.insert(line).second) << "Duplicate request: " << line;
        const auto slash = line.rfind('/');
        ASSERT_NE(slash, std::string::npos) << line;
        auto function = line.substr(0, slash);
        if (function.size() > 1 && function.front() == '\'' && function.back() == '\'')
            function = function.substr(1, function.size() - 2);
        if (function == "+") function = "unary_plus";
        else if (function == "/") function = "divide";
        EXPECT_TRUE(expected_targets.insert("bif_" + function + "_" + line.substr(slash + 1) + "_test").second);
    }
    ASSERT_EQ(requested.size(), 188u);
    std::ifstream input(build_path);
    ASSERT_TRUE(input.good());
    const std::string build{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    // Read the actual plain declarations, not a second inventory generated from
    // a table that could disagree with the tests Bazel registers.
    const std::regex declaration(R"rx(cc_test\(\s*name\s*=\s*"(bif_[a-z0-9_]+_[0-9]+_test)")rx");
    std::set<std::string> targets;
    for (std::sregex_iterator it(build.begin(), build.end(), declaration), end; it != end; ++it)
        EXPECT_TRUE(targets.insert((*it)[1].str()).second) << "Duplicate MFA target: " << (*it)[1].str();
    EXPECT_EQ(targets, expected_targets);
}
} // namespace
int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    if (argc != 3) { std::cerr << "Expected request list and BUILD.bazel\n"; return 2; }
    request_path = libbeam_test::runfile(argv[1]);
    build_path = libbeam_test::runfile(argv[2]);
    return RUN_ALL_TESTS();
}
