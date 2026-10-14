// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#pragma once
#include "dispatch_source.hpp"

namespace libbeam_build {
inline bool has_case(const std::string& body, const std::string& names) {
    return std::regex_search(body, std::regex("OpCase\\((?:" + names + ")\\):"));
}
inline std::string fallible_writable_init(std::string body) {
    if (!contains(body, "OpCase(bs_init_writable):")) return body;
    replace_exactly_once(body, "  HEAVY_SWAPOUT;\n  x(0) = erts_bs_init_writable(c_p, x(0));\n  HEAVY_SWAPIN;",
        "  Eterm lb_binary;\n  SWAPOUT;\n"
        "  LbWritableStatus lb_status=lb_writable_init_term(c_p,x(0),&lb_binary);\n"
        "  SWAPIN;\n  FCALLS-=(Sint)c_p->last_native_cost;\n"
        "  if (lb_status==LB_WRITABLE_NO_MEMORY) goto memory_failure;\n"
        "  if (lb_status==LB_WRITABLE_LIMIT) goto resource_limit;\n"
        "  if (lb_status!=LB_WRITABLE_OK) goto malformed_code;\n"
        "  x(0)=lb_binary;");
    replace_exactly_once(body, "  GotoPF(next_pf);", "  if (FCALLS<=0) goto context_switch3;\n  GotoPF(next_pf);");
    return body;
}
inline std::string owned_send(std::string body) {
    if (!contains(body, "OpCase(send):")) return body;
    replace_exactly_once(body, "PRE_BIF_SWAPOUT(c_p);", "SWAPOUT;");
    for (const char* text : {"c_p->fcalls = FCALLS - 1;", "ERTS_VERIFY_UNUSED_TEMP_ALLOC(c_p);",
         "ERTS_REQ_PROC_MAIN_LOCK(c_p);", "PROCESS_MAIN_CHK_LOCKS(c_p);"})
        replace_exactly_once(body, text, "");
    replace_exactly_once(body, "HTOP = HEAP_TOP(c_p);", "SWAPIN;");
    replace_exactly_once(body, "FCALLS = c_p->fcalls;",
        "FCALLS-=1+(Sint)c_p->last_native_cost;\n  if(c_p->status==LB_PROCESS_NO_MEMORY) goto memory_failure;\n"
        "  if(c_p->status==LB_PROCESS_LIMIT) goto resource_limit;\n"
        "  if(c_p->status==LB_PROCESS_REFUSED) goto execution_refused;\n"
        "  if(c_p->status==LB_PROCESS_CAPABILITY_DENIED) goto capability_denied;\n"
        "  if(c_p->status==LB_PROCESS_ENVIRONMENT) goto environment_failure;");
    replace_all(body, "  GotoPF(next_pf);", "  if(FCALLS<=0) goto context_switch3;\n  GotoPF(next_pf);");
    return body;
}
inline std::string owned_bif_call(std::string body) {
    if (!contains(body, "i_bif__call:") && !contains(body, "i_bif_body__call:")) return body;
    replace_all(body, "Eterm tmp_reg[3];", "Eterm tmp_reg[3]={NIL,NIL,NIL};");
    regex_replace_all(body, R"(#ifdef DEBUG\s*Eterm\* orig_htop = HTOP;\s*Eterm\* orig_stop = E;\s*#endif)",
        "Eterm* orig_htop=HTOP; Eterm* orig_stop=E;");
    replace_all(body, "ASSERT(orig_htop == HTOP && orig_stop == E);",
        "if(bf!=lb_bif_abs_1 && bf!=lb_bif_floor_1 && bf!=lb_bif_ceil_1 && bf!=lb_bif_trunc_1 && bf!=lb_bif_round_1 && bf!=lb_bif_float_1 && (orig_htop!=HTOP || orig_stop!=E)) goto malformed_code;\n"
        "      dst_ptr=REG_TARGET_PTR(dst);");
    for (const char* text : {"c_p->fcalls = FCALLS;", "FCALLS = c_p->fcalls;", "ERTS_DBG_CHK_REDS(c_p, FCALLS);",
         "ASSERT(!ERTS_PROC_IS_EXITING(c_p));", "ASSERT(!ERTS_PROC_IS_EXITING(c_p) || is_non_value(result));",
         "ERTS_CHK_MBUF_SZ(c_p);", "ERTS_HOLE_CHECK(c_p);"}) replace_all(body, text, "");
    replace_all(body, "DEBUG_SWAPOUT;", "SWAPOUT;");
    replace_all(body, "DEBUG_SWAPIN;", "SWAPIN;");
    const std::string call = "result = (*bf)(c_p, tmp_reg, I);";
    replace_exactly_once(body, call, "c_p->last_native_cost=0; " + call + "\n"
        "      FCALLS-=(Sint)c_p->last_native_cost;\n"
        "      if(lb_alloc_domain_failure(c_p->entry_module->space->domain)==LB_ALLOC_WORK_LIMIT) goto work_failure;\n"
        "      if(c_p->status==LB_PROCESS_REFUSED) goto execution_refused;\n"
        "      if(c_p->status==LB_PROCESS_CAPABILITY_DENIED) goto capability_denied;\n"
        "      if(c_p->status==LB_PROCESS_NO_MEMORY) goto memory_failure;\n"
        "      if(c_p->status==LB_PROCESS_LIMIT) goto resource_limit;\n"
        "      if(c_p->status==LB_PROCESS_ENVIRONMENT) goto environment_failure;\n"
        "      if(c_p->status==LB_PROCESS_INVALID) goto malformed_code;");
    replace_all(body, "SET_I((BeamInstr *) I+4);", "SET_I((BeamInstr *) I+4); if(FCALLS<=0) goto context_switch3;");
    replace_count(body, "I = handle_error(c_p, I, reg, ubif2mfa((void *) bf));\n    goto post_error_handling;",
        "goto find_func_info;", contains(body, "i_bif_body__call:") ? 1 : 0);
    return body;
}
inline std::string owned_make_fun(std::string body) {
    if (!contains(body, "OpCase(i_make_fun3_Fdtt):")) return body;
    replace_exactly_once(body, "  funp = (ErlFunThing*)HTOP;",
        "  if(!lb_work_domain_charge(c_p->entry_module->space->domain,num_free+lb_work_bulk_units(num_free))) goto work_failure;\n"
        "  LbAllocStatus held=lb_fun_retain(c_p,fe);\n"
        "  FCALLS-=1+num_free;\n"
        "  if(held==LB_ALLOC_NO_MEMORY) goto memory_failure;\n"
        "  if(held!=LB_ALLOC_OK || (Uint)(E-HTOP)<ERL_FUN_SIZE+(Uint)num_free+S_RESERVED) goto malformed_code;\n"
        "  funp = (ErlFunThing*)HTOP;");
    replace_exactly_once(body, "const ErtsCodeMFA *mfa = erts_get_fun_mfa(fe, erts_active_code_ix());",
        "LbMFA actual_mfa; memcpy(&actual_mfa,fe->dispatch.addresses[0]-sizeof(LbMFA)/sizeof(BeamInstr),sizeof(actual_mfa));\n"
        "    const LbMFA *mfa=&actual_mfa;");
    replace_exactly_once(body, "ASSERT(tb((tmp_packed1>>BEAM_TIGHT_SHIFT)&BEAM_TIGHT_MASK) == fe->arity);",
        "ASSERT(tb((tmp_packed1>>BEAM_TIGHT_SHIFT)&BEAM_TIGHT_MASK) == (Uint)fe->arity);");
    const std::string completion = "  Goto(*I);ASSERT(!\"Fell through 'i_make_fun3' (-no_next)\");";
    replace_exactly_once(body, completion, "  if(FCALLS<=0) goto context_switch3;\n" + completion);
    return body;
}
inline std::string owned_apply(std::string body) {
    if (!has_case(body, "i_apply|i_apply_last_Q|i_apply_only|apply_t|apply_last_tQ")) return body;
    body = regex_adapt(body, R"(HEAVY_SWAPOUT;\s+ep = (fixed_apply|apply)\(c_p, reg, ([^\n]*?)\);\s+HEAVY_SWAPIN;)",
        [](const std::smatch& match) {
            const std::string arguments = match[2];
            const auto helper = match[1] == "fixed_apply" ? "lb_fixed_apply(c_p," + arguments.substr(0, arguments.find(", ")) + ")" : "lb_apply(c_p)";
            return "SWAPOUT; c_p->heap_reserved=0; ep=" + helper + "; SWAPIN;\n"
                "    FCALLS-=(Sint)c_p->last_native_cost;\n"
                "    if(c_p->status==LB_PROCESS_WORK_LIMIT) goto work_failure;\n"
                "    if(c_p->status==LB_PROCESS_LIMIT) goto resource_limit;\n"
                "    if(c_p->status==LB_PROCESS_NO_MEMORY) goto memory_failure;\n"
                "    if(c_p->status==LB_PROCESS_REFUSED) goto execution_refused;\n"
                "    if(c_p->status==LB_PROCESS_INVALID) goto malformed_code;";
        }, 1);
    regex_adapt_exact(body, R"(static ErtsCodeMFA apply3_mfa = \{am_erlang, am_apply, 3\};\s*I = handle_error\(c_p, I, reg, &apply3_mfa\);\s*goto post_error_handling;)",
        "c_p->current=&c_p->entry_module->space->apply_export.info.mfa;\n        goto find_func_info;");
    regex_adapt_exact(body, R"( else if \(ERTS_PROC_GET_SAVED_CALLS_BUF\(c_p\)\) \{\s*save_calls\(c_p, ep\);\s*\})", "");
    return body;
}
inline std::string owned_call_fun(std::string body) {
    if (!has_case(body, "i_call_fun_(?:t|last_tQ)")) return body;
    regex_adapt_exact(body, R"(HEAVY_SWAPOUT;\s+next = call_fun\(c_p, ([^\n]*?), reg, THE_NON_VALUE\);\s+HEAVY_SWAPIN;)",
        "SWAPOUT; c_p->heap_reserved=0; next=lb_fun_call(c_p,$1); SWAPIN;\n"
        "FCALLS-=(Sint)c_p->last_native_cost;\n"
        "if(c_p->status==LB_PROCESS_NO_MEMORY) goto memory_failure;\n"
        "if(c_p->status==LB_PROCESS_REFUSED) goto execution_refused;\n"
        "if(c_p->status==LB_PROCESS_INVALID) goto malformed_code;");
    return body;
}
inline std::string fallible_self(std::string body) {
    if (!has_case(body, "self_[xy]")) return body;
    replace_all(body, "  BeamInstr next_pf",
        "  Eterm identity;\n"
        "  LbProcessStatus identity_status=lb_process_identity(c_p,&identity);\n"
        "  FCALLS-=(Sint)c_p->last_native_cost;\n"
        "  if(identity_status==LB_PROCESS_NO_MEMORY) goto memory_failure;\n"
        "  if(identity_status==LB_PROCESS_LIMIT) goto resource_limit;\n"
        "  if(identity_status!=LB_PROCESS_READY) goto malformed_code;\n"
        "  BeamInstr next_pf");
    replace_exactly_once(body, "c_p->common.id", "identity");
    replace_exactly_once(body, "GotoPF(next_pf);", "if(FCALLS<=0) goto context_switch3; GotoPF(next_pf);");
    return body;
}
inline std::string owned_node(std::string body) {
    if (has_case(body, "node_[xy]"))
        replace_exactly_once(body, "erts_this_node->sysname", "c_p->entry_module->space->node_name");
    return body;
}
} // namespace libbeam_build
