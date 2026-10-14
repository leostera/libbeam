// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#include "atom_generator.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

namespace {
std::string read(const char* path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open input: " + std::string(path));
    std::string bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (input.bad()) throw std::runtime_error("Cannot read input: " + std::string(path));
    return bytes;
}
void write(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.close();
    if (!output) throw std::runtime_error("Cannot write output: " + path.string());
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc != 5) throw std::runtime_error("Usage: atom_generator ATOM_SOURCE ATOM_HEADER BIF_HEADER OUTPUT_DIRECTORY");
        const auto result = libbeam_build::project_atoms(read(argv[1]), read(argv[2]), read(argv[3]));
        const std::filesystem::path directory(argv[4]);
        std::filesystem::create_directories(directory);
        write(directory / "lb_atoms_generated.inc", result.source);
        write(directory / "lb_atoms_generated.h", result.header);
        write(directory / "lb_bif_ids_generated.h", result.bif_ids);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
