// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#include <libbeam/engine.hpp>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <utility>
using namespace libbeam;
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"CHECK %s:%d: %s\n",__FILE__,__LINE__,#x); std::abort(); } } while(0)
template<class T> static T take(Result<T> result) {
    if(auto* error=std::get_if<Error>(&result)) std::fprintf(stderr,"runtime: %s\n",error->message.c_str());
    CHECK(std::holds_alternative<T>(result)); return std::move(std::get<T>(result));
}
static ByteView view(const Bytes& bytes) { return {bytes.data(),bytes.size()}; }
static Deadline deadline() { return std::chrono::steady_clock::now()+std::chrono::seconds(5); }
static Bytes image(const char* path) {
    std::ifstream file(path,std::ios::binary|std::ios::ate); CHECK(file);
    auto size=file.tellg(); CHECK(size>0 && size<8*1024*1024);
    Bytes bytes(static_cast<std::size_t>(size)); file.seekg(0);
    CHECK(file.read(reinterpret_cast<char*>(bytes.data()),size)); return bytes;
}
static void stop(Isolate& world) {
    auto retirement=take(world.stop()); CHECK(!retirement.wait_until(deadline()));
    auto usage=take(world.resource_usage()); CHECK(usage.closed && usage.memory_current==0);
}
static Bytes identity(Isolate& world,const Bytes& input,bool start) {
    auto call=take(start?world.start("memory_slice","identity",view(input)):world.call("memory_slice","identity",view(input)));
    return take(call.wait_until(deadline()));
}
int main(int argc,char** argv) {
    CHECK(argc==1 || argc==2);
    CHECK(std::holds_alternative<Error>(Engine::create({0,10'000'000})));
    {
        auto engine=take(Engine::create({1,10'000'000}));
        auto refused=engine.create_isolate();
        CHECK(std::holds_alternative<Error>(refused) && std::get<Error>(refused).code==ErrorCode::memory_limit);
        auto invalid=engine.create_isolate({0,{}});
        CHECK(std::holds_alternative<Error>(invalid) && std::get<Error>(invalid).code==ErrorCode::invalid_argument);
        {
            auto raised=take(engine.create_isolate({256u*1024u*1024u,{}}));
            auto usage=take(raised.resource_usage());
            CHECK(usage.limits.memory_limit_bytes==256u*1024u*1024u && usage.memory_current>0);
            CHECK(usage.memory_current<=usage.memory_peak && usage.memory_peak<=usage.limits.memory_limit_bytes);
            stop(raised);
        }
        CHECK(!engine.shutdown(deadline()));
    }
    if(argc==1) { std::puts("API_MEMORY_GATE_OK defaults_overrides=true bootstrap_rollback=true"); return 0; }
    auto code=image(argv[1]); auto engine=take(Engine::create());
    Bytes input{0x08,0,0,0},expected{'g','r','o','w','n'},kept; std::size_t exact_peak,load_peak;
    {
        auto probe=take(engine.create_isolate());
        CHECK(take(probe.resource_usage()).limits.memory_limit_bytes==256u*1024u*1024u);
        CHECK(!probe.load_module(view(code))); load_peak=take(probe.resource_usage()).memory_peak;
        auto growing=take(probe.start("memory_slice","grow",view(input)));
        kept=take(growing.wait_until(deadline())); CHECK(kept==expected);
        auto usage=take(probe.resource_usage()); exact_peak=usage.memory_peak;
        CHECK(exact_peak>load_peak); // Dynamic runtime backing, not loader scratch, sets this boundary.
        stop(probe);
    }
    {
        auto exact=take(engine.create_isolate({exact_peak,{}}));
        auto lower=take(engine.create_isolate({exact_peak-1,{}}));
        auto peer=take(engine.create_isolate());
        CHECK(!exact.load_module(view(code)) && !lower.load_module(view(code)) && !peer.load_module(view(code)));
        auto growing=take(exact.start("memory_slice","grow",view(input)));
        CHECK(take(growing.wait_until(deadline()))==expected);
        CHECK(take(exact.resource_usage()).memory_peak==exact_peak);
        auto call=take(lower.start("memory_slice","grow",view(input)));
        auto result=call.wait_until(deadline());
        CHECK(std::holds_alternative<Error>(result) && std::get<Error>(result).code==ErrorCode::memory_limit);
        auto usage=take(lower.resource_usage());
        CHECK(!usage.closed && usage.memory_peak<=exact_peak-1);
        Bytes small{1,2,3}; CHECK(identity(lower,small,false)==small); // Contained pure invocation reuse.
        CHECK(identity(peer,small,true)==small); // A sibling gets neither denial nor poisoned credit.
        CHECK(kept==expected); // Detached host storage is no longer charged to its reclaimed world.
        stop(exact); stop(lower); stop(peer);
    }
    CHECK(!engine.shutdown(deadline()));
    std::printf("API_MEMORY_OK default_mib=256 exact_peak=%zu load_peak=%zu threshold=true pure_reuse=true peer_survives=true physical_zero=true\n",exact_peak,load_peak);
}
