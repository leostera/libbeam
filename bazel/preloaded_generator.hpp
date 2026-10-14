// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace libbeam_build {
inline std::string extract_preloaded(std::string_view source) {
    std::string result =
        "%% SPDX-License-Identifier: Apache-2.0\n"
        "%% Copyright Ericsson AB 1996-2026. All Rights Reserved.\n"
        "%% Complete functions extracted without body changes.\n"
        "-module(erlang).\n-compile(no_auto_import).\n"
        "-export([spawn/1,spawn_monitor/1]).\n";
    const std::string_view starts[] = {
        "spawn(F) when erlang:is_function(F) ->",
        "spawn_monitor(F) when erlang:is_function(F, 0) ->",
        "badarg_with_info(Args) ->",
    };
    bool first = true;
    for (const auto start : starts) {
        const auto anchor = "\n" + std::string(start);
        const auto position = source.find(anchor);
        if (position == std::string_view::npos ||
            source.find(anchor, position + anchor.size()) != std::string_view::npos)
            throw std::runtime_error("Missing or ambiguous preloaded function: " + std::string(start));
        const auto begin = position + 1;
        const auto end = source.find(".\n", begin);
        if (end == std::string_view::npos)
            throw std::runtime_error("Unterminated preloaded function: " + std::string(start));
        if (!first) result += '\n';
        first = false;
        result += source.substr(begin, end + 2 - begin);
    }
    return result;
}

inline std::string embed_preloaded(std::string_view beam) {
    if (beam.size() < 12 || beam.substr(0, 4) != "FOR1" || beam.substr(8, 4) != "BEAM")
        throw std::runtime_error("Expected ordinary compiler-produced BEAM");
    std::uint32_t declared = 0;
    for (unsigned i = 4; i < 8; ++i)
        declared = (declared << 8) | static_cast<unsigned char>(beam[i]);
    if (declared != beam.size() - 8)
        throw std::runtime_error("BEAM container length mismatch");
    std::string result =
        "/* SPDX-License-Identifier: Apache-2.0\n"
        " * Copyright Ericsson AB 1996-2026. All Rights Reserved.\n"
        " * Compiler-produced BEAM; not an alternate instruction encoding. */\n"
        "static const unsigned char lb_preloaded_erlang[] = {\n";
    for (std::size_t i = 0; i < beam.size(); ++i) {
        result += std::to_string(static_cast<unsigned char>(beam[i]));
        result += ',';
        if (i % 24 == 23 || i + 1 == beam.size()) result += '\n';
    }
    result += "};\n";
    return result;
}
} // namespace libbeam_build
