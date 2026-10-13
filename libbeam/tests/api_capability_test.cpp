// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#include <libbeam/engine.hpp>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
using namespace libbeam;
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"capability API:%d: %s\n",__LINE__,#x); std::abort(); } } while(0)
static Deadline deadline() { return std::chrono::steady_clock::now()+std::chrono::seconds(5); }
template<class T> static T take(Result<T> result) {
    if(auto error=std::get_if<Error>(&result)) { std::fprintf(stderr,"%d: %s\n",int(error->code),error->message.c_str()); std::abort(); }
    return std::get<T>(std::move(result));
}
static void denied(Call& call) {
    auto result=call.wait_until(deadline()); CHECK(std::holds_alternative<Error>(result));
    const auto& error=std::get<Error>(result);
    CHECK(error.code==ErrorCode::capability_denied);
    CHECK(error.message=="erlang:load_nif/2 denied: NATIVE_LIBRARY");
    auto consumed=call.wait_until(deadline());
    CHECK(std::holds_alternative<Error>(consumed) && std::get<Error>(consumed).code==ErrorCode::closed);
}
static void stop(Isolate& isolate) {
    auto receipt=take(isolate.stop()); CHECK(!receipt.wait_until(deadline()));
    const auto usage=take(isolate.resource_usage()); CHECK(usage.closed && usage.memory_current==0);
}
int main(int argc,char** argv) {
    CHECK(argc==2); std::ifstream file(argv[1],std::ios::binary); CHECK(file.good());
    Bytes beam{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
    auto engine=take(Engine::create());
    {
        auto isolate=take(engine.create_isolate()),peer=take(engine.create_isolate());
        CHECK(!isolate.load_module({beam.data(),beam.size()}) && !peer.load_module({beam.data(),beam.size()}));
        auto direct=take(isolate.start("capability_slice","direct",{nullptr,0})); denied(direct);
        auto child=take(isolate.call("capability_slice","monitored",{nullptr,0})); denied(child);
        CHECK(!take(isolate.resource_usage()).closed);
        const Bytes payload{'o','k'};
        auto reused=take(isolate.call("capability_slice","identity",{payload.data(),payload.size()}));
        CHECK(take(reused.wait_until(deadline()))==payload);
        stop(isolate);
        auto other=take(peer.start("capability_slice","identity",{payload.data(),payload.size()}));
        CHECK(take(other.wait_until(deadline()))==payload); stop(peer);
    }
    CHECK(!engine.shutdown(deadline()));
    std::puts("API_CAPABILITY_OK distinct_error=true bounded_mfa=true child_failure=true reuse_peer=true reclaimed=true");
}
