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
    case LB_WORLD_RESOURCE_LIMIT: return {ErrorCode::limit, "runtime resource limit exceeded"};
    case LB_WORLD_MEMORY_LIMIT: return {ErrorCode::memory_limit, "Isolate accounted memory limit exceeded"};
    case LB_WORLD_WORK_LIMIT: return {ErrorCode::work_limit, "invocation work limit exceeded"};
    case LB_WORLD_EXECUTION_REFUSED: return {ErrorCode::unsupported, "execution outside admitted ownership profile"};
    case LB_WORLD_ENVIRONMENT: return {ErrorCode::internal_error, "floating-point environment failure"};
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
std::string diagnostic_name(const unsigned char* data, std::size_t size) {
    static const char hex[] = "0123456789abcdef";
    std::string result;
    for (std::size_t i = 0; i < size; ++i) {
        const auto byte = data[i];
        if (byte < 32 || byte == 127 || byte == '\\' || byte == '\"') {
            result += "\\x"; result += hex[byte >> 4]; result += hex[byte & 15];
        } else result += static_cast<char>(byte);
    }
    return result;
}
LbBytesView bytes(ByteView view) { return {view.data, view.size}; }
LbBytesView name(std::string_view view) {
    return {reinterpret_cast<const unsigned char*>(view.data()), view.size()};
}
Error load_error(LbWorldStatus status, const LbWorldLoadReport& report) {
    auto error = world_error(status);
    if (status == LB_WORLD_BAD_BEAM || status == LB_WORLD_UNSUPPORTED || status == LB_WORLD_MEMORY_LIMIT) {
        const std::string stage = report.stage ? report.stage : "unknown";
        error.message += " (stage=" + stage +
            (stage == "unresolved import" ? ", import_index=" : ", offset=") +
            std::to_string(report.offset);
        if (report.has_import) error.message += ", mfa=\"" +
            diagnostic_name(report.module, report.module_size) + "\":\"" +
            diagnostic_name(report.function, report.function_size) + "\"/" + std::to_string(report.arity);
        if (report.has_module_index)
            error.message += ", module_index=" + std::to_string(report.module_index);
        error.message += ")";
    }
    return error;
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

Result<Engine> Engine::create(ResourceLimits limits) {
    // Preserved subtractive-experiment limitation (not this factory):
    // erl_prepare_shared_runtime now stops before diagnostic world tables.
    // Its remaining backend/thread-library state still prevents safe unwind.
    // Neither erl_prepare_runtime nor erl_start_embedded is a factory shortcut.
    try {
        if (!limits.memory_limit_bytes || !limits.invocation_work_limit)
            return Error{ErrorCode::invalid_argument, "resource limits must be finite and positive"};
        auto impl = std::make_unique<Impl>();
        const LbResourceLimits selected{limits.memory_limit_bytes, limits.invocation_work_limit};
        const auto status = lb_engine_create_with_limits(nullptr, &selected, &impl->runtime);
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
Result<Isolate> Engine::create_isolate(ResourceOverrides overrides) {
    try {
        if (!usable(impl_)) return invalid_handle();
        auto isolate = std::make_unique<Isolate::Impl>();
        const LbResourceOverrides selected{
            overrides.memory_limit_bytes.has_value(), overrides.invocation_work_limit.has_value(),
            overrides.memory_limit_bytes.value_or(0), overrides.invocation_work_limit.value_or(0)};
        const auto status = lb_world_create_with_overrides(impl_->runtime, &selected, &isolate->runtime);
        if (status != LB_WORLD_OK) return world_error(status);
        return Isolate(std::move(isolate));
    } catch (const std::bad_alloc&) { return Error{ErrorCode::limit, {}}; }
}
Result<ResourceUsage> Isolate::resource_usage() {
    try {
        if (!usable(impl_)) return invalid_handle();
        LbWorldResourceUsage usage;
        const auto status = lb_world_resource_usage(impl_->runtime, &usage);
        if (status != LB_WORLD_OK) return world_error(status);
        return ResourceUsage{{usage.limits.memory_bytes, usage.limits.invocation_work},
                             usage.current, usage.peak, usage.closed != 0};
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
        return load_modules(std::vector<ByteView>{input});
    } catch (const std::bad_alloc&) { return Error{ErrorCode::limit, {}}; }
}
Status Isolate::load_modules(const std::vector<ByteView>& modules) {
    try {
        if (!usable(impl_)) return invalid_handle();
        std::vector<LbBytesView> views;
        views.reserve(modules.size());
        for (const auto& module : modules) views.push_back(bytes(module));

        LbWorldLoadReport report{};
        const auto status = lb_world_load_batch_report(impl_->runtime, views.data(), views.size(), &report);
        if (status != LB_WORLD_OK) return load_error(status, report);
        return std::nullopt;
    } catch (const std::bad_alloc&) {
        return Error{ErrorCode::limit, {}};
    }
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
Status Call::cancel() {
    try {
        if (!usable(impl_)) return invalid_handle();
        const auto status = lb_call_cancel(impl_->runtime);
        if (status != LB_WORLD_OK) return world_error(status);
        return std::nullopt;
    } catch (const std::bad_alloc&) { return Error{ErrorCode::limit, {}}; }
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
