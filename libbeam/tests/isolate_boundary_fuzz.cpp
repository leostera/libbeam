// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#include <libbeam/engine.hpp>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace {
using namespace libbeam;

[[noreturn]] void bug() { std::abort(); }
Deadline deadline() { return std::chrono::steady_clock::now() + std::chrono::seconds(2); }
ByteView view(const Bytes& bytes) { return {bytes.data(), bytes.size()}; }
ByteView view(std::string_view bytes) {
    return {reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()};
}
std::string fixture_path(const char* path) {
    if (const char* directory = std::getenv("TEST_SRCDIR")) {
        const char* workspace = std::getenv("TEST_WORKSPACE");
        if (!workspace) bug();
        return std::string(directory) + "/" + workspace + "/" + path;
    }
    if (const char* directory = std::getenv("RUNFILES_DIR"))
        return std::string(directory) + "/_main/" + path;
    if (const char* manifest = std::getenv("RUNFILES_MANIFEST_FILE")) {
        std::ifstream entries(manifest);
        std::string line, key = std::string("_main/") + path;
        while (std::getline(entries, line)) {
            const auto separator = line.find(' ');
            if (separator != std::string::npos && line.substr(0, separator) == key)
                return line.substr(separator + 1);
        }
    }
    bug();
}
Bytes read_fixture(const char* path) {
    std::ifstream input(fixture_path(path), std::ios::binary);
    if (!input) bug();
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
const Bytes& image_a() {
    static const Bytes image = read_fixture("libbeam/fixtures/probe_a_fixture/probe.beam");
    return image;
}
const Bytes& image_b() {
    static const Bytes image = read_fixture("libbeam/fixtures/probe_b_fixture/probe.beam");
    return image;
}
Bytes result(Call call, bool allow_guest_exception = false) {
    auto completed = call.wait_until(deadline());
    if (auto* error = std::get_if<Error>(&completed)) {
        const bool denied_external_pid = allow_guest_exception &&
            (error->code == ErrorCode::exception || error->code == ErrorCode::unsupported ||
             error->code == ErrorCode::capability_denied);
        if (!denied_external_pid) {
            std::fprintf(stderr, "unexpected guest result error code=%d message=%s allow_exception=%d\n", static_cast<int>(error->code), error->message.c_str(), allow_guest_exception);
            bug();
        }
        return {};
    }
    return std::get<Bytes>(std::move(completed));
}
Bytes call(Isolate& isolate, const char* function, ByteView input = {nullptr, 0}) {
    auto started = isolate.call("probe", function, input);
    if (auto* error = std::get_if<Error>(&started)) {
        // Cross-isolate PIDs may be rejected by the external-term boundary.
        if (std::string_view(function) == "foreign" &&
            (error->code == ErrorCode::exception || error->code == ErrorCode::unsupported ||
             error->code == ErrorCode::capability_denied))
            return {};
        bug();
    }
    return result(std::get<Call>(std::move(started)), std::string_view(function) == "foreign");
}
Bytes bytes(std::string_view text) { return Bytes(text.begin(), text.end()); }
void append_u32(Bytes& output, std::uint32_t value) {
    output.push_back(static_cast<std::uint8_t>(value >> 24));
    output.push_back(static_cast<std::uint8_t>(value >> 16));
    output.push_back(static_cast<std::uint8_t>(value >> 8));
    output.push_back(static_cast<std::uint8_t>(value));
}
Bytes forged_pid(const std::uint8_t* data, std::size_t size, std::size_t offset) {
    const auto byte = [&](std::size_t n) -> std::uint8_t {
        return size ? data[(offset + n) % size] : 0;
    };
    const bool modern = (byte(0) & 1u) != 0;
    const std::uint32_t id = byte(1) % 8u;
    const std::uint32_t serial = byte(2) % 2u;
    Bytes encoded{131, static_cast<std::uint8_t>(modern ? 88 : 103), 119, 13};
    constexpr std::string_view node = "nonode@nohost";
    encoded.insert(encoded.end(), node.begin(), node.end());
    append_u32(encoded, id);
    append_u32(encoded, serial);
    if (modern) append_u32(encoded, byte(3) % 2u);
    else encoded.push_back(byte(3) % 2u);
    return encoded;
}
void expect(Bytes actual, std::string_view wanted) {
    if (actual != bytes(wanted)) {
        std::fprintf(stderr, "isolation invariant failed: expected=%.*s actual=%.*s\n",
            static_cast<int>(wanted.size()), wanted.data(), static_cast<int>(actual.size()),
            reinterpret_cast<const char*>(actual.data()));
        bug();
    }
}
void check_count(Isolate& isolate, char version, unsigned count) {
    const auto command = bytes("read");
    const auto actual = call(isolate, "request", view(command));
    const auto wanted = std::string(1, version) + ":" + std::to_string(count);
    expect(actual, wanted);
}

void exercise(const std::uint8_t* data, std::size_t size) {
    if (size > 64) return;
    auto engine_result = Engine::create();
    if (auto* error = std::get_if<Error>(&engine_result)) {
        (void)error;
        bug();
    }
    auto engine = std::get<Engine>(std::move(engine_result));
    {
    auto a_result = engine.create_isolate();
    auto b_result = engine.create_isolate();
    if (!std::holds_alternative<Isolate>(a_result) || !std::holds_alternative<Isolate>(b_result)) bug();
    auto a = std::get<Isolate>(std::move(a_result));
    auto b = std::get<Isolate>(std::move(b_result));
    if (a.load_module(view(image_a())) || b.load_module(view(image_b()))) bug();

    auto boot_a = a.start("probe", "boot", view(std::string_view{}));
    auto boot_b = b.start("probe", "boot", view(std::string_view{}));
    if (!std::holds_alternative<Call>(boot_a) || !std::holds_alternative<Call>(boot_b)) bug();
    expect(result(std::get<Call>(std::move(boot_a))), "A:ready");
    expect(result(std::get<Call>(std::move(boot_b))), "B:ready");
    unsigned count_a = 0, count_b = 0;
    for (std::size_t i = 0; i < size; ++i) {
        switch (data[i] % 7) {
        case 0: {
            const auto command = bytes("next");
            expect(call(a, "request", view(command)), "A:" + std::to_string(++count_a));
            break;
        }
        case 1: {
            const auto command = bytes("next");
            expect(call(b, "request", view(command)), "B:" + std::to_string(++count_b));
            break;
        }
        case 2: check_count(a, 'A', count_a); break;
        case 3: check_count(b, 'B', count_b); break;
        case 4: {
            // Feed A's externally serialized PID to B. No result or side effect
            // may cross back into A; local aliasing in B is also tracked below.
            const auto pid = forged_pid(data, size, i);
            const auto reply = call(b, "foreign", view(pid));
            if (reply == bytes("A:cross") || reply == bytes("B:cross")) {
                std::fprintf(stderr, "foreign PID routed in B->A probe: %.*s\n", static_cast<int>(reply.size()), reply.data());
                bug();
            }
            check_count(a, 'A', count_a);
            check_count(b, 'B', count_b);
            break;
        }
        case 5: {
            const auto pid = forged_pid(data, size, i + 1);
            const auto reply = call(a, "foreign", view(pid));
            if (reply == bytes("A:cross") || reply == bytes("B:cross")) {
                std::fprintf(stderr, "foreign PID routed in A->B probe: %.*s\n", static_cast<int>(reply.size()), reply.data());
                bug();
            }
            check_count(a, 'A', count_a);
            check_count(b, 'B', count_b);
            break;
        }
        case 6: {
            // Submit both sides before waiting to vary scheduling/order.
            const auto next_a = bytes("next"), next_b = bytes("next");
            auto pending_a = a.call("probe", "request", view(next_a));
            auto pending_b = b.call("probe", "request", view(next_b));
            if (!std::holds_alternative<Call>(pending_a) || !std::holds_alternative<Call>(pending_b)) bug();
            expect(result(std::get<Call>(std::move(pending_b))), "B:" + std::to_string(++count_b));
            expect(result(std::get<Call>(std::move(pending_a))), "A:" + std::to_string(++count_a));
            break;
        }
        }
    }

    check_count(a, 'A', count_a);
    check_count(b, 'B', count_b);
    auto stop_a = a.stop();
    auto stop_b = b.stop();
    if (!std::holds_alternative<Reclamation>(stop_a) || !std::holds_alternative<Reclamation>(stop_b)) bug();
    if (std::get<Reclamation>(std::move(stop_a)).wait_until(deadline())) bug();
    if (std::get<Reclamation>(std::move(stop_b)).wait_until(deadline())) bug();
    }
    if (engine.shutdown(deadline())) bug();
}
} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    exercise(data, size);
    return 0;
}
