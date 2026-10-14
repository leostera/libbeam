// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#include "bif_inventory.hpp"
#include "test_directory.hpp"
#include "test_process.hpp"
#include <gtest/gtest.h>
#include <iostream>

namespace {
using libbeam_test::TestDirectory;
using libbeam_test::read_file;
using libbeam_test::run;
using libbeam_test::runfile;
using libbeam_test::inventory::Json;
using libbeam_test::inventory::Mfa;
std::string escript;

std::map<Mfa, Json> functions(const Json& report) {
    std::map<Mfa, Json> result;
    for (const auto& function : report.at("functions")) {
        auto key = Mfa::parse(function.at("name").get<std::string>() + "/" + std::to_string(function.at("arity").get<unsigned>()));
        if (!result.emplace(std::move(key), function).second) throw std::invalid_argument("Duplicate AST function");
    }
    return result;
}
struct Call {
    std::optional<std::string> module;
    std::string name;
    unsigned arity;
    bool operator<(const Call& other) const { return std::tie(module, name, arity) < std::tie(other.module, other.name, other.arity); }
    bool operator==(const Call& other) const { return module == other.module && name == other.name && arity == other.arity; }
};
std::set<Call> calls(const Json& values) {
    std::set<Call> result;
    for (const auto& value : values) result.insert({
        value.contains("module") ? std::optional<std::string>{value.at("module").get<std::string>()} : std::nullopt,
        value.at("name").get<std::string>(), value.at("arity").get<unsigned>(),
    });
    return result;
}

TEST(PreloadedAudit, AstDependenciesWithoutExecution) {
    TestDirectory directory;
    const auto source = directory.write("audit_fixture.erl", R"(-module(audit_fixture).
-export([f/2, stub/1, stub/2, dormant/0]).
f(F, X) when erlang:is_integer(X) ->
    local(X), peer:target(1, X), F(X), {fun local/1, fun peer:target/2}, self() ! X.
local(X) -> X.
stub(_) -> erlang:nif_error(undef).
stub(_, _) -> erlang:nif_error(undef).
dormant() -> erlang:halt(73).
)");
    const auto result = run(escript, {runfile("libbeam/tools/audit_preloaded_dependencies.escript"), source.string()});
    ASSERT_TRUE(result.exited(0)) << result.output;
    const auto parsed = functions(Json::parse(result.standard_output));
    const auto& f = parsed.at(Mfa::parse("f/2"));
    EXPECT_EQ(f.at("dynamic_call_count"), 1);
    EXPECT_EQ(f.at("nif_stub"), false);
    EXPECT_EQ(calls(f.at("guard_calls")), (std::set<Call>{{"erlang", "is_integer", 1}}));
    EXPECT_EQ(f.at("guard_calls").size(), 1u);
    EXPECT_EQ(calls(f.at("static_fun_references")), (std::set<Call>{{std::nullopt, "local", 1}, {"peer", "target", 2}}));
    EXPECT_EQ(f.at("operators"), Json::array({"!"}));
    const auto static_calls = calls(f.at("static_calls"));
    EXPECT_EQ(static_calls.count({"peer", "target", 2}), 1u);
    EXPECT_EQ(static_calls.count({std::nullopt, "local", 1}), 1u);
    EXPECT_EQ(static_calls.count({std::nullopt, "self", 0}), 1u);
    EXPECT_EQ(parsed.at(Mfa::parse("stub/1")).at("nif_stub"), true);
    EXPECT_EQ(parsed.at(Mfa::parse("stub/2")).at("nif_stub"), true);
    EXPECT_EQ(parsed.at(Mfa::parse("dormant/0")).at("nif_stub"), false);
}

TEST(PreloadedAudit, ParseErrorsAreNotPartialSuccess) {
    TestDirectory directory;
    const auto source = directory.write("audit_fixture.erl", "-module(audit_fixture).\nf( -> invalid.\n");
    const auto result = run(escript, {runfile("libbeam/tools/audit_preloaded_dependencies.escript"), source.string()});
    EXPECT_FALSE(result.exited(0));
    EXPECT_NE(result.standard_error.find("source_parse_errors"), std::string::npos) << result.output;
    EXPECT_TRUE(result.standard_output.empty()) << result.standard_output;
}

TEST(PreloadedAudit, LoweringRecordsBodyDynamicGuardPredicateAndSend) {
    TestDirectory directory;
    const auto inventory = directory.write("inventory.json", R"({"entries":[{"function":"+","arity":1},{"function":"is_pid","arity":1},{"function":"halt","arity":0},{"function":"send","arity":2}]})");
    const auto result = run(escript, {runfile("libbeam/tools/audit_bif_lowering.escript"), inventory.string(), (directory.root() / "output").string()});
    ASSERT_TRUE(result.exited(0)) << result.output;
    auto rows = Json::parse(result.standard_output).at("entries");
    std::map<std::string, unsigned> counts;
    bool sent = false;
    for (const auto& row : rows) {
        auto form = row.at("form").get<std::string>();
        ++counts[form];
        EXPECT_FALSE(row.at("instructions").empty());
        if (form == "operator") {
            const auto& instructions = row.at("instructions");
            EXPECT_NE(std::find(instructions.begin(), instructions.end(), Json("send")), instructions.end());
            sent = true;
        }
    }
    EXPECT_EQ(counts["body"], 4u);
    EXPECT_EQ(counts["dynamic"], 4u);
    EXPECT_EQ(counts["guard"], 2u);
    EXPECT_EQ(counts["predicate_test"], 1u);
    EXPECT_TRUE(sent);
}

TEST(PreloadedAudit, AllRequestedOverloadsExistInPinnedAst) {
    const auto result = run(escript, {runfile("libbeam/tools/audit_preloaded_dependencies.escript"), runfile("beam/erts/preloaded/src/erlang.erl")});
    ASSERT_TRUE(result.exited(0)) << result.output;
    const auto parsed = functions(Json::parse(result.standard_output));
    for (const auto& mfa : libbeam_test::inventory::requested(read_file(runfile("libbeam/core/otp/bif-batch-request.txt"))))
        EXPECT_EQ(parsed.count(mfa), 1u) << mfa.function() << '/' << mfa.arity();
    const auto& apply = parsed.at(Mfa::parse("apply/2"));
    EXPECT_EQ(apply.at("static_calls")[0].at("name"), "apply");
    EXPECT_EQ(apply.at("nif_stub"), false);
    EXPECT_EQ(parsed.at(Mfa::parse("node/0")).at("nif_stub"), true);
}
}  // namespace

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    if (argc != 2) { std::cerr << "Expected declared escript runfile\n"; return 2; }
    escript = runfile(argv[1]);
    return RUN_ALL_TESTS();
}
