// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#include "preloaded_generator.hpp"
#include <fstream>
#include <iostream>
#include <iterator>

int main(int argc, char** argv) {
    try {
        if (argc != 4) throw std::runtime_error("Usage: preloaded_generator extract|embed INPUT OUTPUT");
        const std::string mode = argv[1];
        if (mode != "extract" && mode != "embed") throw std::runtime_error("Unknown generation mode");
        std::ifstream input(argv[2], std::ios::binary);
        if (!input) throw std::runtime_error("Cannot open input");
        const std::string bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        if (input.bad()) throw std::runtime_error("Cannot read input");
        const auto generated = mode == "extract" ? libbeam_build::extract_preloaded(bytes)
                                                 : libbeam_build::embed_preloaded(bytes);
        // Invalid input never truncates an existing output.
        std::ofstream output(argv[3], std::ios::binary | std::ios::trunc);
        output.write(generated.data(), static_cast<std::streamsize>(generated.size()));
        output.close();
        if (!output) throw std::runtime_error("Cannot write output");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
