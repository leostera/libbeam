// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#pragma once
#include <map>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace libbeam_source {
struct Definition {
    std::string text;
    bool function_like;
};
using Definitions = std::map<std::string, std::vector<Definition>>;
enum class Branch { first, last };

// This is the explicit flat-64 profile selector used by the independent probes,
// not a C preprocessor. Preserve complete original directives and continuations.
inline Definitions definitions(const std::string& source) {
    Definitions result;
    std::istringstream input(source);
    const std::regex directive(R"(^\s*#\s*define\s+(\w+))");
    for (std::string line; std::getline(input, line);) {
        std::smatch match;
        if (!std::regex_search(line, match, directive)) continue;
        const auto name = match[1].str();
        const auto after_name = static_cast<size_t>(match.position(1) + match.length(1));
        const bool function_like = after_name < line.size() && line[after_name] == '(';
        std::string text = line + '\n';
        for (;;) {
            auto last = line.find_last_not_of(" \t\r");
            if (last == std::string::npos || line[last] != '\\') break;
            if (!std::getline(input, line)) throw std::invalid_argument("Unterminated macro: " + name);
            text += line + '\n';
        }
        result[name].push_back({std::move(text), function_like});
    }
    return result;
}

inline std::string macros(const Definitions& input, const std::string& names, Branch branch = Branch::first) {
    std::istringstream words(names);
    std::string output;
    for (std::string name; words >> name;) {
        auto found = input.find(name);
        if (found == input.end()) throw std::invalid_argument("Missing macro: " + name);
        output += (branch == Branch::first ? found->second.front() : found->second.back()).text;
    }
    return output;
}

inline std::string declaration(const std::string& input, const std::string& start, const std::string& end) {
    auto first = input.find(start);
    if (first == std::string::npos || input.find(start, first + start.size()) != std::string::npos)
        throw std::invalid_argument("Expected one declaration: " + start);
    auto last = input.find(end, first + start.size());
    if (last == std::string::npos) throw std::invalid_argument("Unterminated declaration: " + start);
    return input.substr(first, last + end.size() - first) + '\n';
}

inline std::vector<std::string> array_values(const std::string& input, const std::string& name) {
    const std::regex array("\\b" + name + R"(\[[^\]]+\]\s*=\s*\{([\s\S]*?)\};)");
    std::smatch match;
    if (!std::regex_search(input, match, array)) throw std::invalid_argument("Missing array: " + name);
    const auto body = std::regex_replace(match[1].str(), std::regex(R"(/\*[\s\S]*?\*/)"), "");
    std::istringstream values(body);
    std::vector<std::string> result;
    for (std::string value; std::getline(values, value, ',');) {
        auto first = value.find_first_not_of(" \t\r\n");
        if (first != std::string::npos) result.push_back(value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1));
    }
    if (result.empty()) throw std::invalid_argument("Empty array: " + name);
    return result;
}
}  // namespace libbeam_source
