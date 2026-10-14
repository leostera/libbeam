// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#include "test_process.hpp"
#include <gtest/gtest.h>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <regex>
#include <set>
#include <sstream>

namespace {
std::string nm, archive, adapter_archive, graph_path;
std::set<std::string> removed_symbols(const std::string& text) {
    const std::set<std::string> removed{
        "erts_is_embedded", "erts_set_signal", "erts_set_ignore_break", "erts_replace_intr",
        "init_break_handler", "erl_sys_late_init", "erts_sys_unix_later_init", "sys_init_signal_stack",
        "os_set_signal_2", "erl_drv_steal_main_thread", "erl_drv_stolen_main_thread_join",
        "spawn_driver", "forker_driver", "spawn_driver_entry", "forker_driver_entry", "sys_tty_reset",
        "sys_get_key", "do_break", "erts_do_break_handling", "erts_break_requested",
        "erl_sys_initial_tty_mode", "using_oldshell",
    };
    std::set<std::string> found;
    std::istringstream lines(text);
    for (std::string line; std::getline(lines, line);) {
        std::istringstream words(line);
        std::string symbol;
        for (std::string word; words >> word;) symbol = word;
        if (!symbol.empty() && symbol[0] == '_') symbol.erase(0, 1);
        if (removed.count(symbol)) found.insert(symbol);
    }
    return found;
}
TEST(RuntimeLinkage, RemovedHostSurfaceHasNeitherDefinitionsNorCallers) {
    for (const auto& library : {archive, adapter_archive}) {
        SCOPED_TRACE(library);
        const auto result = libbeam_test::run(nm, {library});
        ASSERT_TRUE(result.exited(0)) << result.output;
        EXPECT_TRUE(removed_symbols(result.standard_output).empty()) << result.standard_output;
    }
}
TEST(RuntimeLinkage, SymbolObserverDetectsBothDefinitionsAndCallers) {
    EXPECT_EQ(removed_symbols("000 T _erts_set_signal\n U _forker_driver\n"),
              (std::set<std::string>{"erts_set_signal", "forker_driver"}));
    EXPECT_EQ(removed_symbols("000 T os_set_signal_2\n"), (std::set<std::string>{"os_set_signal_2"}));
    EXPECT_TRUE(removed_symbols("archive.o:\n000 T _erl_start_embedded\n").empty());
}
const std::set<std::string> library_owners{
    "//libbeam:core", "//libbeam:libbeam", "rules_cc+//:empty_lib", "rules_cc+//:link_extra_lib",
};
std::string owner(const nlohmann::json& item) {
    return item.at("repository").get<std::string>() + "//" + item.at("package").get<std::string>() + ":" + item.at("name").get<std::string>();
}
void check_flags(const nlohmann::json& item) {
    const auto expected = owner(item) == "//libbeam:core"
        ? nlohmann::json::array({"-lz", "-lm", "-pthread"}) : nlohmann::json::array();
    EXPECT_EQ(item.at("flags"), expected);
    EXPECT_EQ(item.at("additional_inputs"), nlohmann::json::array());
}
TEST(RuntimeLinkage, ActualProductionGraphContainsOnlyOwnedArchivesAndSystemFlags) {
    std::ifstream file(graph_path);
    ASSERT_TRUE(file.good());
    const auto graph = nlohmann::json::parse(file);
    ASSERT_EQ(graph.at("inputs").size(), library_owners.size());
    std::set<std::string> seen;
    for (const auto& input : graph.at("inputs")) {
        const auto key = owner(input);
        SCOPED_TRACE(key);
        EXPECT_TRUE(seen.insert(key).second);
        check_flags(input);
        const auto& libraries = input.at("libraries");
        if (key == "//libbeam:core" || key == "//libbeam:libbeam") {
            ASSERT_EQ(libraries.size(), 1u);
            const auto& library = libraries[0];
            EXPECT_TRUE(library.at("dynamic_library").is_null());
            EXPECT_TRUE(library.at("interface_library").is_null());
            const auto stem = "libbeam/lib" + input.at("name").get<std::string>();
            unsigned archives = 0;
            for (const char* field : {"static_library", "pic_static_library"}) {
                if (library.at(field).is_null()) continue;
                const auto path = library.at(field).get<std::string>();
                EXPECT_TRUE(path == stem + ".a" || path == stem + ".pic.a") << path;
                ++archives;
            }
            EXPECT_GT(archives, 0u);
        } else {
            // rules_cc's default link-extra hooks must stay empty. A user-
            // supplied archive here is a real production dependency, not data.
            EXPECT_TRUE(libraries.empty());
        }
    }
    EXPECT_EQ(seen, library_owners);
    auto expected_nodes = library_owners;
    expected_nodes.insert("//libbeam:two_isolates");
    ASSERT_EQ(graph.at("nodes").size(), expected_nodes.size());
    seen.clear();
    for (const auto& node : graph.at("nodes")) {
        const auto key = owner(node);
        SCOPED_TRACE(key);
        EXPECT_TRUE(seen.insert(key).second);
        check_flags(node);
        EXPECT_EQ(node.at("kind"), key == "//libbeam:two_isolates" ? "cc_binary" : "cc_library");
    }
    EXPECT_EQ(seen, expected_nodes);
}
TEST(RuntimeLinkage, NoLegacyRuntimeAndOnlyOwnedExecutorThreadSurface) {
    const auto result = libbeam_test::run(nm, {"-u", archive});
    ASSERT_TRUE(result.exited(0)) << result.output;
    EXPECT_EQ(result.standard_output.find("erts_"), std::string::npos) << result.standard_output;
    EXPECT_EQ(result.standard_output.find("ethr_"), std::string::npos) << result.standard_output;
    const std::regex symbol(R"(\b_?(pthread_[A-Za-z0-9_]+)\b)");
    std::set<std::string> actual;
    for (auto it = std::sregex_iterator(result.standard_output.begin(), result.standard_output.end(), symbol); it != std::sregex_iterator(); ++it)
        actual.insert((*it)[1].str());
    const std::set<std::string> expected{
        "pthread_mutex_init", "pthread_mutex_destroy", "pthread_mutex_lock", "pthread_mutex_unlock",
        "pthread_cond_init", "pthread_cond_destroy", "pthread_cond_signal", "pthread_cond_wait",
        "pthread_cond_timedwait", "pthread_create", "pthread_join",
    };
    EXPECT_EQ(actual, expected);
}
}  // namespace

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    if (argc != 5) { std::cerr << "Expected declared nm, core archive, production graph and adapter archive runfiles\n"; return 2; }
    nm = libbeam_test::runfile(argv[1]); archive = libbeam_test::runfile(argv[2]);
    graph_path = libbeam_test::runfile(argv[3]);
    adapter_archive = libbeam_test::runfile(argv[4]);
    return RUN_ALL_TESTS();
}
