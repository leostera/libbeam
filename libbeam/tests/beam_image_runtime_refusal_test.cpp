// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#include "libbeam/engine.hpp"
#include "test_process.hpp"
#include <gtest/gtest.h>
#include <chrono>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <variant>

namespace {
using namespace std::chrono_literals;
using libbeam::ByteView;
using libbeam::Bytes;
using libbeam::Call;
using libbeam::Error;
using libbeam::ErrorCode;
using libbeam::Engine;
using libbeam::Isolate;

std::string read_file(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot read " + path);
    return std::string(std::istreambuf_iterator<char>{input}, {});
}

ByteView view(const std::string& bytes) {
    return {reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()};
}

Isolate take_isolate(libbeam::Result<Isolate> result) {
    if (auto* error = std::get_if<Error>(&result))
        throw std::runtime_error("isolate creation failed: " + error->message);
    return std::move(std::get<Isolate>(result));
}

libbeam::Reclamation take_reclamation(libbeam::Result<libbeam::Reclamation> result) {
    if (auto* error = std::get_if<Error>(&result))
        throw std::runtime_error("isolate stop failed: " + error->message);
    return std::move(std::get<libbeam::Reclamation>(result));
}

Call take_call(libbeam::Result<Call> result) {
    if (auto* error = std::get_if<Error>(&result))
        throw std::runtime_error("call start failed: " + error->message);
    return std::move(std::get<Call>(result));
}

Bytes take_bytes(libbeam::Result<Bytes> result) {
    if (auto* error = std::get_if<Error>(&result))
        throw std::runtime_error("call failed: " + error->message);
    return std::move(std::get<Bytes>(result));
}
}  // namespace

TEST(BeamImageRuntimeRefusal, InvalidGuestPcCannotAbortSharedWorker) {
    const auto malformed = read_file(libbeam_test::runfile(
        "libbeam/tests/fuzz_corpus/beam_image/runtime_invalid_pc.beam"));
    const auto clean = read_file(libbeam_test::runfile(
        "libbeam/fixtures/probe_b_fixture/probe.beam"));

    auto engine = Engine::create();
    ASSERT_TRUE(std::holds_alternative<Engine>(engine));
    auto owner = std::move(std::get<Engine>(engine));
    {
        auto bad_world = take_isolate(owner.create_isolate());
        auto good_world = take_isolate(owner.create_isolate());
        ASSERT_FALSE(bad_world.load_module(view(malformed)));
        ASSERT_FALSE(good_world.load_module(view(clean)));

        auto bad_call = take_call(bad_world.start("probe", "boot", {nullptr, 0}));
        auto good_call = take_call(good_world.start("probe", "boot", {nullptr, 0}));

        auto bad_result = bad_call.wait_until(std::chrono::steady_clock::now() + 10s);
        ASSERT_TRUE(std::holds_alternative<Error>(bad_result))
            << "mutated code unexpectedly returned successfully";
        const auto bad_code = std::get<Error>(bad_result).code;
        EXPECT_TRUE(bad_code == ErrorCode::unsupported || bad_code == ErrorCode::exception)
            << "unexpected malformed-code result: " << std::get<Error>(bad_result).message;

        const auto good_result = take_bytes(
            good_call.wait_until(std::chrono::steady_clock::now() + 10s));
        EXPECT_EQ(std::string(good_result.begin(), good_result.end()), "B:ready");

        auto bad_reclamation = take_reclamation(bad_world.stop());
        auto good_reclamation = take_reclamation(good_world.stop());
        ASSERT_FALSE(bad_reclamation.wait_until(std::chrono::steady_clock::now() + 10s));
        ASSERT_FALSE(good_reclamation.wait_until(std::chrono::steady_clock::now() + 10s));
    }
    auto shutdown_status = owner.shutdown(std::chrono::steady_clock::now() + 10s);
    ASSERT_FALSE(shutdown_status.has_value()) << shutdown_status->message;
}
