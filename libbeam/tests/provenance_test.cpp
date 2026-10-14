// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#include "source_support.hpp"
#include "test_process.hpp"
#include <algorithm>
#include <gtest/gtest.h>
#include <map>
#include <nlohmann/json.hpp>

namespace {
using Json = nlohmann::json;
using Path = std::filesystem::path;
using libbeam_test::read_file;
using libbeam_test::runfile;
using libbeam_test::sha256;

Path reference_path(const std::string& relative) {
    Path path(relative);
    if (path.empty() || path.is_absolute()) throw std::invalid_argument("Not a relative reference path");
    for (const auto& part : path) if (part == "..") throw std::invalid_argument("Reference escapes runfiles");
    return Path(runfile(relative));
}

TEST(Provenance, ReferencePathsCannotEscapeRunfiles) {
    for (const auto* invalid : {"", "/etc/passwd", "../file", "beam/../../file"})
        EXPECT_THROW(reference_path(invalid), std::invalid_argument);
    EXPECT_EQ(reference_path("beam/input.c"), Path(runfile("beam/input.c")));
}

TEST(Provenance, RecordsOwnedSourcesAndValidationPolicy) {
    std::istringstream paths(read_file(runfile("libbeam/validation_sources.txt")));
    Json fingerprints = Json::object();
    for (std::string path; std::getline(paths, path);) {
        ASSERT_FALSE(path.empty());
        ASSERT_FALSE(fingerprints.contains(path)) << path;
        fingerprints[path] = sha256(read_file(reference_path(path)));
    }
    for (const char* required : {"libbeam/core/engine.c", "libbeam/include/libbeam/engine.hpp",
                                "libbeam/tests/provenance_test.cpp", "libbeam/README.md",
                                "libbeam/core/README.md", "libbeam/BUILD.bazel", ".bazelrc",
                                "bazel/defs.bzl", "docs/rfds/0004-eager-batch-admission.md"})
        EXPECT_TRUE(fingerprints.contains(required)) << required;
    // This records source identity, not execution coverage for every file.
    // A final Bazel --check_tests_up_to_date rejects invalidated results.
    RecordProperty("validation_source_sha256", fingerprints.dump());
}

TEST(Provenance, AllAdmittedNativeSourceManifests) {
    std::map<std::string, std::string> digests;
    size_t checked = 0;
    for (const auto& entry : std::filesystem::directory_iterator(runfile("libbeam/core/otp"))) {
        const auto name = entry.path().filename().string();
        const std::string suffix = "-sources.json";
        if (name.size() < suffix.size() || name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0) continue;
        SCOPED_TRACE(name);
        auto document = Json::parse(read_file(entry.path()));
        const auto& sources = document.at("sources");
        ASSERT_TRUE(sources.is_object());
        ASSERT_FALSE(sources.empty());
        for (const auto& [file, expected] : sources.items()) {
            if (!digests.count(file)) digests[file] = sha256(read_file(reference_path(file)));
            EXPECT_EQ(digests.at(file), expected.get<std::string>()) << file;
        }
        ++checked;
    }
    // Missing runfiles must not turn the provenance gate into an empty success.
    EXPECT_GE(checked, 53u);
}

TEST(Provenance, OpcodeInputsAndGeneratorMatchPinnedProfile) {
    auto document = Json::parse(read_file(runfile("libbeam/core/otp/opcodes/manifest.json")));
    EXPECT_EQ(document.at("profile"), Json({{"wordsize", 64}, {"jit", "no"}, {"USE_VM_PROBES", 0}}));
    EXPECT_EQ(sha256(read_file(runfile("libbeam/tools/otp/beam_makeops"))), document.at("generator_sha256"));
    ASSERT_FALSE(document.at("inputs").empty());
    for (const auto& input : document.at("inputs")) {
        const auto original = input.at("source").get<std::string>();
        const auto admitted = "libbeam/core/otp/opcodes/" + input.at("input").get<std::string>();
        EXPECT_EQ(sha256(read_file(reference_path(original))), input.at("sha256")) << original;
        EXPECT_EQ(sha256(read_file(reference_path(admitted))), input.at("sha256")) << admitted;
    }
}

TEST(Provenance, IndependentNumericVectorsAreNotRuntimeAnswers) {
    struct Golden { const char* name; size_t rows; const char* digest; };
    for (const auto& golden : {
        Golden{"float-vectors.txt", 3979, "a979f14eb8661adf92580663bd5656b6bad5015f94a015bc25c623d45e2d1e2c"},
        Golden{"div_vectors.txt", 363, "b39727bf99b90a5619fe1ab4676ede5fa05bb5b9e86b8cb9e7cf8e5a4fb633a5"},
    }) {
        auto bytes = read_file(runfile(std::string("libbeam/tests/fixtures/") + golden.name));
        EXPECT_EQ(static_cast<size_t>(std::count(bytes.begin(), bytes.end(), '\n')), golden.rows) << golden.name;
        EXPECT_EQ(sha256(bytes), golden.digest) << golden.name;
    }
}
}  // namespace
