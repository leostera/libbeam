// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#include <libbeam/engine.hpp>
#include "beam_image.h"
#include "source_support.hpp"
#include "test_process.hpp"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <map>
#include <optional>
#include <set>

namespace {
using Json = nlohmann::json;
using libbeam::Bytes;
using libbeam_test::read_file;
using libbeam_test::runfile;
using libbeam_test::sha256;
std::string fixture_path;
std::string repeat_fixture_path;
std::string sdk_fixture_path;

class Reader {
public:
    explicit Reader(std::string_view input): remaining_(input) {}
    std::string_view bytes(size_t count) {
        if (count > remaining_.size()) throw std::runtime_error("Truncated application fixture");
        const auto value = remaining_.substr(0, count);
        remaining_.remove_prefix(count);
        return value;
    }
    uint32_t integer(size_t width) {
        uint32_t value = 0;
        for (unsigned char byte : bytes(width)) value = (value << 8) | byte;
        return value;
    }
    bool empty() const { return remaining_.empty(); }
private:
    std::string_view remaining_;
};
struct Bundle {
    Json manifest;
    std::map<std::string, std::string> modules;
};
Bundle parse_bundle(std::string_view bytes) {
    Reader input(bytes);
    if (input.bytes(8) != "LBAPP001") throw std::runtime_error("Wrong fixture format");
    const auto manifest_size = input.integer(4);
    if (manifest_size > 1024 * 1024) throw std::runtime_error("Oversized manifest");
    Bundle result{Json::parse(input.bytes(manifest_size)), {}};
    const auto count = input.integer(4);
    if (!count || count > 4096) throw std::runtime_error("Invalid module count");
    size_t total = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const std::string name(input.bytes(input.integer(2)));
        const auto size = input.integer(4);
        if (name.empty() || !size || size > 64u * 1024u * 1024u - total)
            throw std::runtime_error("Invalid module record");
        total += size;
        if (!result.modules.emplace(name, input.bytes(size)).second)
            throw std::runtime_error("Duplicate module");
    }
    if (!input.empty()) throw std::runtime_error("Trailing fixture bytes");
    return result;
}
Bundle bundle() { return parse_bundle(read_file(fixture_path)); }
libbeam::ByteView view(std::string_view bytes) {
    return {reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()};
}
libbeam::Deadline deadline() { return std::chrono::steady_clock::now() + std::chrono::seconds(10); }
template<class T> T take(libbeam::Result<T> result) {
    if (const auto* error = std::get_if<libbeam::Error>(&result)) throw std::runtime_error(error->message);
    return std::get<T>(std::move(result));
}
void append16(std::string& bytes, size_t value) {
    if (value > 65535) throw std::runtime_error("Invalid test wire length");
    bytes.push_back(static_cast<char>(value >> 8));
    bytes.push_back(static_cast<char>(value));
}
std::string request(std::string_view method, std::string_view path, std::string_view query = {},
                    std::string_view body = {},
                    const std::vector<std::pair<std::string, std::string>>& headers = {}) {
    if (method.size() > 255 || headers.size() > 255) throw std::runtime_error("Invalid test wire length");
    std::string bytes{1, static_cast<char>(method.size())};
    append16(bytes, path.size());
    append16(bytes, query.size());
    bytes.push_back(static_cast<char>(headers.size()));
    append16(bytes, body.size());
    bytes.append(method).append(path).append(query);
    for (const auto& [name, value] : headers) {
        if (name.size() > 255) throw std::runtime_error("Invalid test header length");
        bytes.push_back(static_cast<char>(name.size()));
        append16(bytes, value.size());
        bytes.append(name).append(value);
    }
    bytes.append(body);
    return bytes;
}
std::string response(unsigned status, std::string_view body) {
    std::string bytes(1, 2);
    append16(bytes, status);
    append16(bytes, body.size());
    return bytes.append(body);
}

TEST(HelloArtifact, IndependentBuildsProduceIdenticalArtifactsAndClosure) {
    EXPECT_EQ(sha256(read_file(fixture_path)), sha256(read_file(repeat_fixture_path)));
}
TEST(HelloArtifact, UnchangedWorkerSdkJasonAndPackerSources) {
    // Snapshot of the real sibling project, not a replacement application.
    const std::pair<const char*, const char*> sources[] = {
        {"app/greeter.ex", "dd028bc6a5c8b7d965905d6baab2a47910f9fd1e416f29a2127f51c50aa2d028"},
        {"app/hello_worker.ex", "d3a99918aeace1055c5b41355827b3c1fe1a84c42edbcb8b3b3a69e7c1b92a5a"},
        {"jason/codegen.ex", "825cfa6ecf2fb91f8e285e8a9af91ed0679d08839dfab00339d27b6c310df70c"},
        {"jason/decoder.ex", "592372fb9b69ae0095a66a53a05e557416e1bc85f91ba7641a17e78c53d4e5e9"},
        {"jason/encode.ex", "9e8551342f2df4c15f929856bc26f13b9a8bdd49c07092dd6d4702f0cdda782b"},
        {"jason/encoder.ex", "8398c2e091bcd1853348221208eee145f2840552a9cf51f11183b36e0192bb39"},
        {"jason/formatter.ex", "99ee20b490a72990711fd169d92c4a3e30fed868feabeda24a4b376cfdb550d3"},
        {"jason/fragment.ex", "41c4676bc50827ffd10b0fe5d104549e27af4d6c0699fb7325df55337cd0a3a6"},
        {"jason/helpers.ex", "a5e925022ce35d92d179e334d214ffedcc6b1f23b2e4795307b4d35be3ac0f9a"},
        {"jason/jason.ex", "83061d55899b1d3afe4aa2b2968acefd57253419e57022c24e0455d953e9a331"},
        {"jason/ordered_object.ex", "beee914d382826d3839e2e83a85d9cacac7d548409f78d4234ae15ce2577900f"},
        {"jason/sigil.ex", "c5d69419ca806dd870fce232a873779af9ecc350f92b9abd6e5e7116e386bc28"},
        {"midgard/protocol.ex", "625db673d5c111d685a115fef1e30a0031c0ddbe48d02ea4dcdfed2371e59732"},
        {"midgard/request.ex", "42dbd03f09c9315623da709a0011bbabdb3a0c1c7e5c1c067937f175ded37fd4"},
        {"midgard/worker.ex", "f5888db27064089cc5bda24ad202314634c3b9d9a7ab0ec8969ecb638ba67098"},
        {"packer/artifact.ex", "b8faa724a8b99b46dbc0b28c27fdaad4ca0a7adeabc01ee7d0155d23c39f7e24"},
        {"packer/json.ex", "e1db271341c58ecd6d7ba3eb54a78ce9e0f6e28846d5fab54004a879c3abfd08"},
        {"packer/profile.ex", "83b473d3515585f435f98be33a41867ec1e25907922c00f06e4e7b73607e4785"},
        {"packer/verifier.ex", "a2c8db0a5855b23700f432e5ab5d7fff12f5186f556a63ac095e28d92b91f67a"},
    };
    for (const auto& [file, digest] : sources)
        EXPECT_EQ(sha256(read_file(runfile(std::string("libbeam/tests/fixtures/hello_worker/") + file))), digest) << file;
}
TEST(HelloArtifact, RejectsTruncatedAndTrailingFixtureData) {
    const auto bytes = read_file(fixture_path);
    EXPECT_THROW(parse_bundle(std::string_view(bytes).substr(0, 7)), std::runtime_error);
    EXPECT_THROW(parse_bundle(std::string_view(bytes).substr(0, bytes.size() - 1)), std::runtime_error);
    EXPECT_THROW(parse_bundle(bytes + "x"), std::runtime_error);
}
TEST(HelloArtifact, ExactApplicationIdentityAndCompleteStaticClosure) {
    const auto data = bundle();
    const auto& manifest = data.manifest;
    EXPECT_EQ(manifest.at("format"), "midgard.beam");
    EXPECT_EQ(manifest.at("format_version"), 1);
    EXPECT_EQ(manifest.at("application"), "hello_worker");
    EXPECT_EQ(manifest.at("entrypoint"), (Json{{"module", "Elixir.HelloWorker"},
        {"function", "__midgard_call__"}, {"arity", 1}, {"protocol", "midgard.bytes.v1"}}));
    ASSERT_EQ(manifest.at("modules").size(), 32u);
    ASSERT_EQ(manifest.at("dependencies").size(), 2u);
    EXPECT_EQ(manifest.at("dependencies")[0].at("app"), "jason");
    EXPECT_EQ(manifest.at("dependencies")[0].at("version"), "1.4.5");
    EXPECT_EQ(manifest.at("dependencies")[1].at("app"), "midgard");
    EXPECT_EQ(manifest.at("dependencies")[1].at("version"), "0.1.0");
    std::set<std::string> roots;
    for (const auto& module : manifest.at("modules")) {
        const auto name = module.at("name").get<std::string>();
        ASSERT_TRUE(roots.insert(name).second);
        ASSERT_TRUE(data.modules.count(name)) << name;
        EXPECT_EQ(module.at("path"), "modules/" + name + ".beam");
        EXPECT_EQ(module.at("size"), data.modules.at(name).size());
        EXPECT_EQ(module.at("sha256"), sha256(data.modules.at(name)));
    }
    for (const auto* name : {"Elixir.HelloWorker", "Elixir.HelloWorker.Greeter", "Elixir.Midgard.Protocol",
                            "Elixir.Midgard.Request", "Elixir.Midgard.Worker", "Elixir.Jason", "Elixir.Jason.Decoder"})
        EXPECT_TRUE(roots.count(name)) << name;
    EXPECT_GT(data.modules.size(), roots.size());
    for (const auto* name : {"Elixir.URI", "Elixir.Enum", "Elixir.Dict"})
        EXPECT_TRUE(data.modules.count(name)) << name;
    EXPECT_FALSE(data.modules.count("Dict"));
    LbAllocDomain* domain = nullptr;
    ASSERT_EQ(lb_alloc_domain_create(nullptr, &domain), LB_ALLOC_OK);
    for (const auto& [name, beam] : data.modules) {
        SCOPED_TRACE(name);
        LbBeamImage* raw = nullptr;
        EXPECT_EQ(lb_beam_image_create(domain, beam.data(), beam.size(), &raw), LB_BEAM_OK);
        if (!raw) continue;
        std::unique_ptr<LbBeamImage, decltype(&lb_beam_image_destroy)> image(raw, lb_beam_image_destroy);
        LbBeamBytes atom{};
        EXPECT_EQ(lb_beam_image_atom(raw, 1, &atom), LB_BEAM_OK);
        EXPECT_EQ(std::string(reinterpret_cast<const char*>(atom.data), atom.size), name);
        for (uint32_t i = 0; i < lb_beam_image_info(raw)->import_count; ++i) {
            LbBeamImport import{};
            EXPECT_EQ(lb_beam_image_import(raw, i, &import), LB_BEAM_OK);
            EXPECT_EQ(lb_beam_image_atom(raw, import.module, &atom), LB_BEAM_OK);
            const std::string dependency(reinterpret_cast<const char*>(atom.data), atom.size);
            EXPECT_TRUE(dependency == "erlang" || data.modules.count(dependency)) << dependency;
        }
    }
    EXPECT_EQ(lb_alloc_domain_usage(domain).current, 0u);
    EXPECT_EQ(lb_alloc_domain_destroy(domain), LB_ALLOC_OK);
    RecordProperty("application_bundle_sha256", sha256(read_file(fixture_path)));
    RecordProperty("closure_modules", static_cast<int>(data.modules.size()));
    RecordProperty("compiler", manifest.at("compiler").dump());
}

class HelloWorker : public testing::Test {
protected:
    void SetUp() override {
        if (shared_load_error) GTEST_SKIP() << shared_load_error->message;
        if (!shared_engine) shared_engine.emplace(take(libbeam::Engine::create()));
        if (!shared_isolate) {
            // Keep closure admission diagnostics separate from the public
            // default-budget gate; this explicit finite override changes no defaults.
            shared_isolate.emplace(take(shared_engine->create_isolate(libbeam::ResourceOverrides{
                .memory_limit_bytes = 512u * 1024u * 1024u})));
        }
        if (!shared_loaded) {
            const auto loaded = load(*shared_isolate);
            if (loaded) shared_load_error = *loaded;
            ASSERT_FALSE(loaded) << (loaded ? loaded->message : "");
            shared_loaded = true;
        }
        engine = &*shared_engine;
        isolate = &*shared_isolate;
    }
    static libbeam::Status load(libbeam::Isolate& target) {
        // The real loader must retain code after all caller-owned buffers die.
        const auto data = bundle();
        std::vector<libbeam::ByteView> modules;
        for (const auto& [name, beam] : data.modules) modules.push_back(view(beam));
        return target.load_modules(modules,libbeam::ModuleLoadOptions{
            .on_load=libbeam::OnLoadPolicy::defer_denied_nif});
    }
    std::string invoke(std::string_view bytes) {
        auto call = take(started ? isolate->call(entry_module, "__midgard_call__", view(bytes))
                                : isolate->start(entry_module, "__midgard_call__", view(bytes)));
        started = true;
        const auto result = take(call.wait_until(deadline()));
        const auto consumed = call.wait_until(deadline());
        EXPECT_TRUE(std::holds_alternative<libbeam::Error>(consumed));
        if (const auto* error = std::get_if<libbeam::Error>(&consumed))
            EXPECT_EQ(error->code, libbeam::ErrorCode::closed);
        return {result.begin(), result.end()};
    }
    static void reclaim(libbeam::Isolate& target) {
        auto receipt = take(target.stop());
        EXPECT_FALSE(receipt.wait_until(deadline()));
        const auto usage = take(target.resource_usage());
        EXPECT_TRUE(usage.closed);
        EXPECT_EQ(usage.memory_current, 0u);
        EXPECT_EQ(usage.limits.memory_limit_bytes, 512u * 1024u * 1024u);
        EXPECT_EQ(usage.limits.invocation_work_limit, 10000000u);
    }
    void TearDown() override {}
public:
    static bool shutdown_shared() {
        try {
            if (shared_isolate) {
                auto receipt = take(shared_isolate->stop());
                if (receipt.wait_until(deadline())) return false;
                const auto usage = take(shared_isolate->resource_usage());
                if (!usage.closed || usage.memory_current != 0) return false;
                shared_isolate.reset();
            }
            if (shared_engine) {
                if (shared_engine->shutdown(deadline())) return false;
                shared_engine.reset();
            }
            return true;
        } catch (...) { return false; }
    }
protected:
    inline static std::optional<libbeam::Engine> shared_engine;
    inline static std::optional<libbeam::Isolate> shared_isolate;
    inline static bool shared_loaded = false;
    inline static std::optional<libbeam::Error> shared_load_error;
    inline static bool sdk_loaded = false;
    inline static bool started = false;
    libbeam::Engine* engine = nullptr;
    libbeam::Isolate* isolate = nullptr;
    std::string entry_module = "Elixir.HelloWorker";
};
TEST_F(HelloWorker, DefaultGreeting) {
    EXPECT_EQ(invoke(request("GET", "/hello")), response(200, "Hello, world!\n"));
}
TEST_F(HelloWorker, NamedAndEncodedGreetings) {
    EXPECT_EQ(invoke(request("GET", "/hello", "name=Leo")), response(200, "Hello, Leo!\n"));
    EXPECT_EQ(invoke(request("GET", "/hello", "name=Hello%20World%21")), response(200, "Hello, Hello World!!\n"));
    EXPECT_EQ(invoke(request("GET", "/hello", "name=a+b")), response(200, "Hello, a b!\n"));
    EXPECT_EQ(invoke(request("GET", "/hello", "name=J%C3%B6rg")), response(200, "Hello, J\xc3\xb6rg!\n"));
    EXPECT_EQ(invoke(request("GET", "/hello", "name=")), response(200, "Hello, !\n"));
}
TEST_F(HelloWorker, RealJasonJson) {
    EXPECT_EQ(invoke(request("POST", "/json", "", R"({"name":"Leo"})")),
              response(200, "{\"greeting\":\"Hello, Leo!\\n\"}"));
    EXPECT_EQ(invoke(request("POST", "/json", "", R"({"name":"a\"b"})")),
              response(200, "{\"greeting\":\"Hello, a\\\"b!\\n\"}"));
}
TEST_F(HelloWorker, InvalidJsonMissingAndNonStringNames) {
    for (const auto* body : {"", "{", "{}", "null", "[]", R"({"name":3})", R"({"name":null})"}) {
        SCOPED_TRACE(body);
        EXPECT_EQ(invoke(request("POST", "/json", "", body)), response(400, "Invalid JSON\n"));
    }
}
TEST_F(HelloWorker, BinaryEchoAndBodyBoundary) {
    std::string bytes;
    for (unsigned i = 0; i < 32768; ++i) bytes.push_back(static_cast<char>(i));
    EXPECT_EQ(invoke(request("POST", "/echo", "", bytes)), response(200, bytes));
    EXPECT_EQ(invoke(request("POST", "/echo")), response(200, ""));
    EXPECT_EQ(invoke(request("POST", "/echo", "", bytes + "x")), response(400, "worker error\n"));
}
TEST_F(HelloWorker, MissingRoutesAndWrongMethods) {
    for (const auto* path : {"/missing", "/json", "/echo"})
        EXPECT_EQ(invoke(request("GET", path)), response(404, "Not found\n"));
    EXPECT_EQ(invoke(request("POST", "/hello")), response(404, "Not found\n"));
}
TEST_F(HelloWorker, MalformedProtocol) {
    const auto valid = request("GET", "/hello");
    for (size_t i = 0; i < valid.size(); ++i)
        EXPECT_EQ(invoke(std::string_view(valid).substr(0, i)), response(400, "worker error\n"));
    EXPECT_EQ(invoke(valid + "x"), response(400, "worker error\n"));
    auto version = valid; version[0] = 3;
    EXPECT_EQ(invoke(version), response(400, "worker error\n"));
    EXPECT_EQ(invoke(request("get", "/hello")), response(400, "worker error\n"));
    EXPECT_EQ(invoke(request("GET", "hello")), response(400, "worker error\n"));
}
TEST_F(HelloWorker, TransportBoundaryIsDistinctFromProtocolErrors) {
    EXPECT_EQ(invoke(std::string(65536, 'x')), response(400, "worker error\n"));
    const std::string oversized(65537, 'x');
    const auto result = isolate->call("Elixir.HelloWorker", "__midgard_call__", view(oversized));
    const auto* error = std::get_if<libbeam::Error>(&result);
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->code, libbeam::ErrorCode::limit);
    EXPECT_EQ(invoke(request("GET", "/hello")), response(200, "Hello, world!\n"));
}
TEST_F(HelloWorker, MethodPathAndQueryBoundaries) {
    EXPECT_EQ(invoke(request(std::string(16, 'G'), "/hello")), response(404, "Not found\n"));
    EXPECT_EQ(invoke(request(std::string(17, 'G'), "/hello")), response(400, "worker error\n"));
    EXPECT_EQ(invoke(request("", "/hello")), response(400, "worker error\n"));
    EXPECT_EQ(invoke(request("GET", "/" + std::string(2047, 'x'))), response(404, "Not found\n"));
    EXPECT_EQ(invoke(request("GET", "/" + std::string(2048, 'x'))), response(400, "worker error\n"));
    EXPECT_EQ(invoke(request("GET", "")), response(400, "worker error\n"));
    EXPECT_EQ(invoke(request("GET", "/hello", "name=" + std::string(2043, 'x'))),
              response(200, "Hello, " + std::string(2043, 'x') + "!\n"));
    EXPECT_EQ(invoke(request("GET", "/hello", "name=" + std::string(2044, 'x'))), response(400, "worker error\n"));
}
TEST_F(HelloWorker, HeaderCountSizeAndSyntaxBoundaries) {
    std::vector<std::pair<std::string, std::string>> headers(32, {"x", "v"});
    EXPECT_EQ(invoke(request("GET", "/hello", "", "", headers)), response(200, "Hello, world!\n"));
    headers.push_back({"x", "v"});
    EXPECT_EQ(invoke(request("GET", "/hello", "", "", headers)), response(400, "worker error\n"));
    EXPECT_EQ(invoke(request("GET", "/hello", "", "", {{std::string(128, 'x'), ""}})), response(200, "Hello, world!\n"));
    EXPECT_EQ(invoke(request("GET", "/hello", "", "", {{std::string(129, 'x'), ""}})), response(400, "worker error\n"));
    for (const auto* name : {"", "X", "bad name", "bad\rname"})
        EXPECT_EQ(invoke(request("GET", "/hello", "", "", {{name, "v"}})), response(400, "worker error\n"));
    for (const auto* value : {"bad\rvalue", "bad\nvalue"})
        EXPECT_EQ(invoke(request("GET", "/hello", "", "", {{"x", value}})), response(400, "worker error\n"));
    EXPECT_EQ(invoke(request("GET", "/hello", "", "", {{"x", std::string(4096, 'v')}})), response(200, "Hello, world!\n"));
    EXPECT_EQ(invoke(request("GET", "/hello", "", "", {{"x", std::string(4097, 'v')}})), response(400, "worker error\n"));
    headers = {{"x", std::string(4096, 'v')}, {"y", std::string(4088, 'v')}};
    EXPECT_EQ(invoke(request("GET", "/hello", "", "", headers)), response(200, "Hello, world!\n"));
    headers.back().second += "v";
    EXPECT_EQ(invoke(request("GET", "/hello", "", "", headers)), response(400, "worker error\n"));
    const auto valid = request("GET", "/hello", "", "", {{"x", "v"}});
    EXPECT_EQ(invoke(std::string_view(valid).substr(0, valid.size() - 1)), response(400, "worker error\n"));
}
TEST_F(HelloWorker, RepeatedMixedRequests) {
    for (unsigned i = 0; i < 40; ++i) {
        EXPECT_EQ(invoke(request("GET", "/hello", "name=again")), response(200, "Hello, again!\n"));
        EXPECT_EQ(invoke(request("POST", "/json", "", "bad")), response(400, "Invalid JSON\n"));
        EXPECT_EQ(invoke(request("POST", "/echo", "", "owned")), response(200, "owned"));
    }
}
TEST_F(HelloWorker, TwoIsolatesShareEngineNotApplicationState) {
    auto peer = take(engine->create_isolate());
    const auto loaded = load(peer);
    ASSERT_FALSE(loaded) << (loaded ? loaded->message : "");
    {
        const auto input = request("GET", "/hello", "name=peer");
        auto pending = take(peer.start("Elixir.HelloWorker", "__midgard_call__", view(input)));
        EXPECT_EQ(invoke(request("POST", "/echo", "", "first")), response(200, "first"));
        const auto result = take(pending.wait_until(deadline()));
        EXPECT_EQ(std::string(result.begin(), result.end()), response(200, "Hello, peer!\n"));
    }
    reclaim(peer);
    EXPECT_EQ(invoke(request("GET", "/hello")), response(200, "Hello, world!\n"));
}
// Separate callback fixtures exercise the real SDK without altering HelloWorker.
class SdkBoundary : public HelloWorker {
protected:
    void SetUp() override {
        HelloWorker::SetUp();
        if (HasFatalFailure() || shared_load_error) return;
        if (!sdk_loaded) {
            const auto beam = read_file(sdk_fixture_path);
            const auto loaded = isolate->load_module(view(beam));
            ASSERT_FALSE(loaded) << (loaded ? loaded->message : "");
            sdk_loaded = true;
        }
        entry_module = "Elixir.BoundaryWorker";
    }
};
TEST_F(SdkBoundary, OrdinaryCallbackExceptionBecomes500AndAllowsReuse) {
    EXPECT_EQ(invoke(request("GET", "/raise")), response(500, "worker error\n"));
    EXPECT_EQ(invoke(request("GET", "/ok")), response(200, "still usable\n"));
}
TEST_F(SdkBoundary, InvalidCallbackResponseBecomes500AndAllowsReuse) {
    EXPECT_EQ(invoke(request("GET", "/invalid")), response(500, "worker error\n"));
    EXPECT_EQ(invoke(request("GET", "/ok")), response(200, "still usable\n"));
}
TEST_F(SdkBoundary, RescueCannotConvertHostDenialInto500) {
    const auto bytes = request("GET", "/deny");
    {
        auto call = take(started ? isolate->call(entry_module, "__midgard_call__", view(bytes))
                                 : isolate->start(entry_module, "__midgard_call__", view(bytes)));
        started = true;
        const auto result = call.wait_until(deadline());
        const auto* error = std::get_if<libbeam::Error>(&result);
        ASSERT_NE(error, nullptr);
        EXPECT_EQ(error->code, libbeam::ErrorCode::capability_denied);
        EXPECT_EQ(error->message, "erlang:load_nif/2 denied: NATIVE_LIBRARY");
    }
    EXPECT_EQ(invoke(request("GET", "/ok")), response(200, "still usable\n"));
}
} // namespace

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    if (argc != 4) return 2;
    fixture_path = runfile(argv[1]);
    repeat_fixture_path = runfile(argv[2]);
    sdk_fixture_path = runfile(argv[3]);
    const int result = RUN_ALL_TESTS();
    return HelloWorker::shutdown_shared() ? result : 1;
}
