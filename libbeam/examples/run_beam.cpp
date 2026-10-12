// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
// Host-only file/CLI boundary over the same public API as the acceptance tests.
#include <libbeam/engine.hpp>
#include <cstdio>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
using namespace libbeam;
struct Invocation { Bytes input; std::optional<Bytes> expected; };
struct Bootstrap { std::string function; Invocation invocation; };
struct ModuleGroup { std::string main; std::vector<std::string> dependencies; };
struct Plan {
    std::string module,function;
    std::optional<Bootstrap> boot;
    std::vector<Invocation> calls;
    std::vector<ModuleGroup> groups;
};
static Bytes text(const std::string& value) { return {value.begin(),value.end()}; }
static ByteView view(const Bytes& value) { return {value.data(),value.size()}; }
static Bytes file_bytes(const std::string& path,std::size_t maximum) {
    std::ifstream file(path,std::ios::binary|std::ios::ate);
    if(!file) throw std::runtime_error("cannot open file: "+path);
    auto size=file.tellg();
    if(size<0 || static_cast<std::uint64_t>(size)>maximum) throw std::runtime_error("file size outside allowed range: "+path);
    Bytes result(static_cast<std::size_t>(size)); file.seekg(0);
    if(size && !file.read(reinterpret_cast<char*>(result.data()),size)) throw std::runtime_error("cannot read file: "+path);
    return result;
}
static Plan parse(int argc,char** argv) {
    Plan plan;
    auto next=[&](int& i) -> std::string {
        if(++i>=argc) throw std::runtime_error("missing option value");
        return argv[i];
    };
    for(int i=1;i<argc;++i) {
        std::string option=argv[i];
        if(option=="--entry") {
            if(!plan.module.empty()) throw std::runtime_error("duplicate --entry");
            plan.module=next(i); plan.function=next(i);
        } else if(option=="--isolate") {
            plan.groups.push_back({next(i),{}});
        } else if(option=="--dependency") {
            if(plan.groups.empty()) throw std::runtime_error("--dependency requires a preceding --isolate");
            plan.groups.back().dependencies.push_back(next(i));
        } else if(option=="--call" || option=="--call-file") {
            auto value=next(i);
            plan.calls.push_back({option=="--call"?text(value):file_bytes(value,65536),{}});
        } else if(option=="--expect" || option=="--expect-file") {
            if(plan.calls.empty() || plan.calls.back().expected) throw std::runtime_error("--expect requires an unchecked preceding call");
            auto value=next(i);
            plan.calls.back().expected=option=="--expect"?text(value):file_bytes(value,65536);
        } else if(option=="--boot") {
            if(plan.boot) throw std::runtime_error("duplicate --boot");
            auto function=next(i); auto input=next(i);
            plan.boot=Bootstrap{std::move(function),{text(input),{}}};
        } else if(option=="--expect-boot") {
            if(!plan.boot || plan.boot->invocation.expected) throw std::runtime_error("--expect-boot requires an unchecked --boot");
            plan.boot->invocation.expected=text(next(i));
        } else throw std::runtime_error("unknown option: "+option);
    }
    if(plan.module.empty() || plan.function.empty() || plan.groups.empty() || plan.calls.empty())
        throw std::runtime_error("required: --entry MODULE FUNCTION, --isolate MAIN.beam, and --call TEXT or --call-file FILE");
    for(const auto& call:plan.calls) {
        if(call.input.size()>65536) throw std::runtime_error("call input exceeds 64 KiB");
        if(call.expected && call.expected->size()>65536) throw std::runtime_error("expected output exceeds 64 KiB");
    }
    if(plan.boot) {
        if(plan.boot->invocation.input.size()>65536) throw std::runtime_error("boot input exceeds 64 KiB");
        if(plan.boot->invocation.expected && plan.boot->invocation.expected->size()>65536)
            throw std::runtime_error("expected boot output exceeds 64 KiB");
    }
    return plan;
}
template<class T> static T take(Result<T> result) {
    if(auto* failure=std::get_if<Error>(&result)) throw std::runtime_error(failure->message);
    return std::move(std::get<T>(result));
}
static Deadline deadline() { return std::chrono::steady_clock::now()+std::chrono::seconds(5); }
static void require(Status status) { if(status) throw std::runtime_error(status->message); }
static void show(std::size_t index,const char* phase,const Bytes& bytes) {
    std::printf("ISOLATE %zu %s bytes=%zu hex=",index,phase,bytes.size());
    for(auto byte:bytes) std::printf("%02x",static_cast<unsigned>(byte));
    std::putchar('\n');
}
static bool load_group(Isolate& world,std::size_t index,const ModuleGroup& group) {
    std::vector<std::string> paths=group.dependencies;
    paths.push_back(group.main);
    std::vector<Bytes> images; images.reserve(paths.size());
    std::vector<ByteView> views; views.reserve(paths.size());
    for(const auto& path:paths) images.push_back(file_bytes(path,8*1024*1024));
    for(const auto& image:images) views.push_back(view(image));
    if(auto failure=world.load_modules(views)) {
        std::printf("ISOLATE %zu LOAD_GROUP modules=%zu code=%d error=%s\n",index,paths.size(),
                    static_cast<int>(failure->code),failure->message.c_str());
        return false;
    }
    for(const auto& path:paths)
        std::printf("ISOLATE %zu LOAD path=%s admitted=true\n",index,path.c_str());
    return true;
}
static bool finish(Call& call,std::size_t index,const char* phase,const Invocation& expected) {
    auto result=call.wait_until(deadline());
    if(auto* failure=std::get_if<Error>(&result)) {
        std::printf("ISOLATE %zu %s code=%d error=%s\n",index,phase,static_cast<int>(failure->code),failure->message.c_str()); return false;
    }
    const auto& bytes=std::get<Bytes>(result); show(index,phase,bytes);
    if(expected.expected && bytes!=*expected.expected) {
        std::printf("ISOLATE %zu expected_match=false\n",index); return false;
    }
    return true;
}
static bool invoke(Isolate& world,std::size_t index,const Plan& plan,const Invocation& invocation,
                   const std::string& function,bool starting) {
    auto result=starting?world.start(plan.module,function,view(invocation.input)):
        world.call(plan.module,function,view(invocation.input));
    if(auto* failure=std::get_if<Error>(&result)) {
        std::printf("ISOLATE %zu %s code=%d error=%s\n",index,starting?"START":"CALL",static_cast<int>(failure->code),failure->message.c_str()); return false;
    }
    auto call=std::move(std::get<Call>(result)); return finish(call,index,starting?"START":"CALL",invocation);
}
static int execute(const Plan& plan) {
    auto engine=take(Engine::create()); bool success=true;
    {
        // All exact-name groups coexist; each dependency graph publishes in one
        // transaction, so forward and mutual imports need no load ordering.
        std::vector<Isolate> worlds; worlds.reserve(plan.groups.size());
        for(std::size_t i=0;i<plan.groups.size();++i) {
            worlds.push_back(take(engine.create_isolate()));
            if(!load_group(worlds.back(),i,plan.groups[i])) success=false;
        }
        enum class RunState { loaded,running,failed };
        std::vector<RunState> states(worlds.size(),RunState::loaded);
        if(plan.boot) for(std::size_t i=0;i<worlds.size();++i) {
            if(invoke(worlds[i],i,plan,plan.boot->invocation,plan.boot->function,true)) states[i]=RunState::running;
            else { states[i]=RunState::failed; success=false; }
        }
        // Round-robin host calls demonstrate peer progress with both actors live.
        for(const auto& call:plan.calls) for(std::size_t i=0;i<worlds.size();++i) {
            if(states[i]==RunState::failed) continue;
            if(invoke(worlds[i],i,plan,call,plan.function,states[i]==RunState::loaded)) states[i]=RunState::running;
            else { states[i]=RunState::failed; success=false; }
        }
        for(auto& world:worlds) { auto retirement=take(world.stop()); require(retirement.wait_until(deadline())); }
    }
    require(engine.shutdown(deadline()));
    std::printf("BEAM_RUNNER_DONE isolates=%zu success=%s reclaimed=true engine_shutdown=true\n",plan.groups.size(),success?"true":"false");
    return success?0:1;
}
int main(int argc,char** argv) {
    if(argc==2 && std::string(argv[1])=="--help") {
        std::puts("run_beam --entry MODULE FUNCTION [--boot FUNCTION TEXT --expect-boot TEXT] --call TEXT [--expect TEXT] [--call-file FILE --expect-file FILE] --isolate MAIN.beam [--dependency DEP.beam] [--isolate OTHER.beam ...]"); return 0;
    }
    try { auto plan=parse(argc,argv); return execute(plan); }
    catch(const std::exception& error) { std::fprintf(stderr,"run_beam: %s\n",error.what()); return 2; }
}
