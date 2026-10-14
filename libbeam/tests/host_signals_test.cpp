// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#include <libbeam/engine.hpp>
#include "test_process.hpp"
#include <array>
#include <cfenv>
#include <fstream>
#include <iterator>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <gtest/gtest.h>
#include <gtest/gtest-spi.h>
#include <pthread.h>
#include <system_error>
#include <vector>

namespace {
std::string fixture;
struct FloatingState { int round, flags; };
FloatingState floating_snapshot() { return {std::fegetround(), std::fetestexcept(FE_ALL_EXCEPT)}; }
void expect_same_floating(FloatingState expected) {
    const auto actual = floating_snapshot();
    EXPECT_EQ(expected.round, actual.round);
    EXPECT_EQ(expected.flags, actual.flags);
}
class HostFloatingConfiguration {
public:
    HostFloatingConfiguration() {
        if (std::fegetenv(&original_)) throw std::runtime_error("cannot read floating environment");
        if (std::fesetenv(FE_DFL_ENV) || std::fesetround(FE_UPWARD) ||
            std::feraiseexcept(FE_INVALID | FE_DIVBYZERO)) {
            if (std::fesetenv(&original_)) std::abort();
            throw std::runtime_error("cannot configure floating environment");
        }
    }
    ~HostFloatingConfiguration() { EXPECT_EQ(std::fesetenv(&original_), 0); }
    HostFloatingConfiguration(const HostFloatingConfiguration&) = delete;
    HostFloatingConfiguration& operator=(const HostFloatingConfiguration&) = delete;
private:
    std::fenv_t original_;
};
constexpr std::array<int, 9> signals{SIGINT, SIGQUIT, SIGTERM, SIGUSR1, SIGUSR2, SIGTSTP, SIGPIPE, SIGCHLD, SIGFPE};
volatile sig_atomic_t delivered = 0;
void handler(int signal) { if (signal == SIGUSR1) delivered = delivered + 1; }
void syscall_ok(int result) {
    if (result < 0) throw std::system_error(errno, std::generic_category());
}
void pthread_ok(int error) {
    if (error) throw std::system_error(error, std::generic_category());
}
struct SignalState {
    std::array<struct sigaction, signals.size()> actions{};
    sigset_t mask{};
    stack_t stack{};
};
SignalState snapshot() {
    SignalState state;
    for (size_t i = 0; i < signals.size(); ++i) syscall_ok(sigaction(signals[i], nullptr, &state.actions[i]));
    pthread_ok(pthread_sigmask(SIG_SETMASK, nullptr, &state.mask));
    syscall_ok(sigaltstack(nullptr, &state.stack));
    return state;
}
void expect_same_mask(const sigset_t& expected, const sigset_t& actual) {
    for (int signal = 1; signal < NSIG; ++signal)
        EXPECT_EQ(sigismember(&expected, signal), sigismember(&actual, signal)) << "signal " << signal;
}
void expect_same_state(const SignalState& before) {
    const auto after = snapshot();
    for (size_t i = 0; i < signals.size(); ++i) {
        SCOPED_TRACE(signals[i]);
        EXPECT_EQ(before.actions[i].sa_handler, after.actions[i].sa_handler);
        EXPECT_EQ(before.actions[i].sa_flags, after.actions[i].sa_flags);
        expect_same_mask(before.actions[i].sa_mask, after.actions[i].sa_mask);
    }
    expect_same_mask(before.mask, after.mask);
    EXPECT_EQ(before.stack.ss_flags, after.stack.ss_flags);
    if (!(before.stack.ss_flags & SS_DISABLE)) {
        EXPECT_EQ(before.stack.ss_sp, after.stack.ss_sp);
        EXPECT_EQ(before.stack.ss_size, after.stack.ss_size);
    }
}
class HostSignalConfiguration {
public:
    HostSignalConfiguration() : stack_memory_(static_cast<size_t>(SIGSTKSZ) * 2), original_(snapshot()) {
        try {
            struct sigaction action{};
            action.sa_handler = handler;
            syscall_ok(sigemptyset(&action.sa_mask));
            syscall_ok(sigaddset(&action.sa_mask, SIGUSR2));
            action.sa_flags = SA_RESTART | SA_ONSTACK;
            for (const int signal : signals) {
                syscall_ok(sigaction(signal, &action, nullptr));
                ++installed_;
            }
            stack_t stack{};
            stack.ss_sp = stack_memory_.data();
            stack.ss_size = stack_memory_.size();
            syscall_ok(sigaltstack(&stack, nullptr));
            stack_installed_ = true;
            auto mask = original_.mask;
            syscall_ok(sigaddset(&mask, SIGUSR2));
            syscall_ok(sigdelset(&mask, SIGUSR1));
            pthread_ok(pthread_sigmask(SIG_SETMASK, &mask, nullptr));
            mask_installed_ = true;
        } catch (...) {
            restore();
            throw;
        }
    }
    ~HostSignalConfiguration() { restore(); }
    HostSignalConfiguration(const HostSignalConfiguration&) = delete;
    HostSignalConfiguration& operator=(const HostSignalConfiguration&) = delete;
private:
    void restore() {
        // Restore handlers/stack before unblocking the caller's original mask.
        while (installed_) {
            --installed_;
            EXPECT_EQ(sigaction(signals[installed_], &original_.actions[installed_], nullptr), 0);
        }
        if (stack_installed_) {
            auto restored = original_.stack;
            if (restored.ss_flags & SS_DISABLE) {
                // Darwin checks minimum size even for SS_DISABLE. Address and
                // size are immaterial once disabled; supply valid live storage.
                restored.ss_sp = stack_memory_.data();
                restored.ss_size = stack_memory_.size();
            }
            if (sigaltstack(&restored, nullptr) != 0) {
                ADD_FAILURE() << "Cannot restore alternate stack: " << errno;
                std::abort(); // Never free storage still registered as a stack.
            }
            stack_installed_ = false;
        }
        if (mask_installed_) {
            EXPECT_EQ(pthread_sigmask(SIG_SETMASK, &original_.mask, nullptr), 0);
            mask_installed_ = false;
        }
    }
    // Registered alternate-stack storage must outlive restoration, including
    // early assertion returns and exceptions during partial setup.
    std::vector<unsigned char> stack_memory_;
    SignalState original_;
    size_t installed_ = 0;
    bool stack_installed_ = false;
    bool mask_installed_ = false;
};
libbeam::Deadline deadline() { return std::chrono::steady_clock::now() + std::chrono::seconds(5); }
void expect_usr1_delivery(sig_atomic_t expected) {
    ASSERT_EQ(pthread_kill(pthread_self(), SIGUSR1), 0);
    EXPECT_EQ(delivered, expected);
}
TEST(HostSignals, ObserverDetectsSignalMaskMutation) {
    HostSignalConfiguration configuration;
    const auto before = snapshot();
    auto changed = before.mask;
    syscall_ok(sigdelset(&changed, SIGUSR2));
    pthread_ok(pthread_sigmask(SIG_SETMASK, &changed, nullptr));
    EXPECT_NONFATAL_FAILURE(expect_same_state(before), "signal");
    pthread_ok(pthread_sigmask(SIG_SETMASK, &before.mask, nullptr));
    expect_same_state(before);
}
TEST(HostSignals, ObserverDetectsFloatingRoundingMutation) {
    HostFloatingConfiguration configuration;
    const auto before = floating_snapshot();
    ASSERT_EQ(std::fesetround(FE_DOWNWARD), 0);
    EXPECT_NONFATAL_FAILURE(expect_same_floating(before), "round");
    ASSERT_EQ(std::fesetround(before.round), 0);
    expect_same_floating(before);
}
TEST(HostSignals, PublicEngineAndIsolatesPreserveHostState) {
    std::ifstream file(fixture, std::ios::binary);
    ASSERT_TRUE(file.good());
    const libbeam::Bytes beam{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    const auto original = snapshot();
    {
        HostSignalConfiguration configuration;
        HostFloatingConfiguration floating_configuration;
        const auto floating_before = floating_snapshot();
        const auto before = snapshot();
        delivered = 0;
        for (unsigned iteration = 0; iteration < 3; ++iteration) {
            SCOPED_TRACE(iteration);
            auto created = libbeam::Engine::create();
            ASSERT_TRUE(std::holds_alternative<libbeam::Engine>(created));
            auto engine = std::move(std::get<libbeam::Engine>(created));
            expect_same_state(before);
            expect_same_floating(floating_before);
            expect_usr1_delivery(static_cast<sig_atomic_t>(iteration * 2 + 1));
            {
                auto created_isolate = engine.create_isolate();
                ASSERT_TRUE(std::holds_alternative<libbeam::Isolate>(created_isolate));
                auto isolate = std::move(std::get<libbeam::Isolate>(created_isolate));
                expect_same_state(before);
                ASSERT_FALSE(isolate.load_module({beam.data(), beam.size()}));
                const libbeam::Bytes payload{0, 255, 42};
                auto started = isolate.start("add_slice", "rounded", {payload.data(), payload.size()});
                ASSERT_TRUE(std::holds_alternative<libbeam::Call>(started));
                auto call = std::move(std::get<libbeam::Call>(started));
                auto reply = call.wait_until(deadline());
                ASSERT_TRUE(std::holds_alternative<libbeam::Bytes>(reply));
                EXPECT_EQ(std::get<libbeam::Bytes>(reply), payload);
                expect_same_state(before);
                expect_same_floating(floating_before);
                auto stopped = isolate.stop();
                ASSERT_TRUE(std::holds_alternative<libbeam::Reclamation>(stopped));
                EXPECT_FALSE(std::get<libbeam::Reclamation>(stopped).wait_until(deadline()));
            }
            EXPECT_FALSE(engine.shutdown(deadline()));
            expect_same_state(before);
            expect_same_floating(floating_before);
            expect_usr1_delivery(static_cast<sig_atomic_t>(iteration * 2 + 2));
        }
    }
    expect_same_state(original);
}
}  // namespace
int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    if (argc != 2) return 2;
    fixture = libbeam_test::runfile(argv[1]);
    return RUN_ALL_TESTS();
}
