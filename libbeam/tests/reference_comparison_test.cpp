// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#include "test_process.hpp"
#include <filesystem>
#include <gtest/gtest.h>
#include <iostream>
#include <map>
#include <sstream>

namespace {
using libbeam_test::run;
using libbeam_test::runfile;
struct Comparison {
    std::string reference_module;
    std::string reference_function;
    std::string native_program;
    std::string native_flag;
    size_t rows;
    std::vector<std::string> fixtures;
};
const std::map<std::string, Comparison> cases{
    {"phash2", {"hash_reference", "run", "core_hash_test", "--reference", 1071, {"hash_slice"}}},
    {"radix", {"radix_reference", "run", "core_radix_test", "--reference", 1085, {"radix_slice"}}},
    {"bit_writer", {"bit_writer_reference", "run", "core_bit_writer_test", "--reference", 1070, {}}},
    {"bit_writer_fallback", {"bit_writer_reference", "run", "core_bit_writer_fallback_test", "--reference", 1070, {}}},
    {"bit_segment", {"bit_writer_reference", "segments", "core_bit_segment_test", "--reference", 840, {}}},
    {"bit_alias", {"bit_writer_reference", "aliases", "core_binary_test", "--alias-reference", 514, {"binary_slice"}}},
    {"binary_error", {"binary_error_reference", "run", "core_flatmap_test", "--reference", 5, {"binary_slice"}}},
    {"writable_init", {"writable_init_reference", "run", "core_writable_init_test", "--reference", 9, {"writable_init_slice"}}},
    {"binary_build", {"binary_construct_reference", "run", "core_binary_build_test", "--reference", 683, {"binary_construct_slice", "binary_control_slice"}}},
    {"exception", {"exception_reference", "run", "core_exception_test", "--reference", 4, {"exception_slice"}}},
    {"binary_size", {"binary_size_reference", "run", "core_binary_size_test", "--reference", 11, {}}},
};
std::string erl, selected;

std::string fixture(const std::string& module) {
    return runfile("libbeam/fixtures/" + module + "_fixture/" + module + ".beam");
}
std::vector<std::string> lines(const std::string& text) {
    std::istringstream input(text);
    std::vector<std::string> result;
    for (std::string line; std::getline(input, line);) result.push_back(line);
    return result;
}
std::vector<std::string> reference_arguments(const Comparison& test, const std::string& function) {
    std::vector<std::string> args{"-noshell", "-noinput", "-pa", std::filesystem::path(fixture(test.reference_module)).parent_path().string()};
    for (const auto& module : test.fixtures) {
        args.push_back("-pa");
        args.push_back(std::filesystem::path(fixture(module)).parent_path().string());
    }
    args.insert(args.end(), {"-eval", test.reference_module + ":" + function + "(),halt()."});
    return args;
}

TEST(IndependentVectors, NativeResultsMatchReferenceWithExplicitDivergences) {
    const auto& test = cases.at(selected);
    std::vector<std::string> native_args{test.native_flag};
    for (const auto& module : test.fixtures) native_args.push_back(fixture(module));
    const auto native = run(runfile("libbeam/" + test.native_program), native_args);
    const auto reference = run(erl, reference_arguments(test, test.reference_function));
    if (selected == "bit_writer_fallback") {
        const auto components = run(runfile("libbeam/" + test.native_program));
        EXPECT_TRUE(components.exited(0)) << components.output;
    }
    ASSERT_TRUE(native.exited(0)) << native.output;
    ASSERT_TRUE(reference.exited(0)) << reference.output;
    ASSERT_TRUE(native.standard_error.empty()) << native.standard_error;
    ASSERT_TRUE(reference.standard_error.empty()) << reference.standard_error;
    auto actual = lines(native.standard_output), expected = lines(reference.standard_output);
    ASSERT_EQ(actual.size(), test.rows);
    ASSERT_EQ(expected.size(), test.rows);
    if (selected == "binary_size") {
        // The pinned interpreter defers UTF8 validation; installed JITs can
        // detect the UTF8 failure first. Do not normalize this into parity.
        EXPECT_EQ(actual[0], "utf8:badarg:2:integer:size");
        EXPECT_TRUE(expected[0] == actual[0] || expected[0] == "utf8:badarg:1:utf8:type") << expected[0];
        RecordProperty("native_utf8_order", actual[0]);
        RecordProperty("reference_utf8_order", expected[0]);
        EXPECT_EQ(std::vector<std::string>(actual.begin() + 1, actual.end()), std::vector<std::string>(expected.begin() + 1, expected.end()));
    } else {
        EXPECT_EQ(native.standard_output, reference.standard_output);
    }
    if (selected == "binary_build") {
        const auto ordering = run(erl, reference_arguments(test, "ordering"));
        ASSERT_TRUE(ordering.exited(0)) << ordering.output;
        const auto order = lines(ordering.output);
        ASSERT_EQ(order.size(), 1u);
        EXPECT_TRUE(order[0] == "1:utf8:type" || order[0] == "2:integer:size") << order[0];
        RecordProperty("reference_error_order", order[0]);
        native_args.erase(native_args.begin());
        const auto execution = run(runfile("libbeam/" + test.native_program), native_args);
        ASSERT_TRUE(execution.exited(0)) << execution.output;
        EXPECT_NE(execution.output.find("CORE_BINARY_BUILD_EXECUTION_OK"), std::string::npos);
        EXPECT_NE(execution.output.find("native=2:integer:size"), std::string::npos);
    }
}
}  // namespace

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    if (argc != 3 || !cases.count(argv[2])) { std::cerr << "Expected declared erl runfile and comparison case\n"; return 2; }
    erl = runfile(argv[1]); selected = argv[2];
    return RUN_ALL_TESTS();
}
