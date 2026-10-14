// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#include <libbeam/engine.hpp>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <gtest/gtest.h>
#include <system_error>
#include <termios.h>
#include <unistd.h>

namespace {
int checked(int result) {
    if (result < 0) throw std::system_error(errno, std::generic_category());
    return result;
}
class Descriptor {
public:
    explicit Descriptor(int fd) : fd_(checked(fd)) {}
    ~Descriptor() { close(fd_); }
    Descriptor(const Descriptor&) = delete;
    Descriptor& operator=(const Descriptor&) = delete;
    int get() const { return fd_; }
private:
    int fd_;
};
int open_slave(int master) {
    checked(grantpt(master));
    checked(unlockpt(master));
    const char* name = ptsname(master);
    if (!name) throw std::system_error(errno, std::generic_category());
    return open(name, O_RDWR | O_NOCTTY);
}
class TerminalInput {
public:
    explicit TerminalInput(bool nonblocking)
        : master_(posix_openpt(O_RDWR | O_NOCTTY)), slave_(open_slave(master_.get())),
          original_(dup(STDIN_FILENO)), original_flags_(checked(fcntl(STDIN_FILENO, F_GETFD))) {
        const int flags = checked(fcntl(slave_.get(), F_GETFL));
        checked(fcntl(slave_.get(), F_SETFL, nonblocking ? flags | O_NONBLOCK : flags & ~O_NONBLOCK));
        // Switch only after every fallible setup operation, so failed
        // construction leaves the test runner's stdin untouched.
        checked(dup2(slave_.get(), STDIN_FILENO));
    }
    ~TerminalInput() {
        EXPECT_EQ(dup2(original_.get(), STDIN_FILENO), STDIN_FILENO);
        EXPECT_EQ(fcntl(STDIN_FILENO, F_SETFD, original_flags_), 0);
    }
    TerminalInput(const TerminalInput&) = delete;
    TerminalInput& operator=(const TerminalInput&) = delete;
private:
    Descriptor master_, slave_, original_;
    int original_flags_;
};
struct TerminalState {
    int file_flags;
    int descriptor_flags;
    termios attributes;
};
TerminalState snapshot() {
    TerminalState result{checked(fcntl(STDIN_FILENO, F_GETFL)), checked(fcntl(STDIN_FILENO, F_GETFD)), {}};
    checked(tcgetattr(STDIN_FILENO, &result.attributes));
    return result;
}
bool same_state(const TerminalState& a, const TerminalState& b) {
    const auto& x = a.attributes;
    const auto& y = b.attributes;
    // Compare defined fields, not platform-specific struct padding.
    return a.file_flags == b.file_flags && a.descriptor_flags == b.descriptor_flags &&
        x.c_iflag == y.c_iflag && x.c_oflag == y.c_oflag && x.c_cflag == y.c_cflag && x.c_lflag == y.c_lflag &&
        cfgetispeed(&x) == cfgetispeed(&y) && cfgetospeed(&x) == cfgetospeed(&y) &&
        std::memcmp(x.c_cc, y.c_cc, sizeof(x.c_cc)) == 0;
}
void exercise_engine(const TerminalState& before) {
    auto created = libbeam::Engine::create();
    ASSERT_TRUE(std::holds_alternative<libbeam::Engine>(created));
    auto engine = std::move(std::get<libbeam::Engine>(created));
    EXPECT_TRUE(same_state(before, snapshot()));
    {
        auto isolated = engine.create_isolate();
        ASSERT_TRUE(std::holds_alternative<libbeam::Isolate>(isolated));
        auto isolate = std::move(std::get<libbeam::Isolate>(isolated));
        auto stopped = isolate.stop();
        ASSERT_TRUE(std::holds_alternative<libbeam::Reclamation>(stopped));
        EXPECT_FALSE(std::get<libbeam::Reclamation>(stopped).wait_until(std::chrono::steady_clock::now() + std::chrono::seconds(5)));
    }
    EXPECT_FALSE(engine.shutdown(std::chrono::steady_clock::now() + std::chrono::seconds(5)));
    EXPECT_TRUE(same_state(before, snapshot()));
}
class HostTerminal : public testing::TestWithParam<bool> {};
TEST_P(HostTerminal, EngineAndIsolateLifetimePreserveStdin) {
    TerminalInput terminal(GetParam());
    const auto before = snapshot();
    exercise_engine(before);
    exercise_engine(before);
}
INSTANTIATE_TEST_SUITE_P(BlockingModes, HostTerminal, testing::Bool());

TEST(TerminalObserver, DetectsNonblockingReset) {
    TerminalInput terminal(true);
    const auto before = snapshot();
    checked(fcntl(STDIN_FILENO, F_SETFL, before.file_flags & ~O_NONBLOCK));
    EXPECT_FALSE(same_state(before, snapshot()));
}
TEST(TerminalObserver, DetectsTermiosMutation) {
    TerminalInput terminal(false);
    const auto before = snapshot();
    auto changed = before.attributes;
    changed.c_lflag ^= ECHO;
    checked(tcsetattr(STDIN_FILENO, TCSANOW, &changed));
    EXPECT_FALSE(same_state(before, snapshot()));
}
}  // namespace
