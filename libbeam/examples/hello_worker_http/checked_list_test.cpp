// SPDX-License-Identifier: Apache-2.0
// A deliberately unguarded BEAM tests the checked yxx native destructure.
#include <libbeam/engine.hpp>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

namespace {
libbeam::Deadline deadline() { return std::chrono::steady_clock::now() + std::chrono::seconds(10); }
template<class T> T take(libbeam::Result<T> result) {
    if (auto* error = std::get_if<libbeam::Error>(&result)) throw std::runtime_error(error->message);
    return std::get<T>(std::move(result));
}
void require_ok(libbeam::Status result) { if (result) throw std::runtime_error(result->message); }
libbeam::Bytes read_fixture(const char* path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Cannot open checked-list fixture");
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
libbeam::ByteView view(const std::string& value) {
    return {reinterpret_cast<const std::uint8_t*>(value.data()), value.size()};
}
}

int main(int argc, char** argv) {
    if (argc != 2) { std::cerr << "Expected a checked-list BEAM fixture\n"; return 2; }
    std::optional<libbeam::Engine> engine;
    std::optional<libbeam::Isolate> isolate;
    bool okay = false;
    try {
        auto beam = read_fixture(argv[1]);
        if (beam.empty()) throw std::runtime_error("Empty checked-list fixture");
        engine.emplace(take(libbeam::Engine::create()));
        isolate.emplace(take(engine->create_isolate()));
        require_ok(isolate->load_modules({{beam.data(), beam.size()}}));
        auto first = take(isolate->start("checked_list_slice", "run", view("A")));
        if (take(first.wait_until(deadline())) != libbeam::Bytes{0x41, 0x31, 0})
            throw std::runtime_error("positive list result differs from ordinary OTP");
        auto second = take(isolate->call("checked_list_slice", "run", view("AB")));
        if (take(second.wait_until(deadline())) != libbeam::Bytes{0x41, 0x32, 1})
            throw std::runtime_error("positive list tail differs from ordinary OTP");
        auto empty = take(isolate->call("checked_list_slice", "run", view("")));
        auto invalid = empty.wait_until(deadline());
        const auto* error = std::get_if<libbeam::Error>(&invalid);
        if (!error || error->code != libbeam::ErrorCode::exception)
            throw std::runtime_error("non-cons input did not raise a bounded guest exception");
        okay = true;
    } catch (const std::exception& error) {
        std::cerr << "checked list test failed: " << error.what() << '\n';
    }
    if (isolate) {
        try {
            auto receipt = take(isolate->stop());
            require_ok(receipt.wait_until(deadline()));
            auto usage = take(isolate->resource_usage());
            if (!usage.closed || usage.memory_current != 0)
                throw std::runtime_error("Isolate did not reclaim memory");
        } catch (const std::exception& error) {
            std::cerr << "checked list teardown failed: " << error.what() << '\n';
            okay = false;
        }
        isolate.reset();
    }
    if (engine) {
        if (auto error = engine->shutdown(deadline())) {
            std::cerr << "checked list engine shutdown failed: " << error->message << '\n';
            okay = false;
        }
        engine.reset();
    }
    if (okay) std::cout << "CHECKED_LIST_OK positive=otp_bytes negative=guest_exception reclaimed=true\n";
    return okay ? 0 : 1;
}
