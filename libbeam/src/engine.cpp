// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>

#include <libbeam/engine.hpp>
#include "world.h"
#include <algorithm>
#include <thread>
#include <new>
#include <utility>

namespace libbeam {
namespace {
Error world_error(LbWorldStatus status) {
    switch (status) {
    case LB_WORLD_INVALID: return {ErrorCode::invalid_argument, "invalid argument"};
    case LB_WORLD_INVALID_STATE: return {ErrorCode::invalid_state, "invalid Isolate state"};
    case LB_WORLD_CLOSED: return {ErrorCode::closed, "handle closed"};
    case LB_WORLD_FULL: return {ErrorCode::full, "call capacity exhausted"};
    case LB_WORLD_LIMIT: return {ErrorCode::limit, "transport limit exceeded"};
    case LB_WORLD_NO_MEMORY: return {ErrorCode::limit, "out of memory"};
    case LB_WORLD_BAD_BEAM: return {ErrorCode::invalid_beam, "invalid BEAM image"};
    case LB_WORLD_UNSUPPORTED: return {ErrorCode::unsupported, "unsupported BEAM profile or import"};
    case LB_WORLD_NOT_FOUND: return {ErrorCode::invalid_argument, "unknown arity-one MFA"};
    case LB_WORLD_EXCEPTION: return {ErrorCode::exception, "guest exception"};
    case LB_WORLD_CANCELLED: return {ErrorCode::cancelled, "invocation cancelled"};
    case LB_WORLD_BAD_RESULT: return {ErrorCode::unsupported, "call did not return a binary"};
    default: return {ErrorCode::internal_error, "unexpected runtime status"};
    }
}
struct ControlThread { std::thread::id thread = std::this_thread::get_id(); };
template<class T> bool usable(const std::unique_ptr<T>& impl) {
    return impl && impl->thread == std::this_thread::get_id();
}
Error invalid_handle() { return {ErrorCode::invalid_state, "moved handle or wrong control thread"}; }
LbBytesView bytes(ByteView view) { return {view.data, view.size}; }
LbBytesView name(std::string_view view) {
    return {reinterpret_cast<const unsigned char*>(view.data()), view.size()};
}
bool wait_tick(Deadline deadline) {
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) return false;
    std::this_thread::sleep_until(std::min(deadline, now + std::chrono::milliseconds(1)));
    return true;
}
} // namespace

// The adapter owns one C handle, not a parallel runtime/ownership graph.
struct Engine::Impl : ControlThread {
    LbEngine* runtime = nullptr;
    ~Impl() { lb_engine_release(runtime); }
};
struct Isolate::Impl : ControlThread {
    LbWorld* runtime = nullptr;
    ~Impl() { lb_world_release(runtime); }
};
struct Call::Impl : ControlThread {
    LbCall* runtime = nullptr;
    ~Impl() { lb_call_release(runtime); }
};
struct Reclamation::Impl : ControlThread {
    LbReclamation* runtime = nullptr;
    ~Impl() { lb_reclamation_release(runtime); }
};

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
    try {
        if (!usable(impl_)) return invalid_handle();
        auto isolate = std::make_unique<Isolate::Impl>();
        const auto status = lb_world_create(impl_->runtime, &isolate->runtime);
        if (status != LB_WORLD_OK) return world_error(status);
        return Isolate(std::move(isolate));
    } catch (const std::bad_alloc&) { return Error{ErrorCode::limit, {}}; }
}
Status Engine::shutdown(Deadline deadline) {
    try {
        if (!usable(impl_)) return invalid_handle();
        if (!lb_engine_is_open(impl_->runtime)) return Error{ErrorCode::closed, "Engine closed"};
        if (std::chrono::steady_clock::now() >= deadline)
            return Error{ErrorCode::timeout, "shutdown deadline expired"};
        switch (lb_engine_shutdown(impl_->runtime)) {
        case LB_ENGINE_OK: return std::nullopt;
        case LB_ENGINE_BUSY: return Error{ErrorCode::busy, "Engine has live children"};
        case LB_ENGINE_CLOSED: return Error{ErrorCode::closed, "Engine closed"};
        default: return Error{ErrorCode::internal_error, "Engine shutdown failed"};
        }
    } catch (const std::bad_alloc&) { return Error{ErrorCode::limit, {}}; }
}
Status Isolate::load_module(ByteView input) {
    try {
        if (!usable(impl_)) return invalid_handle();
        const auto status = lb_world_load(impl_->runtime, bytes(input));
        if (status != LB_WORLD_OK) return world_error(status);
        return std::nullopt;
    } catch (const std::bad_alloc&) { return Error{ErrorCode::limit, {}}; }
}
Result<Call> Isolate::invoke(bool starting, std::string_view module, std::string_view function, ByteView input) {
    try {
        if (!usable(impl_)) return invalid_handle();
        auto call = std::make_unique<Call::Impl>();
        const auto status = starting
            ? lb_world_start(impl_->runtime, name(module), name(function), bytes(input), &call->runtime)
            : lb_world_call(impl_->runtime, name(module), name(function), bytes(input), &call->runtime);
        if (status != LB_WORLD_OK) return world_error(status);
        return Call(std::move(call));
    } catch (const std::bad_alloc&) { return Error{ErrorCode::limit, {}}; }
}
Result<Call> Isolate::start(std::string_view module, std::string_view function, ByteView input) {
    return invoke(true, module, function, input);
}
Result<Call> Isolate::call(std::string_view module, std::string_view function, ByteView input) {
    return invoke(false, module, function, input);
}
Result<Reclamation> Isolate::stop() {
    try {
        if (!usable(impl_)) return invalid_handle();
        auto reclamation = std::make_unique<Reclamation::Impl>();
        const auto status = lb_world_stop(impl_->runtime, &reclamation->runtime);
        if (status != LB_WORLD_OK) return world_error(status);
        return Reclamation(std::move(reclamation));
    } catch (const std::bad_alloc&) { return Error{ErrorCode::limit, {}}; }
}
Result<Bytes> Call::wait_until(Deadline deadline) {
    try {
        if (!usable(impl_)) return invalid_handle();
        LbBytesView view{};
        LbWorldStatus status;
        while ((status = lb_call_poll(impl_->runtime, &view)) == LB_WORLD_PENDING)
            if (!wait_tick(deadline)) return Error{ErrorCode::timeout, "call deadline expired"};
        if (status == LB_WORLD_OK) {
            Bytes result;
            if (view.size) result.assign(view.data, view.data + view.size);
            if (lb_call_consume(impl_->runtime) != LB_WORLD_OK)
                return Error{ErrorCode::internal_error, "completion consumption failed"};
            return result;
        }
        auto error = world_error(status);
        if (status != LB_WORLD_CLOSED) lb_call_consume(impl_->runtime);
        return error;
    } catch (const std::bad_alloc&) {
        // Host-copy failure is retryable: the terminal C completion is untouched.
        return Error{ErrorCode::limit, {}};
    }
}
Status Reclamation::wait_until(Deadline deadline) {
    try {
        if (!usable(impl_)) return invalid_handle();
        LbWorldStatus status;
        while ((status = lb_reclamation_poll(impl_->runtime)) == LB_WORLD_PENDING)
            if (!wait_tick(deadline)) return Error{ErrorCode::timeout, "reclamation deadline expired"};
        if (status != LB_WORLD_OK) return world_error(status);
        status = lb_reclamation_consume(impl_->runtime);
        if (status != LB_WORLD_OK) return world_error(status);
        return std::nullopt;
    } catch (const std::bad_alloc&) { return Error{ErrorCode::limit, {}}; }
}

} // namespace libbeam
