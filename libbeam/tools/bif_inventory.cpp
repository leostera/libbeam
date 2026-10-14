// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#include "../tests/bif_inventory.hpp"
#include <iostream>

int main(int argc, char** argv) {
    constexpr auto usage = "Usage: bif_inventory --root CHECKOUT\nSource facts only; not capability admission or test coverage.\n";
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        std::cout << usage;
        return 0;
    }
    if (argc != 3 || std::string_view(argv[1]) != "--root") {
        std::cerr << usage;
        return 2;
    }
    try {
        const auto root = std::filesystem::absolute(argv[2]);
        const auto directory = root / "libbeam/core/otp";
        const auto report = libbeam_test::inventory::audit(root,
            directory / "bif-batch-request.txt");
        // Serialize only after the complete audit succeeds: no partial report
        // may look like a successful inventory after a parsing/read failure.
        std::cout << report.dump(2) << '\n';
        return std::cout.good() ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "BIF inventory failed: " << error.what() << '\n';
        return 1;
    }
}
