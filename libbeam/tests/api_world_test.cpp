// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#include <libbeam/engine.hpp>
#include <atomic>
#include <cfenv>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <new>
#include <thread>
#include <utility>
using namespace libbeam;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"CHECK %s:%d: %s\n",__FILE__,__LINE__,#x); std::exit(1); } } while (0)
static std::atomic<bool> fail_next_new{false};
void* operator new(std::size_t size) {
    if (fail_next_new.exchange(false)) throw std::bad_alloc();
    if (auto* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
template<class T> static T take(Result<T> result) {
    if (auto* error = std::get_if<Error>(&result)) std::fprintf(stderr,"runtime: %s\n",error->message.c_str());
    CHECK(std::holds_alternative<T>(result)); return std::move(std::get<T>(result));
}
template<class T> static void error(Result<T> result,ErrorCode code) {
    CHECK(std::holds_alternative<Error>(result) && std::get<Error>(result).code == code);
}
static Deadline deadline() { return std::chrono::steady_clock::now()+std::chrono::seconds(5); }
static ByteView view(const Bytes& b) { return {b.data(),b.size()}; }
static Bytes image(const char* path) {
    std::ifstream file(path,std::ios::binary|std::ios::ate); CHECK(file);
    auto size=file.tellg(); CHECK(size>0 && size<8*1024*1024);
    Bytes result(static_cast<std::size_t>(size)); file.seekg(0);
    CHECK(file.read(reinterpret_cast<char*>(result.data()),size)); return result;
}
static void reclaim(Isolate& isolate) {
    auto stopped=take(isolate.stop()); CHECK(!stopped.wait_until(deadline()));
    auto again=stopped.wait_until(deadline()); CHECK(again && again->code==ErrorCode::closed);
}
int main(int argc,char** argv) {
    CHECK(argc==10); auto code=image(argv[1]), large_code=image(argv[2]), local_code=image(argv[3]), compare_code=image(argv[4]), shape_code=image(argv[5]), add_code=image(argv[6]), decimal_code=image(argv[7]), pid_code=image(argv[8]), fun_code=image(argv[9]);
    auto engine=take(Engine::create());
    Bytes kept;
    {
        auto a=take(engine.create_isolate()), b=take(engine.create_isolate());
        CHECK(!a.load_module(view(code)) && !b.load_module(view(code)));
        CHECK(!a.load_module(view(pid_code)) && !b.load_module(view(pid_code)));
        CHECK(!a.load_module(view(fun_code)) && !b.load_module(view(fun_code)));
        CHECK(!b.load_module(view(large_code)) && !b.load_module(view(local_code)) && !b.load_module(view(compare_code)) && !b.load_module(view(shape_code)) && !b.load_module(view(add_code)) && !b.load_module(view(decimal_code)));
        Bytes input(65,0x31);
        fail_next_new=true; error(a.start("async_slice","identity",view(input)),ErrorCode::limit);
        std::fenv_t environment;
        CHECK(!std::fegetenv(&environment) && !std::fesetround(FE_UPWARD));
        auto first=take(a.start("async_slice","identity",view(input)));
        auto second=take(b.start("async_slice","identity",{nullptr,0}));
        input.assign(input.size(),0);
        CHECK(take(second.wait_until(deadline())).empty());
        fail_next_new=true;
        error(first.wait_until(deadline()),ErrorCode::limit);
        CHECK(!fail_next_new); kept=take(first.wait_until(deadline()));
        CHECK(kept==Bytes(65,0x31)); error(first.wait_until(deadline()),ErrorCode::closed);
        auto own_a=take(a.call("pid_slice","echo",view(kept)));
        auto own_b=take(b.call("pid_slice","echo",view(kept)));
        CHECK(take(own_a.wait_until(deadline()))==kept && take(own_b.wait_until(deadline()))==kept);
        auto fresh_ref_a=take(a.call("pid_slice","reference_echo",view(kept)));
        auto fresh_ref_b=take(b.call("pid_slice","reference_echo",view(kept)));
        CHECK(take(fresh_ref_a.wait_until(deadline()))==kept && take(fresh_ref_b.wait_until(deadline()))==kept);
        for(const char* function : {"echo","nested"}) {
            auto fa=take(a.call("fun_slice",function,view(kept))),fb=take(b.call("fun_slice",function,view(kept)));
            CHECK(take(fa.wait_until(deadline()))==kept && take(fb.wait_until(deadline()))==kept);
        }
        auto closed_fun=take(b.call("fun_slice","closed",view(kept)));
        CHECK(take(closed_fun.wait_until(deadline()))==Bytes({'c','l','o','s','e','d'}));
        auto wrong_arity=take(b.call("fun_slice","badarity",view(kept)));
        error(wrong_arity.wait_until(deadline()),ErrorCode::exception);
        auto escaped_fun=take(b.call("fun_slice","factory",view(kept)));
        error(escaped_fun.wait_until(deadline()),ErrorCode::unsupported); // host protocol remains copied binaries only
        auto not_a_fun=take(b.call("pid_slice","invoke0",view(kept)));
        error(not_a_fun.wait_until(deadline()),ErrorCode::exception);
        for(const char* function : {"identity","gc","tail"}) {
            auto call=take(b.call("local_slice",function,view(kept)));
            CHECK(take(call.wait_until(deadline()))==kept);
        }
        {
            auto empty=take(b.call("compare_slice","empty",{nullptr,0}));
            CHECK(take(empty.wait_until(deadline()))==Bytes({'e','m','p','t','y'}));
            auto nonempty=take(b.call("compare_slice","empty",view(kept)));
            error(nonempty.wait_until(deadline()),ErrorCode::exception);
        }
        for(const char* function : {"tuple_binary","list_binary"}) {
            auto shaped=take(b.call("shape_slice",function,view(kept)));
            CHECK(take(shaped.wait_until(deadline()))==kept);
        }
        for(const char* function : {"copied","rounded"}) {
            auto numeric=take(b.call("add_slice",function,view(kept)));
            CHECK(take(numeric.wait_until(deadline()))==kept);
        }
        auto decimal=take(b.call("decimal_slice","negative",view(kept)));
        const char* negative="-18446744073709551617";
        CHECK(take(decimal.wait_until(deadline()))==Bytes(negative,negative+21));
        auto formatted=take(b.call("decimal_slice","large",view(kept)));
        CHECK(take(formatted.wait_until(deadline())).size()==2467);
        {
            auto formatting_world=take(engine.create_isolate()); CHECK(!formatting_world.load_module(view(decimal_code)));
            auto slow=take(formatting_world.start("decimal_slice","slow",view(kept)));
            auto progress=take(b.call("decimal_slice","small",view(kept)));
            CHECK(take(progress.wait_until(deadline()))==Bytes({'4','2'}));
            error(slow.wait_until(Deadline::min()),ErrorCode::timeout);
            reclaim(formatting_world); error(slow.wait_until(deadline()),ErrorCode::cancelled);
        }
        CHECK(std::fegetround()==FE_UPWARD && !std::fesetenv(&environment));
        for(const char* function : {"command","not_command"}) {
            Bytes next={'n','e','x','t'}, other={'r','e','a','d'};
            auto accepted=take(b.call("compare_slice",function,view(next)));
            CHECK(take(accepted.wait_until(deadline()))==Bytes({'n','e','x','t','-','o','k'}));
            auto rejected=take(b.call("compare_slice",function,view(other)));
            CHECK(take(rejected.wait_until(deadline()))==Bytes({'o','t','h','e','r'}));
        }
        auto loop=take(a.call("async_slice","loop",{nullptr,0}));
        error(loop.wait_until(Deadline::min()),ErrorCode::timeout);
        auto peer=take(b.call("async_slice","identity",view(input)));
        CHECK(take(peer.wait_until(deadline()))==input);
        error(loop.wait_until(Deadline::min()),ErrorCode::timeout); // timeout did not cancel
        auto failed=take(b.call("async_slice","crash",{nullptr,0}));
        error(failed.wait_until(deadline()),ErrorCode::exception);
        error(failed.wait_until(deadline()),ErrorCode::closed);
        auto nonbinary=take(b.call("async_slice","info",{nullptr,0}));
        error(nonbinary.wait_until(deadline()),ErrorCode::unsupported);
        Bytes maximum(65536,0x7d);
        auto maximum_call=take(b.call("async_slice","identity",view(maximum)));
        CHECK(take(maximum_call.wait_until(deadline()))==maximum);
        maximum.push_back(0); error(b.call("async_slice","identity",view(maximum)),ErrorCode::limit);
        auto too_large=take(b.call("code_large","value",{nullptr,0}));
        error(too_large.wait_until(deadline()),ErrorCode::limit);
        std::thread other([&] { error(b.call("async_slice","identity",{nullptr,0}),ErrorCode::invalid_state); });
        other.join();
        fail_next_new=true; error(a.stop(),ErrorCode::limit);
        error(loop.wait_until(Deadline::min()),ErrorCode::timeout);
        reclaim(a); error(loop.wait_until(deadline()),ErrorCode::cancelled);
        error(a.call("async_slice","identity",{nullptr,0}),ErrorCode::closed);
        for(unsigned i=0;i<32;++i) {
            auto replacement=take(engine.create_isolate()); CHECK(!replacement.load_module(view(code)) && !replacement.load_module(view(pid_code)) && !replacement.load_module(view(fun_code)));
            auto call=take(replacement.start("pid_slice","echo",view(input)));
            CHECK(take(call.wait_until(deadline()))==input);
            auto ref=take(replacement.call("pid_slice","reference_echo",view(input)));
            CHECK(take(ref.wait_until(deadline()))==input);
            auto captured=take(replacement.call("fun_slice","nested",view(input)));
            CHECK(take(captured.wait_until(deadline()))==input); reclaim(replacement);
        }
        auto during_stop=take(b.call("fun_slice","echo",view(input)));
        CHECK(take(during_stop.wait_until(deadline()))==input); reclaim(b);
        auto busy=engine.shutdown(deadline()); CHECK(busy && busy->code==ErrorCode::busy);
    }
    {
        auto abandoned=[&] {
            auto world=take(engine.create_isolate()); CHECK(!world.load_module(view(code)));
            return take(world.start("async_slice","loop",{nullptr,0}));
        }();
        error(abandoned.wait_until(deadline()),ErrorCode::cancelled);
    }
    CHECK(!engine.shutdown(deadline()) && kept==Bytes(65,0x31));
    {
        auto survivor=[&] {
            auto parent=take(Engine::create()); auto world=take(parent.create_isolate());
            CHECK(!world.load_module(view(code))); return world;
        }();
        auto call=take(survivor.start("async_slice","identity",view(kept)));
        CHECK(take(call.wait_until(deadline()))==kept); reclaim(survivor);
    }
    std::puts("API_WORLD_EXECUTION_OK real_worker=true copied_io=true copy_failure_retry=true deadlines=true terminal_once=true exceptions=true reclamation=true local_calls_y_gc=true exact_binary_comparison=true tuple_list_calls=true numeric_calls=true decimal_continuations=true owned_self=true ordinary_references=true guest_closures=true repeat=32 stateful_acceptance=false");
    return 0;
}
