// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#include <libbeam/engine.hpp>
#include <iostream>
#include <type_traits>

static_assert(!std::is_copy_constructible_v<libbeam::Engine>);
static_assert(!std::is_copy_constructible_v<libbeam::Isolate>);
static_assert(!std::is_copy_constructible_v<libbeam::Call>);
static_assert(!std::is_copy_constructible_v<libbeam::Reclamation>);

int main() {
    auto result = libbeam::Engine::create();
    auto* error = std::get_if<libbeam::Error>(&result);
    if (!error || error->code != libbeam::ErrorCode::not_implemented ||
        error->message != "not implemented: Engine::create") {
        std::cerr << "Expected explicit scaffold failure, not a simulated engine\n";
        return 1;
    }
    return 0;
}
