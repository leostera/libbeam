// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#include "libbeam/tests/test_process.hpp"
#include <fstream>
#include <gtest/gtest.h>
#include <iomanip>
#include <iterator>
#include <openssl/sha.h>
#include <sstream>

// Numeric identities are not capabilities. Compile the generated immutable data
// in this test without bringing in the runtime or granting any BIF binding.
#define make_atom(index) static_cast<std::size_t>(index)
#include "libbeam/generated/atoms/lb_atoms_generated.h"
#include "libbeam/generated/atoms/lb_atoms_generated.inc"
#include "libbeam/generated/atoms/lb_bif_ids_generated.h"

namespace {
std::string paths[9];
std::string digest(const std::string& bytes) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size(), hash);
    std::ostringstream out;
    for (const auto byte : hash) out << std::hex << std::setfill('0') << std::setw(2) << unsigned(byte);
    return out.str();
}
TEST(AtomInputs, ReviewedOtpSourcesRetainExactIdentity) {
    const char* expected[] = {
        "4503d68bd345492db8127aad8c3ce584319a8355e3e1dafbbed2b680eb20d953",
        "4e6da90ce1effb6d922e01da32dcefd399366fb287b13d04924504d90e4fd309",
        "39f32210feb58f9c60482d7b97db10d21c53a988207fd7a81221ff3506331564",
    };
    for (unsigned i = 0; i < 3; ++i) {
        SCOPED_TRACE(paths[i]);
        std::ifstream input(paths[i], std::ios::binary);
        ASSERT_TRUE(input.good());
        const std::string bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        EXPECT_EQ(digest(bytes), expected[i]);
        EXPECT_NE(digest(bytes + "changed"), expected[i]);
    }
}
TEST(AtomInputs, IndependentGenerationRootsProduceIdenticalOutputs) {
    for (unsigned i = 0; i < 3; ++i) {
        std::ifstream first(paths[3 + i], std::ios::binary);
        std::ifstream second(paths[6 + i], std::ios::binary);
        ASSERT_TRUE(first.good()) << paths[3 + i];
        ASSERT_TRUE(second.good()) << paths[6 + i];
        const std::string a{std::istreambuf_iterator<char>(first), std::istreambuf_iterator<char>()};
        const std::string b{std::istreambuf_iterator<char>(second), std::istreambuf_iterator<char>()};
        EXPECT_FALSE(a.empty());
        EXPECT_EQ(a, b) << paths[3 + i];
    }
}
TEST(AtomInputs, RealGeneratedDataCompilesWithAppendOnlyOwnedIdentities) {
    constexpr auto count = sizeof(lb_predefined_names) / sizeof(lb_predefined_names[0]);
    ASSERT_GT(count, 3u);
    EXPECT_EQ(lb_predefined_names[count - 1], nullptr);
    EXPECT_EQ(am_set_cookie, count - 4);
    EXPECT_EQ(am_load_module, count - 3);
    EXPECT_EQ(am_spawn_monitor, count - 2);
    EXPECT_STREQ(lb_predefined_names[am_spawn_monitor], "spawn_monitor");
    EXPECT_STREQ(lb_predefined_names[am_set_cookie], "set_cookie");
    EXPECT_STREQ(lb_predefined_names[am_load_module], "load_module");
    EXPECT_STREQ(lb_predefined_names[am_true], "true");
    EXPECT_STREQ(lb_predefined_names[am_false], "false");
    EXPECT_LT(BIF_self_0, BIF_SIZE);
}
} // namespace
int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    if (argc != 10) return 2;
    for (unsigned i = 0; i < 9; ++i) paths[i] = libbeam_test::runfile(argv[i + 1]);
    return RUN_ALL_TESTS();
}
