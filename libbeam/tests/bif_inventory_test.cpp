// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#include "bif_inventory.hpp"
#include "test_process.hpp"
#include <gtest/gtest.h>

namespace {
using namespace libbeam_test::inventory;
using libbeam_test::read_file;
using libbeam_test::runfile;
using libbeam_test::sha256;

TEST(InventoryCli, EmitsCompleteSourceFactsWithoutClaimingCoverage) {
    const auto result = libbeam_test::run(runfile("libbeam/bif_inventory"), {"--root", runfile("")});
    ASSERT_TRUE(result.exited(0)) << result.output;
    EXPECT_TRUE(result.standard_error.empty());
    const auto report = Json::parse(result.standard_output);
    EXPECT_EQ(report.at("requested_count"), 185);
    EXPECT_EQ(report.at("direct_native_declarations"), 135);
    EXPECT_EQ(report.at("entries").size(), 185u);
    EXPECT_EQ(report.at("scope"), "source facts and proposed contracts; not capability admission or test coverage");
}
TEST(InventoryCli, InvalidArgumentsAndMissingInputsFailWithoutPartialReport) {
    const auto program = runfile("libbeam/bif_inventory");
    const auto invalid = libbeam_test::run(program, {"--unknown"});
    EXPECT_TRUE(invalid.exited(2));
    EXPECT_TRUE(invalid.standard_output.empty());
    const auto missing = libbeam_test::run(program, {"--root", runfile("missing-inventory-root")});
    EXPECT_TRUE(missing.exited(1));
    EXPECT_TRUE(missing.standard_output.empty());
    EXPECT_NE(missing.standard_error.find("BIF inventory failed:"), std::string::npos);
}

class Inventory : public testing::Test {
protected:
    Path root;
    Path request;
    void SetUp() override {
        const char* temporary = std::getenv("TEST_TMPDIR");
        ASSERT_NE(temporary, nullptr);
        std::string pattern = std::string(temporary) + "/inventory-XXXXXX";
        auto* directory = mkdtemp(pattern.data());
        ASSERT_NE(directory, nullptr);
        root = directory;
        request = root / "request.txt";
    }
    void TearDown() override {
        std::error_code error;
        if (!root.empty()) std::filesystem::remove_all(root, error);
        EXPECT_FALSE(error) << error.message();
    }
    Path put(const std::string& name, const std::string& text) {
        auto path = root / name;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary);
        output << text;
        output.close();
        if (!output) throw std::runtime_error("Cannot write fixture");
        return path;
    }
    void source_fixture() {
        put("request.txt", "+/1\ntrace_info/2\napply/2\nunknown/0\n");
        put("beam/erts/emulator/beam/bif.tab", "ubif erlang:'+'/1 splus_1\nbif erlang:trace_info/2\n");
        put("beam/erts/emulator/beam/example.c", "BIF_RETTYPE splus_1(BIF_ALIST_1) { return 0; }\nEterm\ntrace_info_2(BIF_ALIST_2) { return 0; }\n");
        put("beam/erts/preloaded/src/erlang.erl", "apply(Fun, Args) -> erlang:apply(Fun, Args).\napply(Module, Name, Args) -> erlang:apply(Module, Name, Args).\n");
        put("libbeam/core/engine.c", "{BIF_splus_1, {am_erlang, am_Plus, 1, implementation, BIF_KIND_GUARD}}\n");
    }
};

TEST(InventoryParsing, ExactNamesAndArities) {
    EXPECT_EQ(requested("# requirement list\n\n+/1\n'/'/2\nfoo/0\nfoo/255\n"),
              (std::vector<Mfa>{Mfa::parse("+/1"), Mfa::parse("'/'/2"), Mfa::parse("foo/0"), Mfa::parse("foo/255")}));
}
TEST(InventoryParsing, InvalidOrDuplicateRequests) {
    for (const std::string input : {"", "# empty\n", "foo/-1", "foo/256", "foo", "'foo/1", "foo'/1", "''/1",
                                   "foo/1\n'foo'/1", "foo/1\nfoo/1", "foo /1", "foo/+1", "foo/9999999999999999999999"}) {
        SCOPED_TRACE(input);
        EXPECT_THROW(requested(input), std::invalid_argument);
    }
}
TEST_F(Inventory, CandidatesDoNotClaimSemanticsOrCoverage) {
    source_fixture();
    auto result = audit(root, request);
    EXPECT_EQ(result.at("requested_count"), 4);
    EXPECT_EQ(result.at("direct_native_declarations"), 2);
    const auto& rows = result.at("entries");
    EXPECT_EQ(rows[0]["native_declaration"]["symbol"], "splus_1");
    EXPECT_EQ(rows[0]["native_id_in_engine_catalog"], true);
    EXPECT_EQ(rows[1]["native_id_in_engine_catalog"], false);
    EXPECT_EQ(rows[1]["native_source_candidates"][0]["line"], 2);
    EXPECT_TRUE(rows[2]["native_declaration"].is_null());
    EXPECT_EQ(rows[2]["preloaded_head_candidate_lines"], Json::array({1, 2}));
    EXPECT_EQ(rows[3]["preloaded_head_candidate_lines"], Json::array());
    for (const auto& row : rows) {
        EXPECT_EQ(row["policy_status"], "unreviewed");
        EXPECT_EQ(row["implementation_evidence"], Json::array());
    }
    for (const auto& [name, digest] : result["source_hashes"].items()) EXPECT_EQ(digest, sha256(read_file(root / name)));
    EXPECT_EQ(audit(root, request), result);
}
TEST_F(Inventory, DuplicateNativeDeclarationsFail) {
    source_fixture();
    put("beam/erts/emulator/beam/bif.tab", "bif erlang:trace_info/2\nbif erlang:trace_info/2\n");
    EXPECT_THROW(audit(root, request), std::invalid_argument);
}
TEST_F(Inventory, UnparsedNativeDeclarationsFail) {
    source_fixture();
    put("beam/erts/emulator/beam/bif.tab", "bif erlang:trace_info/2 unexpected extra\n");
    EXPECT_THROW(audit(root, request), std::invalid_argument);
}
TEST(InventoryParsing, ContractCoverageIsExact) {
    auto document = Json::parse(R"({"common":{},"groups":[{"id":"numeric","classification":"local","contract":"real arithmetic","mfas":["+/1"]}]})");
    const std::set<Mfa> expected{Mfa::parse("+/1")};
    EXPECT_EQ(contracts(document, expected).at(Mfa::parse("+/1"))["classification"], "local");
    EXPECT_THROW(contracts(document, {Mfa::parse("+/1"), Mfa::parse("foo/0")}), std::invalid_argument);
    for (const Json& names : {Json::array({"+/1", "'+'/1"}), Json::array({"unknown/0"}), Json::array({"'+/1"})}) {
        document["groups"][0]["mfas"] = names;
        EXPECT_THROW(contracts(document, expected), std::invalid_argument);
    }
    document["groups"][0]["mfas"] = Json::array({"+/1"});
    document["groups"][0]["classification"] = "successful-placeholder";
    EXPECT_THROW(contracts(document, expected), std::invalid_argument);
    document["groups"][0]["classification"] = "local";
    document["groups"][0]["contract"] = "  ";
    EXPECT_THROW(contracts(document, expected), std::invalid_argument);
    document["groups"][0]["contract"] = "real arithmetic";
    document["groups"].push_back(document["groups"][0]);
    EXPECT_THROW(contracts(document, expected), std::invalid_argument);
}
TEST(InventoryParsing, WrapperPlanCoverageIsExact) {
    auto document = Json::parse(R"({"plans":[{"mode":"beam","reason":"preserve complete function","mfas":["alias/0"]}]})");
    const std::set<Mfa> expected{Mfa::parse("alias/0")};
    EXPECT_EQ(wrappers(document, expected).at(Mfa::parse("alias/0"))["mode"], "beam");
    EXPECT_THROW(wrappers(document, {Mfa::parse("alias/0"), Mfa::parse("other/1")}), std::invalid_argument);
    document["plans"][0]["mfas"].push_back("alias/0");
    EXPECT_THROW(wrappers(document, expected), std::invalid_argument);
    document["plans"][0]["mfas"] = Json::array({"alias/0"});
    document["plans"][0]["mode"] = "stub";
    EXPECT_THROW(wrappers(document, expected), std::invalid_argument);
    document["plans"][0]["mode"] = "beam";
    document["plans"][0]["reason"] = "";
    EXPECT_THROW(wrappers(document, expected), std::invalid_argument);
}
TEST(InventorySources, DenialDeclarationsMatchPolicy) {
    auto requests = requested(read_file(runfile("libbeam/core/otp/bif-batch-request.txt")));
    auto document = Json::parse(read_file(runfile("libbeam/core/otp/bif-batch-contracts.json")));
    auto assigned = contracts(document, {requests.begin(), requests.end()});
    std::set<Mfa> expected, actual;
    std::set<std::string> identities;
    for (const auto& [mfa, contract] : assigned) if (contract["classification"] == "denial") expected.insert(mfa);
    auto declarations = read_file(runfile("libbeam/core/capability_denials.def"));
    const std::regex pattern(R"(^LB_DENIAL\(([^,]+), (\w+), (\d+), (\w+)\)$)", std::regex::multiline);
    size_t count = 0;
    for (auto it = std::sregex_iterator(declarations.begin(), declarations.end(), pattern); it != std::sregex_iterator(); ++it) {
        ++count;
        EXPECT_TRUE(actual.insert(Mfa::parse((*it)[2].str() + "/" + (*it)[3].str())).second);
        EXPECT_TRUE(identities.insert((*it)[1].str()).second);
        if ((*it)[2] == "load_nif") EXPECT_EQ((*it)[4], "NATIVE_LIBRARY");
    }
    EXPECT_EQ(actual, expected);
    EXPECT_EQ(count, expected.size());
}
TEST(InventorySources, PinnedInventory) {
    auto root = Path(runfile(""));
    auto result = audit(root, root / "libbeam/core/otp/bif-batch-request.txt",
                        root / "libbeam/core/otp/bif-batch-contracts.json", root / "libbeam/core/otp/bif-batch-wrapper-plan.json");
    EXPECT_EQ(result["requested_count"], 185);
    EXPECT_EQ(result["direct_native_declarations"], 135);
    for (const auto& row : result["entries"]) {
        SCOPED_TRACE(row["function"].dump());
        const std::set<std::string> allowed{"local", "query", "denial", "exception"};
        EXPECT_EQ(allowed.count(row["proposed_contract"]["classification"].get<std::string>()), 1u);
        EXPECT_EQ(row["policy_status"], "contract-assigned; implementation-review-pending");
        EXPECT_TRUE(row["proposed_implementation"].contains("mode"));
        if (!row["native_declaration"].is_null()) EXPECT_FALSE(row["native_source_candidates"].empty());
        else EXPECT_FALSE(row["preloaded_head_candidate_lines"].empty());
    }
    EXPECT_TRUE(result["source_hashes"].contains("libbeam/core/otp/bif-batch-contracts.json"));
    EXPECT_TRUE(result["source_hashes"].contains("libbeam/core/otp/bif-batch-wrapper-plan.json"));
}
TEST(SourceSupport, KnownSha256Values) {
    EXPECT_EQ(sha256(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(sha256("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    EXPECT_EQ(sha256(std::string(1000000, 'a')), "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}
}  // namespace
