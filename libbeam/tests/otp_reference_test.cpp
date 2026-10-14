// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#include "test_process.hpp"
#include <filesystem>
#include <gtest/gtest.h>
#include <iostream>
#include <regex>
#include <set>

namespace {
std::string erl, module, mode, vectors;
std::vector<std::string> beams;

TEST(IndependentOtpReference, FixtureAssertionsPass) {
    std::vector<std::string> arguments{"-noshell", "-noinput"};
    std::set<std::string> paths;
    for (const auto& beam : beams) paths.insert(std::filesystem::path(libbeam_test::runfile(beam)).parent_path().string());
    for (const auto& path : paths) { arguments.push_back("-pa"); arguments.push_back(path); }
    if (mode == "monitor") {
        arguments.insert(arguments.end(), {"-eval",
            "lists:foreach(fun(F)-> <<\"monitored reply\">> = apply(fun_slice,F,[<<\"monitored reply\">>]) end,"
            "[monitored_error,monitored_error_info,monitored_bif_error,monitored_error_call,monitored_empty_info]),"
            "io:format(\"MONITOR_TRACE_REFERENCE_OK~n\"),halt()."});
    } else if (mode == "vectors") {
        arguments.insert(arguments.end(), {"-eval", "[Path]=init:get_plain_arguments()," + module + ":run(Path),halt().", "-extra", libbeam_test::runfile(vectors)});
    } else {
        arguments.insert(arguments.end(), {"-s", module, "run"});
        if (mode == "trace") arguments.insert(arguments.end(), {"-s", module, "execution"});
        arguments.insert(arguments.end(), {"-s", "init", "stop"});
    }
    const auto result = libbeam_test::run(erl, arguments);
    ASSERT_TRUE(result.exited(0)) << module << '\n' << result.output;
    if (mode == "monitor") EXPECT_EQ(result.standard_output, "MONITOR_TRACE_REFERENCE_OK\n");
}
}  // namespace

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    if (argc < 6 || !std::regex_match(argv[2], std::regex("[a-z][a-z0-9_]*"))) {
        std::cerr << "Expected erl, reference module, mode, vectors-or-dash and declared BEAM runfiles\n";
        return 2;
    }
    erl = libbeam_test::runfile(argv[1]);
    module = argv[2]; mode = argv[3]; vectors = argv[4];
    if ((mode != "run" && mode != "vectors" && mode != "trace" && mode != "monitor") || (mode == "vectors") != (vectors != "-")) return 2;
    beams.assign(argv + 5, argv + argc);
    return RUN_ALL_TESTS();
}
