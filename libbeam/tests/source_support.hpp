// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#pragma once
#include <filesystem>
#include <fstream>
#include <iterator>
#include <openssl/sha.h>
#include <stdexcept>
#include <string>
#include <string_view>

namespace libbeam_test {
inline std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot read " + path.string());
    std::string result(std::istreambuf_iterator<char>{input}, {});
    if (input.bad()) throw std::runtime_error("Failed reading " + path.string());
    return result;
}

inline std::string sha256(std::string_view input) {
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(input.data()), input.size(), digest);
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(sizeof(digest) * 2);
    for (auto byte : digest) {
        result.push_back(hex[byte >> 4]);
        result.push_back(hex[byte & 15]);
    }
    return result;
}
}  // namespace libbeam_test
