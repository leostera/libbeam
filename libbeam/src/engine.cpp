// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>

#include <libbeam/engine.hpp>
#include <utility>
#ifdef LIBBEAM_LINKED_ERTS
#include <erl_embed.h>
#endif

namespace libbeam {
namespace {
Error missing(const char* operation) {
    return {ErrorCode::not_implemented, std::string("not implemented: ") + operation};
}
} // namespace

// Opaque ABI scaffolding only: none of these contains a VM or an isolate.
// Factories do NOT return successful handles until they own real runtime state.
// Replace these definitions with ownership-bearing state as milestones land.
struct Engine::Impl {};
struct Isolate::Impl {};
struct Call::Impl {};
struct Reclamation::Impl {};

Engine::Engine(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Engine::Engine(Engine&&) noexcept = default;
Engine::~Engine() = default;
Isolate::Isolate(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Isolate::Isolate(Isolate&&) noexcept = default;
Isolate::~Isolate() = default;
Call::Call(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Call::Call(Call&&) noexcept = default;
Call::~Call() = default;
Reclamation::Reclamation(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Reclamation::Reclamation(Reclamation&&) noexcept = default;
Reclamation::~Reclamation() = default;

Result<Engine> Engine::create() {
#ifdef LIBBEAM_LINKED_ERTS
    if (erl_runtime_is_claimed())
        return Error{ErrorCode::invalid_state, "Engine::create: runtime initialization already claimed"};
    // Preparation no longer boots OTP or launches threads. Its global allocations
    // still need ownership/cleanup before a real Engine can safely unwind.
    // Never use erl_start_embedded (the whole-world diagnostic) as this factory.
    return missing("Engine::create (ERTS linked; unbooted initialization cleanup still required)");
#else
    return missing("Engine::create");
#endif
}
Result<Isolate> Engine::create_isolate() { return missing("Engine::create_isolate"); }
Status Engine::shutdown(Deadline) { return missing("Engine::shutdown"); }
Status Isolate::load_module(ByteView) { return missing("Isolate::load_module"); }
Result<Call> Isolate::start(std::string_view, std::string_view, ByteView) {
    return missing("Isolate::start");
}
Result<Call> Isolate::call(std::string_view, std::string_view, ByteView) {
    return missing("Isolate::call");
}
Result<Reclamation> Isolate::stop() { return missing("Isolate::stop"); }
Result<Bytes> Call::wait_until(Deadline) { return missing("Call::wait_until"); }
Status Reclamation::wait_until(Deadline) { return missing("Reclamation::wait_until"); }

} // namespace libbeam
