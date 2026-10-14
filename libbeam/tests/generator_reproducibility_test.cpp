// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#include "source_support.hpp"
#include "test_process.hpp"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <set>

namespace {
using libbeam_test::read_file;
using libbeam_test::runfile;

void compare(const std::string& directory, const std::string& name) {
    const auto first = read_file(runfile("libbeam/generated/" + directory + "/" + name));
    const auto repeated = read_file(runfile("libbeam/generated/repeat/" + directory + "/" + name));
    ASSERT_FALSE(first.empty()) << name;
    EXPECT_EQ(first, repeated) << name;
}
TEST(GeneratorReproducibility, ImmutableAtomDefinitions) {
    for (const std::string name : {"lb_atoms_generated.h", "lb_atoms_generated.inc", "lb_bif_ids_generated.h"}) compare("atoms", name);
}
TEST(GeneratorReproducibility, PreloadedBeamDoesNotEmbedItsBuildDirectory) {
    compare("atoms", "lb_preloaded_generated.inc");
}
TEST(GeneratorReproducibility, FixtureBytesDoNotEmbedTheirBuildDirectory) {
    const auto first = read_file(runfile("libbeam/fixtures/first_slice_fixture/first_slice.beam"));
    const auto repeated = read_file(runfile("libbeam/fixtures/first_slice_repeat_fixture/first_slice.beam"));
    ASSERT_FALSE(first.empty());
    EXPECT_EQ(first, repeated);
}
TEST(GeneratorReproducibility, OwnedOpcodeAndExecutionProjections) {
    for (const std::string name : {"lb_opcodes_generated.h", "lb_opcodes_generated.inc", "lb_transform_generated.inc", "lb_dispatch_generated.inc", "lb_dispatch_support.inc"}) compare("opcodes", name);
}
void compare_configurations(const std::string& group, const std::vector<std::string>& names) {
    for (const auto& name : names) {
        SCOPED_TRACE(group + "/" + name);
        const auto debug = read_file(runfile("libbeam/generation_configurations/debug/" + group + "/" + name));
        const auto release = read_file(runfile("libbeam/generation_configurations/release/" + group + "/" + name));
        ASSERT_FALSE(debug.empty());
        EXPECT_EQ(debug, release);
    }
}
TEST(GeneratorReproducibility, DebugAndReleaseArtifactsAreIdentical) {
    compare_configurations("atom_definitions", {"lb_atoms_generated.h", "lb_atoms_generated.inc", "lb_bif_ids_generated.h"});
    compare_configurations("preloaded_definitions", {"lb_preloaded_generated.inc"});
    compare_configurations("first_slice_fixture", {"first_slice.beam"});
    compare_configurations("opcode_definitions", {"lb_opcodes_generated.h", "lb_opcodes_generated.inc", "lb_transform_generated.inc", "lb_dispatch_generated.inc", "lb_dispatch_support.inc", "outputs.json"});
}
TEST(GeneratorReproducibility, CompleteUpstreamOutputDigests) {
    const auto first = nlohmann::json::parse(read_file(runfile("libbeam/generated/opcodes/outputs.json")));
    const auto repeated = nlohmann::json::parse(read_file(runfile("libbeam/generated/repeat/opcodes/outputs.json")));
    EXPECT_EQ(first, repeated);
    std::set<std::string> names;
    for (const auto& [name, digest] : first.items()) {
        names.insert(name);
        ASSERT_TRUE(digest.is_string());
        EXPECT_EQ(digest.get<std::string>().size(), 64u);
    }
    EXPECT_EQ(names, (std::set<std::string>{"beam_cold.h", "beam_hot.h", "beam_warm.h", "beam_opcodes.c", "beam_opcodes.h"}));
}
}  // namespace
