// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#include <libbeam/engine.hpp>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <utility>
using namespace libbeam;
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"CHECK %s:%d: %s\n",__FILE__,__LINE__,#x); std::exit(1); } } while(0)
template<class T> static T take(Result<T> result) {
    if(auto* error=std::get_if<Error>(&result)) std::fprintf(stderr,"runtime: %s\n",error->message.c_str());
    CHECK(std::holds_alternative<T>(result)); return std::move(std::get<T>(result));
}
static Bytes image(const char *path) {
    std::ifstream file(path,std::ios::binary|std::ios::ate); CHECK(file);
    auto size=file.tellg(); CHECK(size>0 && size<1024*1024);
    Bytes bytes(static_cast<std::size_t>(size)); file.seekg(0);
    CHECK(file.read(reinterpret_cast<char*>(bytes.data()),size)); return bytes;
}
static ByteView view(const Bytes& bytes) { return {bytes.data(),bytes.size()}; }
static Deadline deadline() { return std::chrono::steady_clock::now()+std::chrono::seconds(5); }
static Bytes round_trip(Isolate& isolate,const Bytes& request,bool starting) {
    auto call=take(starting?isolate.start("batch_alpha","entry",view(request)):
        isolate.call("batch_alpha","entry",view(request)));
    return take(call.wait_until(deadline()));
}
static void reclaim(Isolate& isolate) {
    auto stopped=take(isolate.stop()); CHECK(!stopped.wait_until(deadline()));
}
int main(int argc,char **argv) {
    CHECK(argc==4); auto alpha=image(argv[1]),beta=image(argv[2]),unsupported=image(argv[3]);
    auto engine=take(Engine::create()); Bytes request={0,1,2,0x7f,0xff};
    {
        auto first=take(engine.create_isolate()),peer=take(engine.create_isolate());
        std::vector<ByteView> reverse={view(beta),view(alpha)};
        std::vector<ByteView> forward={view(alpha),view(beta)};
        auto unresolved=take(engine.create_isolate());
        CHECK(!unresolved.load_modules({view(alpha)}));
        {
            auto call=take(unresolved.start("batch_alpha","entry",view(request)));
            auto result=call.wait_until(deadline());
            CHECK(std::holds_alternative<Error>(result) &&
                  std::get<Error>(result).code==ErrorCode::exception);
        }
        reclaim(unresolved);
        auto missing=take(engine.create_isolate());
        auto before_failure=take(missing.resource_usage());
        auto unsupported_error=missing.load_modules({view(alpha),view(beta),view(unsupported)});
        CHECK(unsupported_error && unsupported_error->code==ErrorCode::unsupported);
        auto after_unsupported=take(missing.resource_usage());
        CHECK(before_failure.memory_current==after_unsupported.memory_current);
        CHECK(!missing.load_modules(forward));
        CHECK(!first.load_modules(forward));
        CHECK(!peer.load_modules(reverse));
        auto a=take(first.start("batch_alpha","entry",view(request)));
        auto b=take(peer.start("batch_alpha","entry",view(request)));
        CHECK(take(a.wait_until(deadline()))==request);
        CHECK(take(b.wait_until(deadline()))==request);
        for(unsigned repeat=0;repeat<8;++repeat) {
            CHECK(round_trip(first,request,false)==request);
            CHECK(round_trip(peer,request,false)==request);
        }
        CHECK(first.load_module(view(alpha))->code==ErrorCode::invalid_state);
        reclaim(first); CHECK(round_trip(peer,request,false)==request);
        reclaim(peer); reclaim(missing);
    }
    {
        auto replacement=take(engine.create_isolate());
        CHECK(!replacement.load_modules({view(beta),view(alpha)}));
        CHECK(round_trip(replacement,request,true)==request);
        reclaim(replacement);
    }
    CHECK(!engine.shutdown(deadline()));
    puts("API_BATCH_ADMISSION_OK exact_names=true mutual_dependencies=true atomic_rollback=true simultaneous_worlds=true repeated_worker_calls=true replacement=true");
    return 0;
}
