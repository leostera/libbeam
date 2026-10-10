// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#include <libbeam/engine.hpp>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <type_traits>
#include <utility>

static_assert(!std::is_copy_constructible_v<libbeam::Engine>);
static_assert(!std::is_copy_constructible_v<libbeam::Isolate>);
static_assert(!std::is_copy_constructible_v<libbeam::Call>);
static_assert(!std::is_copy_constructible_v<libbeam::Reclamation>);
static_assert(std::is_nothrow_move_constructible_v<libbeam::Engine>);
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"CHECK %s:%d: %s\n",__FILE__,__LINE__,#x); std::exit(1); } } while (0)

// Exercise the real adapter allocation, not an alternate factory. C allocation
// prefixes and physical resource accounting are covered by core_engine_test.
static bool fail_next_new = false;
static std::size_t allocations = 0;
void* operator new(std::size_t size) {
    if (fail_next_new) { fail_next_new = false; throw std::bad_alloc(); }
    if (auto* pointer = std::malloc(size ? size : 1)) { ++allocations; return pointer; }
    throw std::bad_alloc();
}
void operator delete(void* pointer) noexcept {
    if (pointer) { --allocations; std::free(pointer); }
}
void operator delete(void* pointer, std::size_t) noexcept { ::operator delete(pointer); }

static libbeam::Engine create() {
    auto result = libbeam::Engine::create();
    CHECK(std::holds_alternative<libbeam::Engine>(result));
    return std::move(std::get<libbeam::Engine>(result));
}
static libbeam::Deadline deadline() {
    return std::chrono::steady_clock::now() + std::chrono::seconds(5);
}
int main() {
    using namespace libbeam;
    const auto before = allocations;
    {
        fail_next_new = true;
        auto failed = Engine::create();
        CHECK(!fail_next_new && std::holds_alternative<Error>(failed));
        CHECK(std::get<Error>(failed).code == ErrorCode::limit);
    }
    CHECK(allocations == before);
    for (unsigned i = 0; i < 32; ++i) {
        auto original = create();
        auto engine = std::move(original);
        auto moved = original.shutdown(deadline());
        CHECK(moved && moved->code == ErrorCode::invalid_state);
        {
            fail_next_new = true;
            auto failed_world = engine.create_isolate();
            CHECK(std::holds_alternative<Error>(failed_world) && std::get<Error>(failed_world).code == ErrorCode::limit);
            auto created = engine.create_isolate(); CHECK(std::holds_alternative<Isolate>(created));
            auto& world = std::get<Isolate>(created);
            auto busy = engine.shutdown(deadline()); CHECK(busy && busy->code == ErrorCode::busy);
            auto stop = world.stop(); CHECK(std::holds_alternative<Reclamation>(stop));
            CHECK(!std::get<Reclamation>(stop).wait_until(deadline()));
            busy = engine.shutdown(deadline()); CHECK(busy && busy->code == ErrorCode::busy);
        }
        auto expired = engine.shutdown(Deadline::min());
        CHECK(expired && expired->code == ErrorCode::timeout);
        CHECK(!engine.shutdown(deadline()));
        auto closed = engine.shutdown(deadline());
        CHECK(closed && closed->code == ErrorCode::closed);
        auto refused = engine.create_isolate();
        CHECK(std::holds_alternative<Error>(refused) && std::get<Error>(refused).code == ErrorCode::closed);
        // A closed handle is finite control state, not an initialization claim.
        auto replacement = create();
        CHECK(!replacement.shutdown(deadline()));
    }
    {
        auto survivor = create();
        { auto dropped = create(); } // RAII cleanup without an explicit shutdown
        CHECK(!survivor.shutdown(deadline()));
    }
    CHECK(allocations == before);
    std::puts("API_ENGINE_OK create_shutdown_create=true allocation_failure_retry=true moved_handles=true deadlines=true empty_isolate_lifetime=true execution=false");
    return 0;
}
