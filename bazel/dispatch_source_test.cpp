// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#include "dispatch_source.hpp"
#include "dispatch_calls.hpp"
#include "libbeam/tests/test_process.hpp"
#include <fstream>
#include <gtest/gtest.h>
#include <iterator>

namespace {
std::string generated;
TEST(DispatchSource, CaseExtractionIgnoresQuotedAndCommentedBraces) {
    const std::string body = "OpCase(test_x): {\n  f(\"}\\\"{\"); char c='}'; /* } { */\n  // }\n  if (x) { f(); }\n}";
    EXPECT_EQ(libbeam_build::dispatch_case("prefix\n" + body + "\nother();", "test_x"), body);
    EXPECT_THROW(libbeam_build::dispatch_case("OpCase(test_x): { /* }", "test_x"), std::runtime_error);
    EXPECT_THROW(libbeam_build::dispatch_case(body, "missing"), std::runtime_error);
    EXPECT_THROW(libbeam_build::dispatch_case(body, "test.*"), std::runtime_error);
}
TEST(DispatchSource, SharedScopeAndEpilogueRemainIntact) {
    const std::string group = "{\n int local;\n OpCase(one): { local=1; } goto finish;\n OpCase(two): { local=2; }\nfinish: consume(local);\n}";
    EXPECT_EQ(libbeam_build::dispatch_group("prefix\n" + group + "\nsuffix", "one"), group);
    EXPECT_THROW(libbeam_build::dispatch_group("OpCase(one): {}", "one"), std::runtime_error);
}
TEST(DispatchSource, EqualityChecksScratchFailureBeforeTakingBranch) {
    const auto result = libbeam_build::fallible_equality("  if (!is_immed(src) && eq(src, literal)) goto matched;");
    const auto failure = result.find("goto memory_failure;");
    const auto branch = result.find("  if (!is_immed(src) && lb_equal)");
    ASSERT_NE(failure, std::string::npos);
    ASSERT_NE(branch, std::string::npos);
    EXPECT_LT(failure, branch);
    EXPECT_NE(result.find("lb_term_equal(c_p->entry_module->space->domain, src, literal, &lb_equal)"), std::string::npos);
    EXPECT_THROW(libbeam_build::fallible_equality("eq(a,b);"), std::runtime_error);
    EXPECT_THROW(libbeam_build::fallible_equality("  if (EQ(a,b) || EQ(c,d)) {}"), std::runtime_error);
}
TEST(DispatchSource, RealArithmeticReacquiresDestinationAfterPossibleCollection) {
    const auto body = libbeam_build::fallible_arithmetic(libbeam_build::dispatch_case(generated, "i_times_jssd"));
    const auto call = body.find("SWAPOUT; result = lb_mixed_times");
    const auto reload = body.find("dst_ptr = REG_TARGET_PTR(dst);", call);
    ASSERT_NE(call, std::string::npos);
    ASSERT_NE(reload, std::string::npos);
    EXPECT_GT(reload, call);
    EXPECT_NE(body.find("goto work_failure;"), std::string::npos);
    EXPECT_NE(body.find("goto memory_failure;"), std::string::npos);
    EXPECT_NE(body.find("goto resource_limit;"), std::string::npos);
    EXPECT_NE(body.find("goto environment_failure;"), std::string::npos);
    EXPECT_EQ(body.find("erts_mixed_times"), std::string::npos);
    EXPECT_EQ(body.find("handle_error("), std::string::npos);
}
TEST(DispatchSource, RealDivisionRetainsBothGuestErrorBranchesAndOwnedFailureCauses) {
    const auto body = libbeam_build::fallible_integer_division(libbeam_build::dispatch_case(generated, "i_int_div_jssd"));
    EXPECT_NE(body.find("lb_int_div_rem(c_p, op1, op2, &q, &r)"), std::string::npos);
    EXPECT_EQ(body.find("erts_int_div_rem"), std::string::npos);
    EXPECT_EQ(body.find("handle_error("), std::string::npos);
    EXPECT_NE(body.find("goto malformed_code;"), std::string::npos);
    EXPECT_THROW(libbeam_build::fallible_integer_division("OpCase(i_int_div_jssd): {}"), std::runtime_error);
}
TEST(DispatchCalls, RealApplyVariantsUseOwnedStateAndAccountFailures) {
    for (const char* name : {"i_apply", "i_apply_last_Q", "i_apply_only", "apply_t", "apply_last_tQ"}) {
        SCOPED_TRACE(name);
        const auto body = libbeam_build::owned_apply(libbeam_build::dispatch_case(generated, name));
        EXPECT_NE(body.find("c_p->heap_reserved=0"), std::string::npos);
        EXPECT_NE(body.find("goto work_failure;"), std::string::npos);
        EXPECT_NE(body.find("goto execution_refused;"), std::string::npos);
        EXPECT_EQ(body.find("save_calls("), std::string::npos);
        EXPECT_EQ(body.find("handle_error("), std::string::npos);
    }
}
TEST(DispatchCalls, ApplyFunVariantsRetainOwnedFailureBoundaries) {
    for (const char* name : {"i_apply_fun", "i_apply_fun_last_Q", "i_apply_fun_only"}) {
        const auto body = libbeam_build::owned_apply_fun(libbeam_build::dispatch_case(generated, name));
        EXPECT_NE(body.find("next=lb_apply_fun(c_p)"), std::string::npos);
        for (const char* failure : {"work_failure", "resource_limit", "memory_failure", "execution_refused", "malformed_code"})
            EXPECT_NE(body.find(std::string("goto ")+failure+";"), std::string::npos);
        EXPECT_EQ(body.find("apply_fun(c_p, x(0)"), std::string::npos);
    }
    EXPECT_THROW(libbeam_build::owned_apply_fun("OpCase(i_apply_fun): {}"), std::runtime_error);
}
TEST(DispatchCalls, RealClosuresRetainCodeAndCallsClearReservation) {
    const auto creation = libbeam_build::owned_make_fun(libbeam_build::dispatch_case(generated, "i_make_fun3_Fdtt"));
    EXPECT_NE(creation.find("lb_fun_retain(c_p,fe)"), std::string::npos);
    EXPECT_NE(creation.find("goto work_failure;"), std::string::npos);
    EXPECT_EQ(creation.find("erts_get_fun_mfa"), std::string::npos);
    for (const char* name : {"i_call_fun_t", "i_call_fun_last_tQ"}) {
        const auto body = libbeam_build::owned_call_fun(libbeam_build::dispatch_case(generated, name));
        EXPECT_NE(body.find("c_p->heap_reserved=0; next=lb_fun_call"), std::string::npos);
        EXPECT_NE(body.find("goto memory_failure;"), std::string::npos);
        EXPECT_NE(body.find("goto execution_refused;"), std::string::npos);
    }
}
TEST(DispatchCalls, BifFailuresCannotTakeTheGuestGuardFailurePath) {
    for (const char* name : {"i_bif1_sfbd", "i_bif1_body_sbd"}) {
        const auto body = libbeam_build::owned_bif_call(libbeam_build::dispatch_group(generated, name));
        EXPECT_NE(body.find("Eterm tmp_reg[3]={NIL,NIL,NIL}"), std::string::npos);
        EXPECT_NE(body.find("goto capability_denied;"), std::string::npos);
        EXPECT_NE(body.find("goto work_failure;"), std::string::npos);
        EXPECT_NE(body.find("dst_ptr=REG_TARGET_PTR(dst)"), std::string::npos);
        EXPECT_EQ(body.find("handle_error("), std::string::npos);
    }
}
TEST(DispatchCalls, SendWritableIdentityAndNodeUseOwnedBoundaries) {
    const auto send = libbeam_build::owned_send(libbeam_build::dispatch_case(generated, "send"));
    EXPECT_NE(send.find("goto capability_denied;"), std::string::npos);
    EXPECT_NE(send.find("FCALLS-=1+(Sint)c_p->last_native_cost"), std::string::npos);
    const auto writable = libbeam_build::fallible_writable_init(libbeam_build::dispatch_case(generated, "bs_init_writable"));
    EXPECT_NE(writable.find("lb_writable_init_term"), std::string::npos);
    EXPECT_LT(writable.find("x(0)=lb_binary;"), writable.find("if (FCALLS<=0) goto context_switch3;"));
    for (const char* name : {"self_x", "self_y"}) {
        const auto body = libbeam_build::fallible_self(libbeam_build::dispatch_case(generated, name));
        EXPECT_NE(body.find("lb_process_identity(c_p,&identity)"), std::string::npos);
        EXPECT_NE(body.find("goto memory_failure;"), std::string::npos);
    }
    for (const char* name : {"node_x", "node_y"}) {
        const auto body = libbeam_build::owned_node(libbeam_build::dispatch_case(generated, name));
        EXPECT_EQ(body.find("erts_this_node"), std::string::npos);
        EXPECT_NE(body.find("c_p->entry_module->space->node_name"), std::string::npos);
    }
}
} // namespace
int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    if (argc != 4) return 2;
    for (int i = 1; i < argc; ++i) {
        std::ifstream input(libbeam_test::runfile(argv[i]), std::ios::binary);
        if (!input) return 2;
        if (i != 1) generated += '\n';
        generated.append(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    }
    return RUN_ALL_TESTS();
}
