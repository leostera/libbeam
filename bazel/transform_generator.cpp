// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#include "transform_projection.hpp"
#include <fstream>
#include <iostream>
#include <iterator>

int main(int argc, char** argv) {
    try {
        if (argc != 3) throw std::runtime_error("Usage: transform_generator BEAM_OPCODES OUTPUT");
        std::ifstream input(argv[1], std::ios::binary);
        if (!input) throw std::runtime_error("Cannot open opcode source");
        const std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        if (input.bad()) throw std::runtime_error("Cannot read opcode source");
        const auto projected = libbeam_build::project_transform(text);
        std::ofstream output(argv[2], std::ios::binary | std::ios::trunc);
        output << "/* SPDX-License-Identifier: Apache-2.0\n"
                  " * Copyright Ericsson AB 1996-2026. All Rights Reserved.\n"
                  " * Native projection of complete reviewed beam_makeops cases. */\n" << projected;
        output.close();
        if (!output) throw std::runtime_error("Cannot write transform projection");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
