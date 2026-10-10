// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>

#include <libbeam/engine.hpp>
#include "engine.h"
#include <new>
#include <utility>

namespace libbeam {
namespace {
Error missing(const char* operation) {
    return {ErrorCode::not_implemented, std::string("not implemented: ") + operation};
}
} // namespace

// The adapter owns one C handle, not a parallel runtime/ownership graph.
struct Engine::Impl {
    LbEngine* runtime = nullptr;
    ~Impl() { lb_engine_release(runtime); }
};
// No successful handles for the not-yet-implemented public world lifecycle.
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
    // Preserved subtractive-experiment limitation (not this factory):
    // erl_prepare_shared_runtime now stops before diagnostic world tables.
    // Its remaining backend/thread-library state still prevents safe unwind.
    // Neither erl_prepare_runtime nor erl_start_embedded is a factory shortcut.
    try {
        auto impl = std::make_unique<Impl>();
        const auto status = lb_engine_create(nullptr, &impl->runtime);
        if (status == LB_ENGINE_NO_MEMORY)
            return Error{ErrorCode::limit, "out of memory"};
        if (status != LB_ENGINE_OK)
            return Error{ErrorCode::internal_error, "Engine construction failed"};
        return Engine(std::move(impl));
    } catch (const std::bad_alloc&) {
        // Even diagnostic allocation may fail. The empty string allocates no
        // storage; RAII has already unwound any constructed C ownership prefix.
        return Error{ErrorCode::limit, {}};
    }
}
Result<Isolate> Engine::create_isolate() {
    if (!impl_) return Error{ErrorCode::invalid_state, "moved Engine"};
    if (!lb_engine_is_open(impl_->runtime)) return Error{ErrorCode::closed, "Engine closed"};
    return missing("Engine::create_isolate");
}
Status Engine::shutdown(Deadline deadline) {
    if (!impl_) return Error{ErrorCode::invalid_state, "moved Engine"};
    if (!lb_engine_is_open(impl_->runtime)) return Error{ErrorCode::closed, "Engine closed"};
    if (std::chrono::steady_clock::now() >= deadline)
        return Error{ErrorCode::timeout, "shutdown deadline expired"};
    switch (lb_engine_shutdown(impl_->runtime)) {
    case LB_ENGINE_OK: return std::nullopt;
    case LB_ENGINE_BUSY: return Error{ErrorCode::busy, "Engine has live children"};
    case LB_ENGINE_CLOSED: return Error{ErrorCode::closed, "Engine closed"};
    default: return Error{ErrorCode::internal_error, "Engine shutdown failed"};
    }
}
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
