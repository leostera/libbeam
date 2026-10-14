// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#pragma once
#include <algorithm>
#include <regex>
#include <stdexcept>
#include <string>
#include <vector>

namespace libbeam_build {
struct AtomProjection {
    std::string source;
    std::string header;
    std::string bif_ids;
};
inline void replace_once(std::string& text, const std::string& before, const std::string& after) {
    const auto offset = text.find(before);
    if (offset == std::string::npos || text.find(before, offset + before.size()) != std::string::npos)
        throw std::runtime_error("Missing or ambiguous generator declaration: " + before);
    text.replace(offset, before.size(), after);
}
inline std::vector<std::string> matching_lines(const std::string& text, const std::regex& pattern) {
    std::vector<std::string> result;
    for (std::sregex_iterator it(text.begin(), text.end(), pattern), end; it != end; ++it)
        result.push_back(it->str());
    return result;
}
inline AtomProjection project_atoms(std::string source, std::string header, const std::string& bif_header) {
    replace_once(source, "char* erl_atom_names[]", "static const char *const lb_predefined_names[]");
    replace_once(header, "extern char* erl_atom_names[];", "/* Names are private immutable data in atoms.c. */");
    const std::string guard = "__ERL_ATOM_TABLE_H__";
    for (auto offset = header.find(guard); offset != std::string::npos; offset = header.find(guard))
        header.replace(offset, guard.size(), "LIBBEAM_GENERATED_ATOMS_H");

    const auto flags = std::regex::ECMAScript | std::regex::multiline;
    const auto names = matching_lines(source, std::regex("^  \".*\",$", flags));
    const std::regex atom_index("^#define am_\\w+ make_atom\\(([0-9]+)\\)$", flags);
    std::vector<std::size_t> indices;
    for (std::sregex_iterator it(header.begin(), header.end(), atom_index), end; it != end; ++it)
        indices.push_back(std::stoull((*it)[1].str()));
    std::sort(indices.begin(), indices.end());
    if (names.empty() || indices.size() != names.size())
        throw std::runtime_error("Atom names and numeric identities disagree");
    for (std::size_t i = 0; i < indices.size(); ++i)
        if (indices[i] != i) throw std::runtime_error("Atom identities must be unique and contiguous");
    std::size_t index = names.size();
    for (const std::string name : {"set_cookie", "load_module", "spawn_monitor"}) {
        if (header.find("#define am_" + name + " ") != std::string::npos)
            throw std::runtime_error("Owned atom already appears upstream: " + name);
        replace_once(source, "  0\n};", "  \"" + name + "\",\n  0\n};");
        header += "\n#define am_" + name + " make_atom(" + std::to_string(index++) + ")\n";
    }
    const auto ids = matching_lines(bif_header, std::regex("^#define BIF_\\w+ [0-9]+$", flags));
    if (ids.empty()) throw std::runtime_error("Missing BIF identities");
    const std::string notice =
        "/* SPDX-License-Identifier: Apache-2.0\n"
        " * Copyright Ericsson AB 1996-2026. All Rights Reserved.\n"
        " * Copyright 2026 Leandro Ostera <leandro@ostera.io>\n"
        " * Generated from admitted OTP naming inputs; not a BIF capability list.\n"
        " */\n";
    std::string identities = notice;
    for (const auto& id : ids) identities += id + '\n';
    return {notice + source, notice + header, std::move(identities)};
}
} // namespace libbeam_build
