// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#pragma once
#include "source_support.hpp"
#include <algorithm>
#include <charconv>
#include <cctype>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <tuple>
#include <vector>

namespace libbeam_test::inventory {
using Json = nlohmann::json;
using Path = std::filesystem::path;

class Mfa {
public:
    static Mfa parse(std::string_view text) {
        auto slash = text.rfind('/');
        if (slash == std::string_view::npos || slash == 0 ||
            std::any_of(text.begin(), text.end(), [](unsigned char c) { return std::isspace(c); }))
            throw std::invalid_argument("Malformed MFA");
        std::string name(text.substr(0, slash));
        if (name.find('\'') != std::string::npos) {
            if (name.front() != '\'' || name.back() != '\'' || std::count(name.begin(), name.end(), '\'') != 2)
                throw std::invalid_argument("Malformed quoted MFA");
            name = name.substr(1, name.size() - 2);
        }
        unsigned arity;
        auto [end, error] = std::from_chars(text.data() + slash + 1, text.data() + text.size(), arity);
        if (name.empty() || error != std::errc{} || end != text.data() + text.size() || arity > 255)
            throw std::invalid_argument("Invalid MFA");
        return Mfa(std::move(name), arity);
    }
    const std::string& function() const { return function_; }
    unsigned arity() const { return arity_; }
    bool operator<(const Mfa& other) const { return std::tie(function_, arity_) < std::tie(other.function_, other.arity_); }
    bool operator==(const Mfa& other) const { return function_ == other.function_ && arity_ == other.arity_; }
private:
    Mfa(std::string function, unsigned arity) : function_(std::move(function)), arity_(arity) {}
    std::string function_;
    unsigned arity_;
};

inline std::string trim(std::string text) {
    auto first = text.find_first_not_of(" \t\r\n\v\f");
    return first == std::string::npos ? "" : text.substr(first, text.find_last_not_of(" \t\r\n\v\f") - first + 1);
}

inline std::vector<Mfa> requested(std::string_view text) {
    std::istringstream input{std::string(text)};
    std::vector<Mfa> result;
    std::set<Mfa> seen;
    for (std::string line; std::getline(input, line);) {
        line = trim(line);
        if (line.empty() || line.front() == '#') continue;
        auto mfa = Mfa::parse(line);
        if (!seen.insert(mfa).second) throw std::invalid_argument("Duplicate requested MFA");
        result.push_back(std::move(mfa));
    }
    if (result.empty()) throw std::invalid_argument("Empty request inventory");
    return result;
}

inline std::map<Mfa, Json> contracts(const Json& document, const std::set<Mfa>& expected) {
    std::set<std::string> groups;
    const std::set<std::string> allowed{"local", "query", "denial", "exception"};
    std::map<Mfa, Json> result;
    for (const auto& group : document.at("groups")) {
        auto id = group.at("id").get<std::string>();
        if (!groups.insert(id).second || !allowed.count(group.at("classification").get<std::string>()) ||
            trim(group.at("contract").get<std::string>()).empty())
            throw std::invalid_argument("Invalid contract group");
        for (const auto& value : group.at("mfas")) {
            auto key = Mfa::parse(value.get<std::string>());
            Json contract{{"id", id}, {"classification", group.at("classification")}, {"contract", group.at("contract")}};
            if (!expected.count(key) || !result.emplace(key, std::move(contract)).second)
                throw std::invalid_argument("Unexpected or duplicate contract MFA");
        }
    }
    if (result.size() != expected.size()) throw std::invalid_argument("Missing implementation contracts");
    return result;
}

inline std::map<Mfa, Json> wrappers(const Json& document, const std::set<Mfa>& expected) {
    const std::set<std::string> allowed{"beam", "intrinsic", "owned-query-adapter", "local-native-adapter", "denial", "compatibility-exception"};
    std::map<Mfa, Json> result;
    for (const auto& group : document.at("plans")) {
        if (!allowed.count(group.at("mode").get<std::string>()) || trim(group.at("reason").get<std::string>()).empty())
            throw std::invalid_argument("Invalid wrapper plan");
        for (const auto& value : group.at("mfas")) {
            auto key = Mfa::parse(value.get<std::string>());
            if (!expected.count(key) || !result.emplace(key, Json{{"mode", group.at("mode")}, {"reason", group.at("reason")}}).second)
                throw std::invalid_argument("Unexpected or duplicate wrapper plan");
        }
    }
    if (result.size() != expected.size()) throw std::invalid_argument("Missing wrapper plans");
    return result;
}

// Source facts only: a declaration or candidate body is not executable coverage.
inline Json audit(const Path& root, const Path& request, const std::optional<Path>& contracts_path = std::nullopt, const std::optional<Path>& wrappers_path = std::nullopt) {
    auto contract_data = contracts_path ? Json::parse(read_file(*contracts_path)) : Json{};
    auto wrapper_data = wrappers_path ? Json::parse(read_file(*wrappers_path)) : Json{};
    const Json* contract_document = contracts_path ? &contract_data : nullptr;
    const Json* wrapper_document = wrappers_path ? &wrapper_data : nullptr;
    const auto table_path = root / "beam/erts/emulator/beam/bif.tab";
    std::map<Mfa, Json> table;
    const std::regex declaration(R"((bif|ubif|hbif)\s+erlang:(\S+)/(\d+)(?:\s+(\w+))?\s*)");
    const std::regex starts_declaration(R"(^(bif|ubif|hbif)\s+erlang:)");
    std::istringstream table_input(read_file(table_path));
    size_t number = 0;
    for (std::string line; std::getline(table_input, line);) {
        ++number;
        std::smatch match;
        if (!std::regex_match(line, match, declaration)) {
            if (std::regex_search(line, starts_declaration)) throw std::invalid_argument("Unparsed erlang declaration");
            continue;
        }
        auto key = Mfa::parse(match[2].str() + "/" + match[3].str());
        std::string symbol = match[4].matched ? match[4].str() : key.function() + "_" + match[3].str();
        if (!table.emplace(key, Json{{"kind", match[1].str()}, {"symbol", symbol}, {"line", number}}).second)
            throw std::invalid_argument("Duplicate native declaration");
    }
    std::vector<Path> paths;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root / "beam/erts/emulator", std::filesystem::directory_options::follow_directory_symlink))
        if (entry.is_regular_file() && entry.path().extension() == ".c") paths.push_back(entry.path());
    std::sort(paths.begin(), paths.end());
    std::map<std::string, Json> definitions;
    const std::regex definition(R"(\b(?:BIF_RETTYPE|Eterm)\s+(\w+)\s*\()");
    for (const auto& path : paths) {
        auto text = read_file(path);
        auto digest = sha256(text);
        for (auto it = std::sregex_iterator(text.begin(), text.end(), definition); it != std::sregex_iterator(); ++it) {
            auto& candidates = definitions[(*it)[1].str()];
            if (candidates.is_null()) candidates = Json::array();
            candidates.push_back({{"path", path.lexically_relative(root).string()}, {"line", 1 + std::count(text.begin(), text.begin() + it->position(), '\n')}, {"sha256", digest}});
        }
    }
    const auto engine_path = root / "libbeam/core/engine.c";
    std::string engine = read_file(engine_path), registered = engine;
    std::vector<Path> sources{table_path, root / "beam/erts/preloaded/src/erlang.erl", engine_path, request};
    std::set<std::string> predicate_symbols;
    for (const std::string file : {"capability_denials.def", "type_predicates.def"}) {
        auto path = root / "libbeam/core" / file;
        if (!std::filesystem::is_regular_file(path) || engine.find("#include \"" + file + "\"") == std::string::npos) continue;
        sources.push_back(path);
        auto text = read_file(path);
        registered += "\n" + text;
        if (file == "type_predicates.def") {
            const std::regex pattern(R"(^LB_TYPE_PREDICATE\((\w+),)", std::regex::multiline);
            for (auto it = std::sregex_iterator(text.begin(), text.end(), pattern); it != std::sregex_iterator(); ++it)
                predicate_symbols.insert((*it)[1].str() + "_1");
        }
    }
    if (contracts_path) sources.push_back(*contracts_path);
    if (wrappers_path) sources.push_back(*wrappers_path);
    auto requests = requested(read_file(request));
    std::set<Mfa> expected(requests.begin(), requests.end()), without_native;
    for (const auto& mfa : expected) if (!table.count(mfa)) without_native.insert(mfa);
    auto assigned = contract_document ? contracts(*contract_document, expected) : std::map<Mfa, Json>{};
    auto plans = wrapper_document ? wrappers(*wrapper_document, without_native) : std::map<Mfa, Json>{};
    auto rows = Json::array();
    unsigned native_count = 0;
    auto preloaded = read_file(sources[1]);
    for (const auto& mfa : requests) {
        auto native = table.find(mfa);
        Json row{{"module", "erlang"}, {"function", mfa.function()}, {"arity", mfa.arity()},
                 {"policy_status", "unreviewed"}, {"implementation_evidence", Json::array()}};
        if (contract_document) {
            row["policy_status"] = "contract-assigned; implementation-review-pending";
            row["proposed_contract"] = assigned.at(mfa);
        }
        if (wrapper_document) row["proposed_implementation"] = native != table.end()
            ? Json{{"mode", "native-c"}, {"reason", "Pinned native C body or explicit C capability boundary; see contract."}} : plans.at(mfa);
        if (native != table.end()) {
            ++native_count;
            row["native_declaration"] = native->second;
            auto symbol = native->second.at("symbol").get<std::string>();
            row["native_source_candidates"] = definitions.count(symbol) ? definitions.at(symbol) : Json::array();
            std::string escaped;
            for (char c : symbol) {
                if (std::string_view("\\.^$|()[]{}*+?").find(c) != std::string_view::npos) escaped += '\\';
                escaped += c;
            }
            const std::regex binding("(?:\\{BIF_|LB_DENIAL\\(BIF_)" + escaped + R"(\s*,)");
            row["native_id_in_engine_catalog"] = std::regex_search(registered, binding) || predicate_symbols.count(symbol);
        } else {
            row["native_declaration"] = nullptr;
            row["requires_wrapper_or_intrinsic_review"] = true;
        }
        auto heads = Json::array();
        std::istringstream lines(preloaded);
        number = 0;
        for (std::string line; std::getline(lines, line);) {
            ++number;
            if (line.rfind(mfa.function() + "(", 0) == 0 || line.rfind("'" + mfa.function() + "'(", 0) == 0) heads.push_back(number);
        }
        row["preloaded_head_candidate_lines"] = heads;
        rows.push_back(std::move(row));
    }
    Json hashes = Json::object();
    for (const auto& path : sources) hashes[path.lexically_relative(root).string()] = sha256(read_file(path));
    return {{"scope", "source facts and proposed contracts; not capability admission or test coverage"},
            {"common_proposed_contract", contract_document ? contract_document->at("common") : Json::object()},
            {"requested_count", rows.size()}, {"direct_native_declarations", native_count},
            {"source_hashes", hashes}, {"entries", rows}};
}
}  // namespace libbeam_test::inventory
