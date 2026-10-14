// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#include "tools/source_projection.hpp"
#include <gtest/gtest.h>

namespace {
using namespace libbeam_source;
TEST(SourceProjection, PreservesOriginalMacrosAndExplicitBranchSelection) {
    const std::string source = "#define VALUE 1\n#define FN(x) ((x) + \\\n  VALUE)\n#define VALUE 2\n";
    auto parsed = definitions(source);
    EXPECT_EQ(macros(parsed, "VALUE FN"), "#define VALUE 1\n#define FN(x) ((x) + \\\n  VALUE)\n");
    EXPECT_EQ(macros(parsed, "VALUE", Branch::last), "#define VALUE 2\n");
    EXPECT_FALSE(parsed.at("VALUE").front().function_like);
    EXPECT_TRUE(parsed.at("FN").front().function_like);
    EXPECT_THROW(macros(parsed, "MISSING"), std::invalid_argument);
}
TEST(SourceProjection, ObjectExpressionIsNotFunctionMacro) {
    auto parsed = definitions("#define VALUE (1 + 2)\n");
    EXPECT_FALSE(parsed.at("VALUE").front().function_like);
}
TEST(SourceProjection, UnterminatedContinuationIsRejected) {
    EXPECT_THROW(definitions("#define BAD \\"), std::invalid_argument);
}
TEST(SourceProjection, DeclarationMustBeUniqueAndComplete) {
    const std::string good = "typedef struct value {\n int member;\n} Value;";
    EXPECT_EQ(declaration(good, "typedef struct value {", "\n} Value;"), good + '\n');
    EXPECT_THROW(declaration(good + good, "typedef struct value {", "\n} Value;"), std::invalid_argument);
    EXPECT_THROW(declaration("", "typedef struct value {", "\n} Value;"), std::invalid_argument);
    EXPECT_THROW(declaration("typedef struct value {", "typedef struct value {", "\n} Value;"), std::invalid_argument);
}
TEST(SourceProjection, ArrayCommentsDoNotChangeValues) {
    EXPECT_EQ(array_values("Eterm values[2] = { first, /* , ignored */ second, };", "values"),
              (std::vector<std::string>{"first", "second"}));
    EXPECT_THROW(array_values("", "values"), std::invalid_argument);
    EXPECT_THROW(array_values("Eterm values[2] = {};", "values"), std::invalid_argument);
}
}  // namespace
