// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#pragma once

// EXPERIMENTAL API: signatures express the target contract below.
// Operations remain unimplemented. A linked ERTS build also checks native
// lifecycle state before reporting the missing cooperative stop/join protocol.
// This API does not yet start BEAM, create worlds, or claim a stable public ABI.
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace libbeam {

using Bytes = std::vector<std::uint8_t>;
using Deadline = std::chrono::steady_clock::time_point;

struct ByteView {
    const std::uint8_t* data;
    std::size_t size;
};

enum class ErrorCode {
    not_implemented, invalid_argument, invalid_beam, unsupported, invalid_state, full, limit,
    closed, cancelled, exception, timeout, busy, internal_error
};
struct Error {
    ErrorCode code;
    std::string message; // Bounded diagnostic, never a raw VM term/pointer.
};
template<class T> using Result = std::variant<T, Error>;
using Status = std::optional<Error>; // No error means success.

class Call {
public:
    Call(Call&&) noexcept;
    Call(const Call&) = delete;
    Call& operator=(const Call&) = delete;
    ~Call();

    // Wait on this correlated completion; schedulers and other isolates proceed.
    // Success transfers host-owned bytes. Timeout does not cancel the invocation;
    // the same handle can be waited on again. A terminal result is consumed once.
    Result<Bytes> wait_until(Deadline);
private:
    friend class Isolate;
    struct Impl;
    explicit Call(std::unique_ptr<Impl>);
    std::unique_ptr<Impl> impl_;
};

class Reclamation {
public:
    Reclamation(Reclamation&&) noexcept;
    Reclamation(const Reclamation&) = delete;
    Reclamation& operator=(const Reclamation&) = delete;
    ~Reclamation();

    // Success means physical reclamation, not merely acknowledgement of stop.
    // Timeout retains safe ownership and permits another wait; never force-free.
    Status wait_until(Deadline);
private:
    friend class Isolate;
    struct Impl;
    explicit Reclamation(std::unique_ptr<Impl>);
    std::unique_ptr<Impl> impl_;
};

class Isolate {
public:
    Isolate(Isolate&&) noexcept;
    Isolate(const Isolate&) = delete;
    Isolate& operator=(const Isolate&) = delete;
    ~Isolate();

    // Copies/consumes bytes before return, derives the module name from BEAM,
    // and publishes only in this isolate. No filesystem paths or module aliases.
    // Load failure rolls back unpublished state. Loading after start is rejected.
    Status load_module(ByteView);

    // Atomically enters running state and admits the initial binary arity-one
    // invocation. A failed admission leaves the isolate loaded, not half-started.
    Result<Call> start(std::string_view module, std::string_view function, ByteView);
    Result<Call> call(std::string_view module, std::string_view function, ByteView);

    // Closes admission synchronously, then asynchronously cancels/terminates and
    // drains the entire world, including processes outliving their invocations.
    // The handle remains a closed tombstone after reclamation; it never revives.
    Result<Reclamation> stop();
private:
    friend class Engine;
    struct Impl;
    explicit Isolate(std::unique_ptr<Impl>);
    std::unique_ptr<Impl> impl_;
};

class Engine {
public:
    Engine(Engine&&) noexcept;
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    ~Engine();

    static Result<Engine> create();
    Result<Isolate> create_isolate(); // Fresh world, not a pooled tenant context.
    // Requires reclaimed isolates and released handles; otherwise returns busy.
    // Success includes thread joins and engine resource release, without exit().
    Status shutdown(Deadline);
private:
    struct Impl;
    explicit Engine(std::unique_ptr<Impl>);
    std::unique_ptr<Impl> impl_;
};

// All management/wait operations initially use one host control thread. No host
// callbacks run under VM locks. Inputs (including MFA names) are copied on
// admission. Guest calls run in ordinary isolate-owned Erlang processes.
// Target bounds: 64 KiB/call payload or result, 64 outstanding calls/isolate,
// 1 MiB queued payload/isolate, with terminal-status capacity reserved separately.
// These are NOT heap/CPU budgets. Accepted calls get one terminal completion.
// Dropping handles is not proof of shutdown: destructors must never force-free
// reachable VM state or terminate the host. Error-path draining remains an
// implementation obligation; the example's successful path explicitly reclaims.

} // namespace libbeam
