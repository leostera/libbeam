// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#include <libbeam/engine.hpp>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
using namespace libbeam;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr,"CHECK %d: %s\n",__LINE__,#c); std::abort(); } } while (0)
static ByteView view(const Bytes& bytes) { return {bytes.data(),bytes.size()}; }
static Deadline deadline() { return std::chrono::steady_clock::now()+std::chrono::seconds(5); }
template<class T> static T take(Result<T> result) {
    if (auto error=std::get_if<Error>(&result)) { std::fprintf(stderr,"error %d: %s\n",int(error->code),error->message.c_str()); std::abort(); }
    return std::get<T>(std::move(result));
}
static void error(Result<Bytes> result,ErrorCode expected) { CHECK(std::holds_alternative<Error>(result) && std::get<Error>(result).code==expected); }
static void reply(Call& call,const Bytes& expected) { CHECK(take(call.wait_until(deadline()))==expected); }
static void stop(Isolate& world) {
    auto reclamation=take(world.stop()); CHECK(!reclamation.wait_until(deadline()));
    auto usage=take(world.resource_usage()); CHECK(usage.closed && usage.memory_current==0);
}
int main(int argc,char** argv) {
    if(argc==1) return 0; CHECK(argc==2);
    std::ifstream input(argv[1],std::ios::binary); CHECK(input.good());
    Bytes beam{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
    const Bytes bytes{'p','a','y','l','o','a','d'};
    auto invalid_engine=Engine::create(ResourceLimits{256u*1024u*1024u,0});
    CHECK(std::holds_alternative<Error>(invalid_engine) && std::get<Error>(invalid_engine).code==ErrorCode::invalid_argument);
    {
        auto engine=take(Engine::create());
        {
            auto world=take(engine.create_isolate());
            auto limits=take(world.resource_usage()).limits;
            CHECK(limits.memory_limit_bytes==256u*1024u*1024u && limits.invocation_work_limit==10'000'000);
            CHECK(!world.load_module(view(beam)));
            auto call=take(world.start("work_slice","pure",view(bytes))); reply(call,bytes);
            stop(world);
        }
        CHECK(!engine.shutdown(deadline()));
    }
    auto engine=take(Engine::create(ResourceLimits{256u*1024u*1024u,4096}));
    ResourceOverrides invalid_override; invalid_override.invocation_work_limit=0;
    auto invalid_isolate=engine.create_isolate(invalid_override);
    CHECK(std::holds_alternative<Error>(invalid_isolate) && std::get<Error>(invalid_isolate).code==ErrorCode::invalid_argument);
    {
        auto world=take(engine.create_isolate()); ResourceOverrides overrides; overrides.invocation_work_limit=8192;
        auto peer=take(engine.create_isolate(overrides));
        CHECK(take(world.resource_usage()).limits.invocation_work_limit==4096);
        CHECK(take(peer.resource_usage()).limits.invocation_work_limit==8192);
        CHECK(!world.load_module(view(beam)) && !peer.load_module(view(beam)));
        auto first=take(world.start("work_slice","pure",view(bytes))); reply(first,bytes);
        auto waiting=take(world.call("work_slice","waiting",view(bytes)));
        for(unsigned i=0;i<2;++i) error(waiting.wait_until(std::chrono::steady_clock::now()+std::chrono::milliseconds(2)),ErrorCode::timeout);
        auto limited=take(world.call("work_slice","loop",view(bytes))); error(limited.wait_until(deadline()),ErrorCode::work_limit);
        CHECK(!take(world.resource_usage()).closed); // Another pending family must not trigger the old solitary heuristic.
        auto pure=take(world.call("work_slice","pure",view(bytes))); reply(pure,bytes);
        CHECK(!waiting.cancel()); error(waiting.wait_until(deadline()),ErrorCode::cancelled);
        auto family=take(world.call("work_slice","private_wait",view(bytes))); error(family.wait_until(deadline()),ErrorCode::work_limit);
        CHECK(!take(world.resource_usage()).closed);
        auto peer_call=take(peer.start("work_slice","pure",view(bytes))); reply(peer_call,bytes);
        stop(world); stop(peer);
    }
    CHECK(!engine.shutdown(deadline()));
    std::puts("API_WORK_OK defaults=true overrides=true timeout_no_reset=true private_reuse=true pending_peer_family=true cancellation_distinct=true peer_survives=true physical_zero=true execution=shared_worker");
}
