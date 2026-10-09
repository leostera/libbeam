// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>

// API-first progress test. The initial scaffold links but fails at Engine::create.
// Usage: two_isolates /host/a/probe.beam /host/b/probe.beam
#include <libbeam/engine.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace beam = libbeam;
using namespace std::chrono_literals;

static beam::Deadline deadline() {
    return std::chrono::steady_clock::now() + 5s;
}

static void require(beam::Status status) {
    if (status) throw std::runtime_error(status->message);
}

template<class T>
static T require(beam::Result<T> result) {
    if (auto* error = std::get_if<beam::Error>(&result))
        throw std::runtime_error(error->message);
    return std::move(std::get<T>(result));
}

static beam::ByteView view(const beam::Bytes& bytes) {
    return {bytes.data(), bytes.size()};
}

static beam::ByteView view(std::string_view text) {
    return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()};
}

static beam::Bytes read_beam(const char* path) {
    // Host I/O only. The VM gets bytes, never a filename or filesystem authority.
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error(std::string("cannot open ") + path);
    const auto size = file.tellg();
    if (size <= 0 || size > 8 * 1024 * 1024)
        throw std::runtime_error("invalid fixture size (host example limit: 8 MiB)");
    beam::Bytes bytes(static_cast<std::size_t>(size));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size)))
        throw std::runtime_error("cannot read complete BEAM fixture");
    return bytes;
}

static void expect(beam::Call& call, std::string_view wanted) {
    auto result = require(call.wait_until(deadline()));
    if (beam::Bytes(wanted.begin(), wanted.end()) != result)
        throw std::runtime_error("unexpected guest result; expected " + std::string(wanted));
}

static beam::Call request(beam::Isolate& isolate, std::string_view command) {
    return require(isolate.call("probe", "request", view(command)));
}

static void expect_request(beam::Isolate& isolate,
                           std::string_view command, std::string_view wanted) {
    auto call = request(isolate, command);
    expect(call, wanted);
}

static void expect_closed(beam::Isolate& isolate) {
    auto result = isolate.call("probe", "request", view("next"));
    auto* error = std::get_if<beam::Error>(&result);
    if (!error || error->code != beam::ErrorCode::closed)
        throw std::runtime_error("closed isolate accepted a call");
}

static void reclaim(beam::Isolate& isolate) {
    auto stopped = require(isolate.stop());
    require(stopped.wait_until(deadline()));
}

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: two_isolates a/probe.beam b/probe.beam\n";
        return 2;
    }
    try {
        auto code_a = read_beam(argv[1]);
        auto engine = require(beam::Engine::create());
        beam::Bytes retained_reply;
        {
            auto a = require(engine.create_isolate());
            auto b = require(engine.create_isolate());
            require(a.load_module(view(code_a)));
            {
                auto code_b = read_beam(argv[2]);
                require(b.load_module(view(code_b)));
            } // Host module bytes can be released before execution starts.

            // Both modules declare -module(probe). No renamed module or second VM.
            // Admit both starts before waiting. Each starts a persistent process
            // registered under exactly the same local name: probe_state.
            {
                auto boot_a = require(a.start("probe", "boot", view("")));
                auto boot_b = require(b.start("probe", "boot", view("")));
                expect(boot_a, "A:ready");
                expect(boot_b, "B:ready");
            }
            {
                auto first_a = request(a, "next");
                auto first_b = request(b, "next");
                expect(first_b, "B:1"); // Waiting order is not submission order.
                expect(first_a, "A:1");
            }
            expect_request(a, "next", "A:2");
            expect_request(b, "read", "B:1"); // A did not change B's state.

            {
                auto snapshot = request(a, "read");
                retained_reply = require(snapshot.wait_until(deadline()));
                if (retained_reply != beam::Bytes({'A', ':', '2'}))
                    throw std::runtime_error("wrong snapshot");
            }
            {
                auto b_during_stop = request(b, "next");
                auto a_reclaimed = require(a.stop());
                expect_closed(a); // Admission closes before reclamation finishes.
                expect(b_during_stop, "B:2");
                require(a_reclaimed.wait_until(deadline())); // Physical release.
            }
            // A's persistent probe_state process and its code must now be gone.
            // A live C++ tombstone must not resolve to a later isolate generation.
            expect_closed(a);
            expect_request(b, "next", "B:3");

            {
                auto replacement = require(engine.create_isolate());
                require(replacement.load_module(view(code_a)));
                {
                    auto boot = require(replacement.start("probe", "boot", view("")));
                    expect(boot, "A:ready");
                }
                expect_closed(a); // A's tombstone cannot address the replacement.
                expect_request(replacement, "read", "A:0"); // No inherited state.
                expect_request(replacement, "next", "A:1");
                expect_request(b, "read", "B:3");
                reclaim(replacement);
            }
            reclaim(b);
        } // Release isolate/call/reclamation handles before shutting down engine.
        require(engine.shutdown(deadline()));
        if (retained_reply != beam::Bytes({'A', ':', '2'}))
            throw std::runtime_error("host-owned reply changed after reclamation");
        std::cout << "two isolates, independent state, replacement, clean shutdown: OK\n";
        return 0;
    } catch (const std::exception& error) {
        // A failed proof, including timeout, is never successful reclamation.
        // The eventual runner also needs an external watchdog for host hangs.
        std::cerr << "two-isolate proof FAILED: " << error.what() << '\n';
        return 1;
    }
}
