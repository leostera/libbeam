// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#include "atom_generator.hpp"
#include <gtest/gtest.h>

namespace {
const std::string atoms = "char* erl_atom_names[] = {\n  \"false\",\n  \"true\",\n  0\n};\n";
const std::string header =
    "#ifndef __ERL_ATOM_TABLE_H__\n#define __ERL_ATOM_TABLE_H__\n"
    "extern char* erl_atom_names[];\n"
    "#define am_false make_atom(0)\n#define am_true make_atom(1)\n#endif\n";
const std::string bifs = "#define BIF_test_1 0\nextern void forbidden_dispatch();\n#define BIF_SIZE 1\n";
TEST(AtomGenerator, PreservesOriginalIndicesAndAppendsOnlyOwnedAtoms) {
    const auto projected = libbeam_build::project_atoms(atoms, header, bifs);
    EXPECT_NE(projected.source.find("static const char *const lb_predefined_names[] = {\n  \"false\",\n  \"true\",\n  \"set_cookie\",\n  \"load_module\",\n  \"spawn_monitor\",\n  0\n};"), std::string::npos);
    EXPECT_NE(projected.header.find("#define am_false make_atom(0)\n#define am_true make_atom(1)"), std::string::npos);
    EXPECT_NE(projected.header.find("#define am_set_cookie make_atom(2)\n"), std::string::npos);
    EXPECT_NE(projected.header.find("#define am_load_module make_atom(3)\n"), std::string::npos);
    EXPECT_NE(projected.header.find("#define am_spawn_monitor make_atom(4)\n"), std::string::npos);
    EXPECT_EQ(projected.header.find("extern char*"), std::string::npos);
    EXPECT_EQ(projected.header.find("__ERL_ATOM_TABLE_H__"), std::string::npos);
    EXPECT_NE(projected.header.find("LIBBEAM_GENERATED_ATOMS_H"), std::string::npos);
    EXPECT_EQ(projected.bif_ids.find("forbidden_dispatch"), std::string::npos);
    EXPECT_NE(projected.bif_ids.find("#define BIF_test_1 0\n#define BIF_SIZE 1\n"), std::string::npos);
}
TEST(AtomGenerator, RejectsDuplicateMissingAndChangedDeclarations) {
    EXPECT_THROW(libbeam_build::project_atoms(atoms + atoms, header, bifs), std::runtime_error);
    EXPECT_THROW(libbeam_build::project_atoms("", header, bifs), std::runtime_error);
    EXPECT_THROW(libbeam_build::project_atoms(atoms, "", bifs), std::runtime_error);
    EXPECT_THROW(libbeam_build::project_atoms(atoms, header, ""), std::runtime_error);
    std::string no_sentinel = atoms;
    no_sentinel.replace(no_sentinel.find("  0\n};"), 6, "};");
    EXPECT_THROW(libbeam_build::project_atoms(no_sentinel, header, bifs), std::runtime_error);
}
TEST(AtomGenerator, RejectsHolesDuplicatesAndOwnedNameCollisions) {
    auto changed = header;
    changed.replace(changed.find("make_atom(1)"), 12, "make_atom(3)");
    EXPECT_THROW(libbeam_build::project_atoms(atoms, changed, bifs), std::runtime_error);
    changed = header;
    changed.replace(changed.find("make_atom(1)"), 12, "make_atom(0)");
    EXPECT_THROW(libbeam_build::project_atoms(atoms, changed, bifs), std::runtime_error);
    changed = header;
    changed.replace(changed.find("am_true"), 7, "am_set_cookie");
    EXPECT_THROW(libbeam_build::project_atoms(atoms, changed, bifs), std::runtime_error);
}
} // namespace
