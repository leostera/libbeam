// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#include "transform_projection.hpp"
#include <gtest/gtest.h>

namespace {
std::string engine(const std::string& body) {
    return "int erts_transform_engine(LoaderState* st) {\n  switch (opcode) {\n" + body +
        "    default: ASSERT(0); return TE_FAIL;\n  }\n}\nconst GenOpEntry gen_opc[] = {};\n";
}
TEST(TransformProjection, UnknownDependencyRefusesWholeCaseRatherThanSkippingRule) {
    const auto result = libbeam_build::project_transform(engine(
        "    case 1: /* unsafe */\n      /* Call predicate not_admitted */\n      forbidden_effect();\n      return TE_OK;\n"));
    EXPECT_NE(result.find("    case 1: /* unsafe */\n      return TE_UNSUPPORTED;"), std::string::npos);
    EXPECT_EQ(result.find("forbidden_effect"), std::string::npos);
    EXPECT_EQ(result.find("return TE_FAIL"), std::string::npos);
    EXPECT_NE(result.find("default: return TE_UNSUPPORTED;"), std::string::npos);
}
TEST(TransformProjection, AdmittedCasesAndCompletePredicateBodiesArePreserved) {
    const std::string helper = "static int never(void) {\n    return 0;\n}";
    const auto result = libbeam_build::project_transform(helper + "\n" + engine(
        "    case 2: /* safe */\n      /* Call predicate never */\n      if (never()) return TE_FAIL;\n      return TE_OK;\n"));
    EXPECT_NE(result.find(helper), std::string::npos);
    EXPECT_NE(result.find("if (never()) return TE_FAIL;\n      return TE_OK;"), std::string::npos);
}
TEST(TransformProjection, MissingStructureAndHelpersAreErrors) {
    EXPECT_THROW(libbeam_build::project_transform(""), std::runtime_error);
    EXPECT_THROW(libbeam_build::project_transform(engine("")), std::runtime_error);
    EXPECT_THROW(libbeam_build::project_transform(engine(
        "    case 2: /* safe */\n      /* Call predicate never */\n      return TE_OK;\n")), std::runtime_error);
}
TEST(TransformProjection, AllocationsAndGeneratorErrorsRemainFallible) {
    std::string body =
        "BeamOp* new_instr = beamopallocator_new_op(&st->op_allocator);\n"
        "BeamOp* new_instr = allocate(st, instr);\n"
        "beamopallocator_free_op(&st->op_allocator, first);\n"
        "instr->a = erts_alloc(ERTS_ALC_T_LOADER_TMP, instr->arity * sizeof(BeamOpArg));\n";
    libbeam_build::adapt_transform_engine(body);
    EXPECT_NE(body.find("if (!new_instr) return TE_NO_MEMORY;"), std::string::npos);
    EXPECT_NE(body.find("if (st->program->error.status != LB_BEAM_OK) return TE_NO_MEMORY;"), std::string::npos);
    EXPECT_NE(body.find("if (!instr->a) return TE_NO_MEMORY;"), std::string::npos);
    EXPECT_NE(body.find("lb_load_free_op(st, first)"), std::string::npos);
    EXPECT_EQ(body.find("erts_alloc"), std::string::npos);
    EXPECT_THROW(libbeam_build::adapt_transform_generator("allocate", "erts_alloc(NEW_KIND, 1);"), std::runtime_error);
}
TEST(TransformProjection, ImportIndexIsCheckedBeforeNarrowingAndCatalogIsImmutable) {
    std::string body = "int i = instr->a[2].val;\nBifEntry *entry = st->bif_imports[i];\n";
    libbeam_build::adapt_transform_engine(body);
    EXPECT_NE(body.find("Uint i; TR_REQUIRE(instr->a[2].type == TAG_u && instr->a[2].val >= 0); i = (Uint)instr->a[2].val;"), std::string::npos);
    EXPECT_NE(body.find("const BifEntry *entry"), std::string::npos);
}
TEST(TransformProjection, ReviewedSpecialBoundariesCannotSilentlyDisappear) {
    EXPECT_THROW(libbeam_build::adapt_transform_generator("get", "changed()"), std::runtime_error);
    EXPECT_THROW(libbeam_build::adapt_transform_generator("create_bin", "changed()"), std::runtime_error);
    EXPECT_THROW(libbeam_build::adapt_transform_generator("make_fun3", "changed()"), std::runtime_error);
    EXPECT_THROW(libbeam_build::adapt_transform_generator("bs_match", "changed()"), std::runtime_error);
}
} // namespace
