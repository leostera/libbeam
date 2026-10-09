// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#include <libbeam/engine.hpp>
#include <iostream>
#include <type_traits>
#ifdef LIBBEAM_TEST_NATIVE
#include <erl_embed.h>
#endif

static_assert(!std::is_copy_constructible_v<libbeam::Engine>);
static_assert(!std::is_copy_constructible_v<libbeam::Isolate>);
static_assert(!std::is_copy_constructible_v<libbeam::Call>);
static_assert(!std::is_copy_constructible_v<libbeam::Reclamation>);

int main() {
    auto result = libbeam::Engine::create();
    auto* error = std::get_if<libbeam::Error>(&result);
    if (!error || error->code != libbeam::ErrorCode::not_implemented ||
        error->message.find("not implemented: Engine::create") != 0) {
        std::cerr << "Expected explicit scaffold failure, not a simulated engine\n";
        return 1;
    }
#ifdef LIBBEAM_TEST_NATIVE
    ErlSchedulerThreadInventory threads{};
    erl_scheduler_thread_inventory(&threads);
    if (threads.total != 0 || erl_runtime_startup_phase() != ERL_RUNTIME_UNCLAIMED ||
        error->message.find("ERTS linked") == std::string::npos)
        return 2; // Safe preflight must not start scheduler threads.
#endif
    return 0;
}
