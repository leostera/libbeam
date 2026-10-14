// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#pragma once
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unistd.h>

namespace libbeam_test {
class TestDirectory {
public:
    TestDirectory() {
        const char* temporary = std::getenv("TEST_TMPDIR");
        if (!temporary) throw std::runtime_error("Bazel TEST_TMPDIR is missing");
        std::string pattern = std::string(temporary) + "/fixture-XXXXXX";
        if (!mkdtemp(pattern.data())) throw std::runtime_error("Cannot create test directory");
        root_ = pattern;
    }
    ~TestDirectory() {
        std::error_code error;
        std::filesystem::remove_all(root_, error);
    }
    TestDirectory(const TestDirectory&) = delete;
    TestDirectory& operator=(const TestDirectory&) = delete;
    const std::filesystem::path& root() const { return root_; }
    std::filesystem::path write(const std::string& name, const std::string& contents) const {
        const std::filesystem::path relative(name);
        if (relative.empty() || relative.is_absolute()) throw std::invalid_argument("Invalid fixture path");
        for (const auto& part : relative) if (part == "..") throw std::invalid_argument("Escaping fixture path");
        auto path = root_ / relative;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream stream(path, std::ios::binary);
        stream << contents;
        stream.close();
        if (!stream) throw std::runtime_error("Cannot write fixture " + path.string());
        return path;
    }
private:
    std::filesystem::path root_;
};
}  // namespace libbeam_test
