// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
// Local Bazel contract probe, not remote-hermeticity evidence. Only a private
// copy of the checked-in fixture is changed; the checkout stays untouched.
#include "test_directory.hpp"
#include "test_process.hpp"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iterator>
namespace {
std::string bazel;
std::string read(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("cannot read " + path);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
struct Build {
    libbeam_test::ProcessResult process;
    std::vector<nlohmann::json> tests;
};
class Workspace {
public:
    Workspace() {
        for (const auto* name : {"BUILD.bazel", "MODULE.bazel", ".bazelversion", "probe.cc", "payload.txt"}) {
            const auto bytes = read(libbeam_test::runfile(std::string("libbeam/tests/fixtures/cache_contract/") + name));
            directory_.write(std::string("workspace/") + name, bytes);
        }
    }
    void write(const std::string& name, const std::string& bytes) { directory_.write("workspace/" + name, bytes); }
    Build test(const std::string& mode, bool freshness = false) {
        const auto events = directory_.root() / "events.json";
        std::vector<std::string> arguments{
            "-c", "cd \"$1\"; shift; exec \"$@\"", "cache-probe", (directory_.root() / "workspace").string(),
            bazel, "--batch", "--ignore_all_rc_files", "--output_user_root=" + (directory_.root() / "cache").string(),
            "test", "//:probe", "--compilation_mode=" + mode, "--jobs=2", "--color=no", "--curses=no",
            "--build_event_json_file=" + events.string(), "--test_output=errors"};
        if (freshness) arguments.push_back("--check_tests_up_to_date");
        Build result{libbeam_test::run("/bin/sh", arguments), {}};
        std::ifstream stream(events);
        if (!stream) throw std::runtime_error(result.process.output);
        std::string line;
        bool started = false;
        while (std::getline(stream, line)) {
            const auto event = nlohmann::json::parse(line);
            if (event.contains("started")) {
                EXPECT_EQ(event.at("started").at("buildToolVersion"), "9.3.0");
                started = true;
            }
            if (event.contains("testResult")) result.tests.push_back(event.at("testResult"));
        }
        EXPECT_TRUE(started) << result.process.output;
        return result;
    }
private:
    libbeam_test::TestDirectory directory_;
};
void expect_result(const Build& build, bool success, bool cached) {
    ASSERT_TRUE(build.process.exited(success ? 0 : 3)) << build.process.output;
    ASSERT_EQ(build.tests.size(), 1u) << build.process.output;
    EXPECT_EQ(build.tests[0].at("status"), success ? "PASSED" : "FAILED");
    EXPECT_EQ(build.tests[0].value("cachedLocally", false), cached);
}
void expect_stale(const Build& build) {
    EXPECT_FALSE(build.process.exited(0));
    EXPECT_NE(build.process.output.find("not up-to-date"), std::string::npos) << build.process.output;
    EXPECT_TRUE(build.tests.empty()) << "Freshness refusal must not execute the stale test";
}
TEST(BazelCache, SourceFixtureAndConfigurationChangesCannotReuseStaleSuccess) {
    Workspace workspace;
    ASSERT_NO_FATAL_FAILURE(expect_result(workspace.test("dbg"), true, false));
    expect_result(workspace.test("dbg"), true, true);
    EXPECT_TRUE(workspace.test("dbg", true).process.exited(0));

    workspace.write("payload.txt", "changed\n");
    expect_stale(workspace.test("dbg", true));
    expect_result(workspace.test("dbg"), false, false);
    workspace.write("payload.txt", "original\n");
    ASSERT_TRUE(workspace.test("dbg").process.exited(0));

    const auto original = read(libbeam_test::runfile("libbeam/tests/fixtures/cache_contract/probe.cc"));
    auto changed = original;
    const auto position = changed.find("source_version = 1");
    ASSERT_NE(position, std::string::npos);
    changed.replace(position, std::string("source_version = 1").size(), "source_version = 2");
    workspace.write("probe.cc", changed);
    expect_stale(workspace.test("dbg", true));
    expect_result(workspace.test("dbg"), false, false);
    workspace.write("probe.cc", original);
    ASSERT_TRUE(workspace.test("dbg").process.exited(0));

    expect_stale(workspace.test("opt", true));
    expect_result(workspace.test("opt"), true, false);
    expect_result(workspace.test("opt"), true, true);
    EXPECT_TRUE(workspace.test("opt", true).process.exited(0));
    EXPECT_TRUE(workspace.test("dbg", true).process.exited(0));
}
}
int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    if (argc != 2) return 2;
    bazel = libbeam_test::runfile(argv[1]);
    return RUN_ALL_TESTS();
}
