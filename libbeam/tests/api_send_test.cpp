// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#include <libbeam/engine.hpp>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
using namespace libbeam;
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"CHECK %s:%d: %s\n",__FILE__,__LINE__,#x); std::exit(1); } } while(0)
template<class T> static T take(Result<T> result) {
    if(auto* e=std::get_if<Error>(&result)) std::fprintf(stderr,"runtime: %s\n",e->message.c_str());
    CHECK(std::holds_alternative<T>(result)); return std::get<T>(std::move(result));
}
static Deadline deadline() { return std::chrono::steady_clock::now()+std::chrono::seconds(10); }
static ByteView view(const Bytes& b) { return {b.data(),b.size()}; }
static void stop(Isolate& isolate) {
    auto receipt=take(isolate.stop()); CHECK(!receipt.wait_until(deadline()));
    const auto usage=take(isolate.resource_usage()); CHECK(usage.closed && !usage.memory_current);
}
int main(int argc,char** argv) {
    CHECK(argc==2); std::ifstream file(argv[1],std::ios::binary); CHECK(file.good());
    Bytes code{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
    auto engine=take(Engine::create());
    {
        auto isolate=take(engine.create_isolate()),peer=take(engine.create_isolate());
        for(auto* target:{&isolate,&peer}) {
            auto error=target->load_module(view(code));
            if(error) std::fprintf(stderr,"load: %s\n",error->message.c_str());
            CHECK(!error);
            auto boot=take(target->start("send_slice","identity",{})); CHECK(take(boot.wait_until(deadline())).empty());
        }
        std::fill(code.begin(),code.end(),0xa5);
        for(std::size_t size:{0u,65u,8192u}) for(const char* function:{"copy","named","dead"}) {
            Bytes expected(size); for(std::size_t i=0;i<size;++i) expected[i]=static_cast<unsigned char>(i*173+19);
            Bytes input=expected;
            auto call=take(isolate.call("send_slice",function,view(input)));
            auto other=take(peer.call("send_slice",function,view(input)));
            std::fill(input.begin(),input.end(),0xa5);
            CHECK(take(call.wait_until(deadline()))==expected);
            CHECK(take(other.wait_until(deadline()))==expected);
            auto again=call.wait_until(deadline()); CHECK(std::get<Error>(again).code==ErrorCode::closed);
        }
        Bytes invalid{131,97,7};
        for(const char* function:{"bad_destination","missing"}) {
            auto call=take(isolate.call("send_slice",function,view(invalid)));
            auto result=call.wait_until(deadline()); CHECK(std::holds_alternative<Error>(result));
            CHECK(std::get<Error>(result).code==ErrorCode::exception && std::get<Error>(result).message=="guest exception");
            auto reused=take(isolate.call("send_slice","copy",view(invalid)));
            CHECK(take(reused.wait_until(deadline()))==invalid);
        }
        stop(isolate);
        auto survivor=take(peer.call("send_slice","copy",view(invalid))); CHECK(take(survivor.wait_until(deadline()))==invalid);
        stop(peer);
    }
    CHECK(!engine.shutdown(deadline()));
    std::puts("API_SEND_OK self=true registered=true dead_pid=true invalid=true copied_input=true single_consumption=true two_isolates=true peer_survival=true reclaimed=true");
}
