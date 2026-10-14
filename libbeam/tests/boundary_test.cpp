// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#include <libbeam/engine.hpp>
#include "test_directory.hpp"
#include "test_process.hpp"
#include <cstdio>
#include <dlfcn.h>
#include <fstream>
#include <gtest/gtest.h>
#include <iterator>
#include <optional>
#include <unistd.h>

namespace {
using namespace libbeam;
std::string fixture_path;
std::string erl_path;
std::string library_path;
Deadline deadline() { return std::chrono::steady_clock::now() + std::chrono::seconds(10); }
ByteView view(std::string_view bytes) { return {reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()}; }
template<class T> T take(Result<T> result) {
    if (const auto* error = std::get_if<Error>(&result)) throw std::runtime_error(error->message);
    return std::get<T>(std::move(result));
}

// Capture only the guest execution window, restoring host descriptors even on
// an exception. Assertions run after restoration so failures remain visible.
class Capture {
public:
    explicit Capture(FILE* stream): stream_(stream), temporary_(std::tmpfile()) {
        if (!temporary_) throw std::runtime_error("Cannot create output capture");
        std::fflush(stream_);
        saved_ = dup(fileno(stream_));
        if (saved_ < 0 || dup2(fileno(temporary_), fileno(stream_)) < 0) {
            if (saved_ >= 0) close(saved_);
            std::fclose(temporary_);
            throw std::runtime_error("Cannot redirect output");
        }
    }
    ~Capture() { restore(); std::fclose(temporary_); }
    Capture(const Capture&) = delete;
    Capture& operator=(const Capture&) = delete;
    std::string finish() {
        restore();
        std::rewind(temporary_);
        std::string bytes;
        for (int value; (value = std::fgetc(temporary_)) != EOF;) bytes.push_back(static_cast<char>(value));
        if (std::ferror(temporary_)) throw std::runtime_error("Cannot read output capture");
        return bytes;
    }
private:
    void restore() {
        if (saved_ < 0) return;
        std::fflush(stream_);
        if (dup2(saved_, fileno(stream_)) < 0) std::abort();
        close(saved_);
        saved_ = -1;
    }
    FILE* stream_;
    FILE* temporary_;
    int saved_ = -1;
};

TEST(ExceptionReference, OrdinaryOtpCatchAfterAndReraiseAgree) {
    // Never run capability/resource probes on OTP. This is an independent
    // language reference, not an application execution backend.
    const auto result = libbeam_test::run(erl_path, {
        "-noshell", "-pa", std::filesystem::path(fixture_path).parent_path().string(),
        "-eval", "io:format(\"~s~n~s~n~s~n\", [boundary_slice:ordinary(<<\"opaque\">>), boundary_slice:nested_after(<<>>), boundary_slice:reraised(<<\"reason\">>)]), halt()."
    });
    ASSERT_TRUE(result.exited(0)) << result.output;
    EXPECT_EQ(result.standard_output, "ok\nok\nok\n");
}
TEST(HostCapture, DetectsBothStreamsAndRestoresDescriptors) {
    std::string out, err;
    {
        Capture stdout_capture(stdout), stderr_capture(stderr);
        std::fputs("stdout control", stdout);
        std::fputs("stderr control", stderr);
        out = stdout_capture.finish();
        err = stderr_capture.finish();
    }
    EXPECT_EQ(out, "stdout control");
    EXPECT_EQ(err, "stderr control");
}

class LibraryMarker {
public:
    explicit LibraryMarker(const std::filesystem::path& path) {
        if (const char* old = std::getenv(name)) previous_ = old;
        if (setenv(name, path.c_str(), 1)) throw std::runtime_error("Cannot set library witness path");
    }
    ~LibraryMarker() {
        const int status = previous_ ? setenv(name, previous_->c_str(), 1) : unsetenv(name);
        if (status) std::abort();
    }
    LibraryMarker(const LibraryMarker&) = delete;
    LibraryMarker& operator=(const LibraryMarker&) = delete;
private:
    static constexpr const char* name = "LIBBEAM_BOUNDARY_LIBRARY_MARKER";
    std::optional<std::string> previous_;
};
TEST(HostLibraryProbe, ConstructorWitnessDetectsRealDlopen) {
    libbeam_test::TestDirectory directory;
    const auto path = directory.root() / "library-marker";
    LibraryMarker marker(path);
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
        ASSERT_FALSE(std::filesystem::exists(path));
        void* handle = dlopen(library_path.c_str(), RTLD_NOW | RTLD_LOCAL);
        ASSERT_NE(handle, nullptr) << dlerror();
        EXPECT_TRUE(std::filesystem::exists(path));
        EXPECT_EQ(dlclose(handle), 0);
        ASSERT_TRUE(std::filesystem::remove(path));
    }
}

class Boundary : public testing::Test {
protected:
    virtual ResourceOverrides limits() const { return {}; }
    void SetUp() override {
        std::ifstream file(fixture_path, std::ios::binary);
        ASSERT_TRUE(file.good());
        const Bytes beam{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        engine.emplace(take(Engine::create()));
        isolate.emplace(take(engine->create_isolate(limits())));
        peer.emplace(take(engine->create_isolate()));
        for (auto* target : {&*isolate, &*peer}) {
            const auto loaded = target->load_module({beam.data(), beam.size()});
            ASSERT_FALSE(loaded) << (loaded ? loaded->message : "");
        }
        auto setup = take(isolate->start("boundary_slice", "setup", {nullptr, 0}));
        EXPECT_EQ(take(setup.wait_until(deadline())), (Bytes{'r','e','a','d','y'}));
        auto other = take(peer->start("boundary_slice", "identity", view("peer")));
        EXPECT_EQ(take(other.wait_until(deadline())), (Bytes{'p','e','e','r'}));
    }
    Result<Bytes> invoke(std::string_view function, std::string_view bytes = {}) {
        auto call = take(isolate->call("boundary_slice", function, view(bytes)));
        auto result = call.wait_until(deadline());
        const auto consumed = call.wait_until(deadline());
        EXPECT_TRUE(std::holds_alternative<Error>(consumed));
        if (const auto* error = std::get_if<Error>(&consumed)) EXPECT_EQ(error->code, ErrorCode::closed);
        return result;
    }
    void success(std::string_view function, std::string_view expected, std::string_view bytes = {}) {
        const auto result = take(invoke(function, bytes));
        EXPECT_EQ(std::string(result.begin(), result.end()), expected);
    }
    void failure(std::string_view function, ErrorCode code, std::string_view message, std::string_view bytes = {}) {
        const auto result = invoke(function, bytes);
        const auto* error = std::get_if<Error>(&result);
        ASSERT_NE(error, nullptr) << "guest catch/rescue masked the host failure";
        EXPECT_EQ(error->code, code);
        EXPECT_EQ(error->message, message);
        EXPECT_FALSE(take(isolate->resource_usage()).closed);
        success("after_count", "clean");
        success("identity", "reused", "reused");
        auto other = take(peer->call("boundary_slice", "identity", view("peer survived")));
        EXPECT_EQ(take(other.wait_until(deadline())), (Bytes{'p','e','e','r',' ','s','u','r','v','i','v','e','d'}));
    }
    static void reclaim(Isolate& target) {
        auto receipt = take(target.stop());
        EXPECT_FALSE(receipt.wait_until(deadline()));
        const auto usage = take(target.resource_usage());
        EXPECT_TRUE(usage.closed);
        EXPECT_EQ(usage.memory_current, 0u);
    }
    void TearDown() override {
        if (peer) { reclaim(*peer); peer.reset(); }
        if (isolate) { reclaim(*isolate); isolate.reset(); }
        if (engine) EXPECT_FALSE(engine->shutdown(deadline()));
    }
    std::optional<Engine> engine;
    std::optional<Isolate> isolate, peer;
};
TEST_F(Boundary, ErrorThrowAndExitAreOrdinaryCatchableGuestExceptions) { success("ordinary", "ok", "opaque"); }
TEST_F(Boundary, UncaughtGuestExceptionHasDistinctCauseAndAllowsReuse) {
    failure("uncaught", ErrorCode::exception, "guest exception", "ordinary reason");
}
TEST_F(Boundary, NestedUnwindRunsAfterInOrder) { success("nested_after", "ok"); }
TEST_F(Boundary, ReraisePreservesOriginalFrameAndReason) { success("reraised", "ok", "opaque reason"); }
TEST_F(Boundary, AfterObserverDetectsRealGuestEffects) {
    success("after_count", "clean");
    success("allowed_after", "control", "control");
    success("after_count", "after-ran");
}
TEST_F(Boundary, CatchCannotMaskHostDenial) {
    failure("denied_catch", ErrorCode::capability_denied, "erlang:load_nif/2 denied: NATIVE_LIBRARY");
}
TEST_F(Boundary, TryCannotMaskHostDenialOrExecuteAfter) {
    failure("denied_try", ErrorCode::capability_denied, "erlang:load_nif/2 denied: NATIVE_LIBRARY");
}
TEST_F(Boundary, DescendantDenialCannotBecomeAnOrdinaryDownMessage) {
    failure("denied_child", ErrorCode::capability_denied, "erlang:load_nif/2 denied: NATIVE_LIBRARY");
}
TEST_F(Boundary, HaltCannotTerminateHostOrBecomeGuestSuccess) {
    failure("denied_halt", ErrorCode::capability_denied, "erlang:halt/1 denied: HOST_CONTROL");
}
TEST_F(Boundary, DisplayCannotWriteToHostStreams) {
    Result<Bytes> result;
    std::string out, err;
    {
        Capture stdout_capture(stdout), stderr_capture(stderr);
        auto call = take(isolate->call("boundary_slice", "denied_output", {nullptr, 0}));
        result = call.wait_until(deadline());
        out = stdout_capture.finish();
        err = stderr_capture.finish();
    }
    EXPECT_TRUE(out.empty()) << out;
    EXPECT_TRUE(err.empty()) << err;
    const auto* error = std::get_if<Error>(&result);
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->code, ErrorCode::capability_denied);
    EXPECT_EQ(error->message, "erlang:display/1 denied: HOST_OUTPUT");
    success("identity", "reused", "reused");
}
TEST_F(Boundary, PortCannotCreateHostMarker) {
    libbeam_test::TestDirectory directory;
    const auto path = directory.write("forbidden-marker", "positive control");
    ASSERT_TRUE(std::filesystem::exists(path));
    ASSERT_TRUE(std::filesystem::remove(path));
    std::string quoted = "'";
    for (char c : path.string()) quoted += c == '\'' ? "'\\''" : std::string(1, c);
    quoted += "'";
    failure("denied_port", ErrorCode::capability_denied, "erlang:open_port/2 denied: HOST_PORT", "touch " + quoted);
    EXPECT_FALSE(std::filesystem::exists(path));
}
TEST_F(Boundary, DeniedLibraryCannotRunNativeConstructor) {
    libbeam_test::TestDirectory directory;
    const auto path = directory.root() / "forbidden-library-marker";
    LibraryMarker marker(path);
    // load_nif appends the platform suffix; the control above proves that this
    // exact shared object really performs an observable effect if dlopen runs.
    const auto stem = std::filesystem::path(library_path).replace_extension().string();
    failure("denied_library", ErrorCode::capability_denied, "erlang:load_nif/2 denied: NATIVE_LIBRARY", stem);
    EXPECT_FALSE(std::filesystem::exists(path));
}
class WorkBoundary : public Boundary {
    ResourceOverrides limits() const override { return {std::nullopt, 20000}; }
};
TEST_F(WorkBoundary, PrivateWorkExhaustionIsNotCatchableAndAfterDoesNotRun) {
    failure("work_catch", ErrorCode::work_limit, "invocation work limit exceeded");
}
TEST_F(WorkBoundary, DescendantWorkIsChargedToWaitingCaller) {
    failure("child_work", ErrorCode::work_limit, "invocation work limit exceeded");
}
class MemoryBoundary : public Boundary {
    ResourceOverrides limits() const override { return {8u * 1024u * 1024u, std::nullopt}; }
};
TEST_F(MemoryBoundary, AccountedMemoryExhaustionIsDistinctAndNotCatchable) {
    const std::string count("\x83\x62\x00\x0f\x42\x40", 6); // ETF integer 1,000,000
    failure("memory_catch", ErrorCode::memory_limit, "Isolate accounted memory limit exceeded", count);
}
} // namespace
int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    if (argc != 4) return 2;
    fixture_path = libbeam_test::runfile(argv[1]);
    erl_path = libbeam_test::runfile(argv[2]);
    library_path = libbeam_test::runfile(argv[3]);
    return RUN_ALL_TESTS();
}
