// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#include "source_support.hpp"
#include "test_process.hpp"
#include "tools/source_projection.hpp"
#include <gtest/gtest.h>

namespace {
using libbeam_test::run;
using libbeam_test::runfile;
using libbeam_test::read_file;

class Representation : public testing::TestWithParam<const char*> {};
TEST_P(Representation, OwnedMatchesPinnedFlat64Reference) {
    const auto kind = std::string(GetParam());
    const auto core = run(runfile("libbeam/representation_" + kind + "_core"));
    const auto reference = run(runfile("libbeam/representation_" + kind + "_reference"));
    ASSERT_TRUE(core.exited(0)) << core.output;
    ASSERT_TRUE(reference.exited(0)) << reference.output;
    ASSERT_FALSE(core.output.empty());
    EXPECT_EQ(core.output, reference.output);
}
INSTANTIATE_TEST_SUITE_P(NativeLayouts, Representation, testing::Values("term", "binary", "flatmap", "exception"));

TEST(ExceptionRepresentation, NativeDescriptorAndClassTables) {
    const auto native = read_file(runfile("beam/erts/emulator/beam/beam_common.c"));
    const auto owned = read_file(runfile("libbeam/core/exception.c"));
    for (const std::string name : {"error_atom", "exception_tag"}) {
        EXPECT_EQ(libbeam_source::array_values(native, name), libbeam_source::array_values(owned, name)) << name;
    }
}
}  // namespace
