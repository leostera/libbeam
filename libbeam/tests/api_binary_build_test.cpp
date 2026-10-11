// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#include <libbeam/engine.hpp>
#include <cstdio>
#include <cstdlib>
#include <fstream>
using namespace libbeam;
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"CHECK %s:%d: %s\n",__FILE__,__LINE__,#x); std::exit(1); } } while(0)
template<class T> static T take(Result<T> result) {
    if(auto* e=std::get_if<Error>(&result)) std::fprintf(stderr,"runtime: %s\n",e->message.c_str());
    CHECK(std::holds_alternative<T>(result)); return std::move(std::get<T>(result));
}
static Deadline deadline() { return std::chrono::steady_clock::now()+std::chrono::seconds(5); }
static ByteView view(const Bytes& b) { return {b.data(),b.size()}; }
static Bytes image(const char* path) {
    std::ifstream file(path,std::ios::binary|std::ios::ate); CHECK(file); auto length=file.tellg(); CHECK(length>0);
    Bytes b(static_cast<std::size_t>(length)); file.seekg(0); CHECK(file.read(reinterpret_cast<char*>(b.data()),length)); return b;
}
static void reclaim(Isolate& isolate) { auto r=take(isolate.stop()); CHECK(!r.wait_until(deadline())); }
static void failure(Call& call,ErrorCode code,const char* message) {
    auto r=call.wait_until(deadline()); CHECK(std::holds_alternative<Error>(r));
    CHECK(std::get<Error>(r).code==code && std::get<Error>(r).message==message);
    CHECK(std::get<Error>(call.wait_until(deadline())).code==ErrorCode::closed);
}
int main(int argc,char** argv) {
    CHECK(argc==3); auto code=image(argv[1]),control=image(argv[2]); auto engine=take(Engine::create());
    Bytes input(65,0x59),expected={'o','w','n','e','d',':'}; expected.insert(expected.end(),input.begin(),input.end()); expected.push_back('!'); Bytes kept;
    {
        auto a=take(engine.create_isolate()),b=take(engine.create_isolate());
        CHECK(!a.load_module(view(control)) && !b.load_module(view(control)) && !b.load_module(view(code)));
        auto loop=take(a.start("binary_control_slice","loop",view(input)));
        auto peer=take(b.start("binary_construct_slice","decorate",view(input))); kept=take(peer.wait_until(deadline())); CHECK(kept==expected);
        for(const char* function : {"private","guard"}) {
            auto call=take(b.call("binary_control_slice",function,view(input))); CHECK(take(call.wait_until(deadline()))==input);
        }
        auto alias=take(b.call("binary_control_slice","private_alias",view(input)));
        failure(alias,ErrorCode::unsupported,"execution outside admitted ownership profile");
        auto limit=take(b.call("binary_control_slice","limit",view(input)));
        failure(limit,ErrorCode::limit,"runtime resource limit exceeded");
        auto badarg=take(b.call("binary_construct_slice","utf8",view(input))); failure(badarg,ErrorCode::exception,"guest exception");
        Bytes maximum(65536,0x31); auto oversized=take(b.call("binary_construct_slice","decorate",view(maximum)));
        failure(oversized,ErrorCode::limit,"transport limit exceeded");
        reclaim(a); CHECK(std::get<Error>(loop.wait_until(deadline())).code==ErrorCode::cancelled);
        auto after=take(b.call("binary_construct_slice","decorate",view(input))); CHECK(take(after.wait_until(deadline()))==expected);
        auto replacement=take(engine.create_isolate()); CHECK(!replacement.load_module(view(code)));
        auto fresh=take(replacement.start("binary_construct_slice","append",view(input))); auto appended=input; appended.push_back(33);
        CHECK(take(fresh.wait_until(deadline()))==appended); reclaim(replacement); reclaim(b);
    }
    CHECK(kept==expected && !engine.shutdown(deadline()));
    std::puts("API_BINARY_BUILD_OK guest_construction=true worker_only=true copied_reply=true private_append=true guard=true ownership_refusal=true runtime_transport_limits_distinct=true peer_stop=true replacement=true stateful_acceptance=false");
    return 0;
}
