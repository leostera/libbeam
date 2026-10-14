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
static Bytes image(const char* path) {
    std::ifstream file(path,std::ios::binary); CHECK(file.good());
    return Bytes{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
static void round_trip(Isolate& isolate,Isolate& peer,const char* module,const char* function,std::size_t size) {
    Bytes expected(size); for(std::size_t i=0;i<size;++i) expected[i]=static_cast<unsigned char>(i*173+19);
    Bytes input=expected;
    auto call=take(isolate.call(module,function,view(input)));
    auto other=take(peer.call(module,function,view(input)));
    std::fill(input.begin(),input.end(),0xa5);
    CHECK(take(call.wait_until(deadline()))==expected);
    CHECK(take(other.wait_until(deadline()))==expected);
    auto again=call.wait_until(deadline()); CHECK(std::get<Error>(again).code==ErrorCode::closed);
}
int main(int argc,char** argv) {
    CHECK(argc==5); Bytes code=image(argv[1]),monitors=image(argv[2]),leaders=image(argv[3]),flags=image(argv[4]);
    auto engine=take(Engine::create());
    {
        auto isolate=take(engine.create_isolate()),peer=take(engine.create_isolate());
        for(auto* target:{&isolate,&peer}) {
            auto error=target->load_module(view(code));
            if(error) std::fprintf(stderr,"load: %s\n",error->message.c_str());
            CHECK(!error);
            error=target->load_module(view(monitors));
            if(error) std::fprintf(stderr,"load: %s\n",error->message.c_str());
            CHECK(!error);
            error=target->load_module(view(leaders));
            if(error) std::fprintf(stderr,"load: %s\n",error->message.c_str());
            CHECK(!error);
            error=target->load_module(view(flags));
            if(error) std::fprintf(stderr,"load: %s\n",error->message.c_str());
            CHECK(!error);
            auto boot=take(target->start("send_slice","identity",{})); CHECK(take(boot.wait_until(deadline())).empty());
        }
        std::fill(code.begin(),code.end(),0xa5); std::fill(monitors.begin(),monitors.end(),0xa5);
        std::fill(leaders.begin(),leaders.end(),0xa5); std::fill(flags.begin(),flags.end(),0xa5);
        for(std::size_t size:{0u,65u,8192u}) {
            for(const char* function:{"copy","named","dead"}) round_trip(isolate,peer,"send_slice",function,size);
            for(const char* function:{"copy","named","local_name","missing","dead","cancel","self_monitor","mfa","mfa_peer"})
                round_trip(isolate,peer,"monitor_slice",function,size);
            round_trip(isolate,peer,"group_leader_slice","lifecycle",size);
            round_trip(isolate,peer,"process_flag_slice","copy",size);
            round_trip(isolate,peer,"process_flag_slice","lifecycle",size);
        }
        // Both registered targets stay alive until both arm calls finish.
        // Identical names must coexist in separate namespaces, not merely
        // succeed sequentially after the previous registration disappears.
        round_trip(isolate,peer,"monitor_slice","arm",65);
        round_trip(isolate,peer,"monitor_slice","drain",65);
        Bytes invalid{131,97,7};
        for(const char* function:{"bad_destination","missing"}) {
            auto call=take(isolate.call("send_slice",function,view(invalid)));
            auto result=call.wait_until(deadline()); CHECK(std::holds_alternative<Error>(result));
            CHECK(std::get<Error>(result).code==ErrorCode::exception && std::get<Error>(result).message=="guest exception");
            auto reused=take(isolate.call("send_slice","copy",view(invalid)));
            CHECK(take(reused.wait_until(deadline()))==invalid);
        }
        for(const char* function:{"bad_kind","bad_target","bad_name","bad_node","bad_reference","bad_module","bad_function","bad_args"}) {
            auto call=take(isolate.call("monitor_slice",function,view(invalid)));
            auto result=call.wait_until(deadline()); CHECK(std::holds_alternative<Error>(result));
            CHECK(std::get<Error>(result).code==ErrorCode::exception && std::get<Error>(result).message=="guest exception");
            round_trip(isolate,peer,"monitor_slice","copy",65);
        }
        for(const char* function:{"bad_value","bad_flag"}) {
            auto call=take(isolate.call("process_flag_slice",function,view(invalid)));
            auto result=call.wait_until(deadline()); CHECK(std::holds_alternative<Error>(result));
            CHECK(std::get<Error>(result).code==ErrorCode::exception && std::get<Error>(result).message=="guest exception");
            round_trip(isolate,peer,"process_flag_slice","copy",65);
        }
        stop(isolate);
        auto survivor=take(peer.call("send_slice","copy",view(invalid))); CHECK(take(survivor.wait_until(deadline()))==invalid);
        stop(peer);
    }
    CHECK(!engine.shutdown(deadline()));
    std::puts("API_SEND_OK self=true registered=true dead_pid=true invalid=true copied_input=true single_consumption=true two_isolates=true monitor_lifecycle=true named_scope=true demonitor=true group_leader_inheritance=true group_leader_reassignment=true peer_survival=true reclaimed=true");
}
