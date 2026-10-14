// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#pragma once
#include <regex>
#include <stdexcept>
#include <string>

namespace libbeam_build {
inline std::size_t replace_all(std::string& text, const std::string& before, const std::string& after) {
    if (before.empty()) throw std::invalid_argument("Empty replacement boundary");
    std::size_t count = 0, offset = 0;
    while ((offset = text.find(before, offset)) != std::string::npos) {
        text.replace(offset, before.size(), after);
        offset += after.size();
        ++count;
    }
    return count;
}
inline void replace_exactly_once(std::string& text, const std::string& before, const std::string& after) {
    if (replace_all(text, before, after) != 1)
        throw std::runtime_error("Changed generator boundary: " + before);
}
inline void regex_replace_all(std::string& text, const std::string& pattern, const std::string& replacement) {
    text = std::regex_replace(text, std::regex(pattern, std::regex::ECMAScript | std::regex::multiline), replacement);
}
inline std::string whole_function(const std::string& text, const std::string& declaration) {
    const auto begin = text.find(declaration);
    if (begin == std::string::npos || text.find(declaration, begin + declaration.size()) != std::string::npos)
        throw std::runtime_error("Missing or ambiguous function: " + declaration);
    const auto end = text.find("\n}", begin);
    if (end == std::string::npos) throw std::runtime_error("Unterminated function: " + declaration);
    return text.substr(begin, end + 2 - begin);
}
inline void reject_unowned_allocation(const std::string& body) {
    for (const char* token : {"erts_alloc", "erts_free", "beamopallocator_"})
        if (body.find(token) != std::string::npos)
            throw std::runtime_error("Unadapted generator allocation: " + std::string(token));
}
} // namespace libbeam_build
