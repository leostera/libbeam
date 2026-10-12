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
static ByteView view(const Bytes& value) { return {value.data(),value.size()}; }
static void reclaim(Isolate& isolate) { auto r=take(isolate.stop()); CHECK(!r.wait_until(deadline())); }
int main(int argc,char** argv) {
    CHECK(argc==2); std::ifstream file(argv[1],std::ios::binary|std::ios::ate); CHECK(file);
    auto length=file.tellg(); CHECK(length>0); Bytes code(static_cast<std::size_t>(length)); file.seekg(0);
    CHECK(file.read(reinterpret_cast<char*>(code.data()),length));
    auto engine=take(Engine::create()); Bytes input(65,0x59),kept;
    {
        // This gate tests explicit stop, not the independently tested work threshold.
        // Release can finish the old default allowance during the peer's large memset.
        ResourceOverrides stopping; stopping.invocation_work_limit=UINT64_MAX;
        auto a=take(engine.create_isolate(stopping)),b=take(engine.create_isolate());
        CHECK(!a.load_module(view(code)) && !b.load_module(view(code)));
        auto loop=take(a.start("writable_init_slice","loop",view(input)));
        auto peer=take(b.start("writable_init_slice","saved",view(input))); kept=take(peer.wait_until(deadline())); CHECK(kept==input);
        auto limit=take(b.call("writable_init_slice","limit",view(input)));
        auto result=limit.wait_until(deadline()); CHECK(std::holds_alternative<Error>(result));
        const auto& error=std::get<Error>(result);
        CHECK(error.code==ErrorCode::limit && error.message=="runtime resource limit exceeded");
        auto again=limit.wait_until(deadline()); CHECK(std::get<Error>(again).code==ErrorCode::closed);
        auto large=take(b.call("writable_init_slice","large",view(input))); CHECK(take(large.wait_until(deadline())).empty());
        reclaim(a); auto cancelled=loop.wait_until(deadline()); CHECK(std::get<Error>(cancelled).code==ErrorCode::cancelled);
        auto after=take(b.call("writable_init_slice","saved",view(input))); CHECK(take(after.wait_until(deadline()))==input);
        auto replacement=take(engine.create_isolate()); CHECK(!replacement.load_module(view(code)));
        auto zero=take(replacement.start("writable_init_slice","zero",view(input))); CHECK(take(zero.wait_until(deadline())).empty());
        reclaim(replacement); reclaim(b);
    }
    CHECK(kept==input && !engine.shutdown(deadline()));
    std::puts("API_WRITABLE_INIT_OK worker_execution=true copied_y_root=true limit_not_guest_exception=true once_only=true peer_stop=true replacement=true constructor_component_only=true stateful_acceptance=false");
    return 0;
}
