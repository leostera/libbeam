// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#include <fstream>
#include <iostream>
#include <string>
int main(int argc, char** argv) {
    constexpr int source_version = 1;
    if (argc != 2) return 2;
    std::ifstream file(argv[1]);
    std::string payload;
    if (!std::getline(file, payload)) return 2;
    if (source_version != 1 || payload != "original") return 1;
    std::cout << "CACHE_PROBE_OK\n";
    return 0;
}
