// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#pragma once
#include "generator_text.hpp"
#include <functional>

namespace libbeam_build {
inline std::string regex_adapt(const std::string& text, const std::string& pattern,
                              const std::function<std::string(const std::smatch&)>& replacement,
                              std::size_t expected) {
    const std::regex expression(pattern, std::regex::ECMAScript | std::regex::multiline);
    std::string output;
    std::size_t offset = 0, count = 0;
    for (std::sregex_iterator it(text.begin(), text.end(), expression), end; it != end; ++it) {
        const auto begin = static_cast<std::size_t>(it->position());
        output += text.substr(offset, begin - offset);
        output += replacement(*it);
        offset = begin + it->length();
        ++count;
    }
    if (count != expected) throw std::runtime_error("Changed dispatch boundary: " + pattern);
    return output + text.substr(offset);
}
inline void regex_adapt_exact(std::string& text, const std::string& pattern,
                             const std::string& replacement, std::size_t expected = 1) {
    text = regex_adapt(text, pattern, [&](const std::smatch& match) { return match.format(replacement); }, expected);
}
inline void replace_count(std::string& text, const std::string& before,
                          const std::string& after, std::size_t expected) {
    if (replace_all(text, before, after) != expected)
        throw std::runtime_error("Changed dispatch boundary: " + before);
}

// Count C braces, not braces inside comments, character constants or strings.
inline std::size_t c_block_end(const std::string& source, std::size_t begin) {
    if (begin >= source.size() || source[begin] != '{') throw std::runtime_error("Expected C block");
    std::size_t depth = 0;
    char quote = 0;
    bool line_comment = false, block_comment = false;
    for (std::size_t i = begin; i < source.size(); ++i) {
        const char c = source[i];
        const char next = i + 1 < source.size() ? source[i + 1] : '\0';
        if (line_comment) { if (c == '\n') line_comment = false; continue; }
        if (block_comment) { if (c == '*' && next == '/') { block_comment = false; ++i; } continue; }
        if (quote) {
            if (c == '\\') ++i;
            else if (c == quote) quote = 0;
            continue;
        }
        if (c == '/' && next == '/') { line_comment = true; ++i; continue; }
        if (c == '/' && next == '*') { block_comment = true; ++i; continue; }
        if (c == '\'' || c == '"') { quote = c; continue; }
        if (c == '{') ++depth;
        else if (c == '}' && --depth == 0) return i + 1;
    }
    throw std::runtime_error("Unbalanced generated C block");
}
inline std::string dispatch_case(const std::string& source, const std::string& name) {
    if (!std::regex_match(name, std::regex(R"([A-Za-z_]\w*)"))) throw std::runtime_error("Invalid instruction name");
    const std::regex pattern("\\bOpCase\\(" + name + R"(\):\s*\{)");
    std::smatch match;
    if (!std::regex_search(source, match, pattern)) throw std::runtime_error("Missing generated case: " + name);
    const auto begin = static_cast<std::size_t>(match.position());
    const auto brace = begin + match.length() - 1;
    return source.substr(begin, c_block_end(source, brace) - begin);
}
inline std::string dispatch_group(const std::string& source, const std::string& name) {
    const auto marker = source.find("OpCase(" + name + "):");
    if (marker == std::string::npos) throw std::runtime_error("Missing generated group: " + name);
    const auto start = source.rfind("\n{\n", marker);
    const auto end = source.find("\n}\n", marker);
    if (start == std::string::npos || end == std::string::npos)
        throw std::runtime_error("Missing group scope: " + name);
    return source.substr(start + 1, end + 2 - (start + 1));
}
inline bool contains(const std::string& body, const std::string& text) { return body.find(text) != std::string::npos; }
inline void remove_arithmetic_debug(std::string& body) {
    regex_replace_all(body, R"(#ifdef DEBUG\s*Eterm\* orig_htop = HTOP;\s*Eterm\* orig_stop = E;\s*#endif)", "");
    for (const char* text : {"DEBUG_SWAPOUT;", "DEBUG_SWAPIN;", "c_p->fcalls = FCALLS;",
         "FCALLS = c_p->fcalls;", "ASSERT(orig_htop == HTOP && orig_stop == E);", "ERTS_HOLE_CHECK(c_p);"})
        replace_all(body, text, "");
}
inline std::string fallible_equality(std::string body) {
    const std::regex pattern(R"(\b(eq|EQ)\()");
    std::sregex_iterator call(body.begin(), body.end(), pattern), end;
    if (call == end) return body;
    const std::smatch match = *call;
    if (++call != end) throw std::runtime_error("Multiple equality calls");
    const auto begin = static_cast<std::size_t>(match.position());
    const auto args_begin = begin + match.length();
    auto args_end = args_begin;
    unsigned depth = 1;
    while (depth && args_end < body.size()) {
        if (body[args_end] == '(') ++depth;
        else if (body[args_end] == ')') --depth;
        ++args_end;
    }
    if (depth) throw std::runtime_error("Unbalanced equality call");
    const auto branch = body.rfind("  if (", begin);
    if (branch == std::string::npos) throw std::runtime_error("Equality outside reviewed condition");
    const auto guard = match[1] == "eq" ? "!is_immed(src) && " : "";
    const auto prefix = std::string("  int lb_equal=0;\n  if (") + guard +
        "lb_term_equal(c_p->entry_module->space->domain, " + body.substr(args_begin, args_end - args_begin - 1) +
        ", &lb_equal)!=LB_ALLOC_OK) goto memory_failure;\n";
    return body.substr(0, branch) + prefix + body.substr(branch, begin - branch) + "lb_equal" + body.substr(args_end);
}
inline std::string fallible_arithmetic(std::string body) {
    const bool unary = contains(body, "erts_unary_minus");
    const std::string operation = contains(body, "erts_mixed_times") ? "times" : "plus";
    const auto native = unary ? std::string("erts_unary_minus") : "erts_mixed_" + operation;
    const auto owned = unary ? std::string("lb_unary_minus") : "lb_mixed_" + operation;
    const auto bif = unary ? std::string("BIF_sminus_1") : "BIF_s" + operation + "_2";
    if (!contains(body, native)) return body;
    remove_arithmetic_debug(body);
    regex_adapt_exact(body, "result = " + native + R"(\s*\(c_p, ([^;]+)\);)",
        "SWAPOUT; result = " + owned + "(c_p, $1); SWAPIN;\n"
        "      dst_ptr = REG_TARGET_PTR(dst); FCALLS -= (Sint)c_p->last_native_cost;\n"
        "      if (lb_alloc_domain_failure(c_p->entry_module->space->domain)==LB_ALLOC_WORK_LIMIT) goto work_failure;\n"
        "      if (c_p->status == LB_PROCESS_NO_MEMORY) goto memory_failure;\n"
        "      if (c_p->status == LB_PROCESS_LIMIT) goto resource_limit;\n"
        "      if (c_p->status == LB_PROCESS_ENVIRONMENT) goto environment_failure;\n"
        "      if (c_p->status == LB_PROCESS_EXCEPTION) goto find_func_info;");
    const auto error = "I = handle_error(c_p, I, reg, &BIF_TRAP_EXPORT(" + bif + ")->info.mfa);\n      goto post_error_handling;";
    replace_count(body, error, "goto find_func_info;", contains(body, "OpCase(i_increment_rWd)") ? 0 : 1);
    replace_all(body, "(Sint)increment_val << _TAG_IMMED1_SIZE", "(Sint)((Uint)increment_val << _TAG_IMMED1_SIZE)");
    return body;
}
inline std::string fallible_integer_division(std::string body) {
    const bool division = contains(body, "OpCase(i_int_div_jssd)");
    if (!division && !contains(body, "rem__execute:")) return body;
    const std::string operands = division ? "op1, op2" : "RemOp1, RemOp2";
    const std::string bif = division ? "BIF_intdiv_2" : "BIF_rem_2";
    remove_arithmetic_debug(body);
    replace_exactly_once(body, "succeeded = erts_int_div_rem(c_p, " + operands + ", &q, &r);",
        "SWAPOUT; succeeded = lb_int_div_rem(c_p, " + operands + ", &q, &r); SWAPIN;\n"
        "        dst_ptr = REG_TARGET_PTR(dst); FCALLS -= (Sint)c_p->last_native_cost;\n"
        "        if(lb_alloc_domain_failure(c_p->entry_module->space->domain)==LB_ALLOC_WORK_LIMIT) goto work_failure;\n"
        "        if(c_p->status==LB_PROCESS_NO_MEMORY) goto memory_failure;\n"
        "        if(c_p->status==LB_PROCESS_LIMIT) goto resource_limit;\n"
        "        if(c_p->status==LB_PROCESS_INVALID) goto malformed_code;");
    regex_adapt_exact(body, "I = handle_error\\(c_p, I, reg, &BIF_TRAP_EXPORT\\(" + bif +
        R"(\)->info.mfa\);\s*goto post_error_handling;)", "goto find_func_info;", 2);
    return body;
}
} // namespace libbeam_build
