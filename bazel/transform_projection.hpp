// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#pragma once
#include "generator_text.hpp"
#include <set>
#include <vector>

namespace libbeam_build {
inline const std::set<std::string> pure_predicates = {
    "distinct", "equal", "independent_moves", "is_offset", "negation_is_small", "never",
    "succ", "succ3", "succ4", "fixed_size_values", "floats_or_bignums", "mixed_types",
    "use_jump_tab", "is_empty_map", "is_small_map_literal_keys", "literal_is_map", "map_key_sort",
};
inline const std::set<std::string> owned_predicates = {
    "needs_nif_padding", "is_heavy_bif", "smp_already_locked", "smp_mark_target_label",
};
inline const std::set<std::string> transform_generators = {
    "allocate", "allocate_heap", "init_yregs", "select_tuple_arity", "increment", "increment_from_minus", "plus_from_minus", "create_bin",
    "call_fun2", "make_fun3", "is_function2", "get", "element", "literal_timeout",
    "literal_timeout_locked", "select_val", "bs_match", "const_select_val", "jump_tab",
    "select_literals", "split_values",
};

inline std::string adapt_predicate(const std::string& name, std::string body) {
    if (name == "fixed_size_values") {
        replace_all(body, "beamfile_get_literal(&S->beam, Rest[i].val)", "load_literal(S, Rest[i].val)");
        replace_all(body, "is_float(", "lb_transform_is_float(");
    }
    if (name == "use_jump_tab")
        replace_all(body, "return max - min <= Size.val;", "return (Uint)max - (Uint)min <= (Uint)Size.val;");
    if (name == "is_empty_map") {
        replace_all(body, "beamfile_get_literal(&S->beam, Lit.val)", "load_literal(S, Lit.val)");
        replace_all(body, "is_flatmap(term) && flatmap_get_size(flatmap_val(term)) == 0",
                    "lb_is_flatmap(term) && ((LbFlatMap *)boxed_val(term))->size == 0");
    }
    if (name == "is_small_map_literal_keys") replace_all(body, "MAP_SMALL_MAP_LIMIT", "LB_FLATMAP_MAX_SIZE");
    if (name == "literal_is_map") {
        replace_all(body, "beamfile_get_literal(&S->beam, Lit.val)", "load_literal(S, Lit.val)");
        replace_all(body, "is_map(term)", "lb_is_flatmap(term)");
    }
    if (name == "map_key_sort")
        replace_all(body, "beam_load_map_key_sort(S, Size, Rest)", "lb_load_map_key_sort(S, Size, Rest)");
    return body;
}

inline void adapt_create_bin(std::string& body) {
    replace_exactly_once(body, "ASSERT(src[0].type = TAG_a);", "ASSERT(src[0].type == TAG_a);");
    replace_exactly_once(body, "op->a = erts_alloc(ERTS_ALC_T_LOADER_TMP, op->arity * sizeof(BeamOpArg));",
        "op->a = lb_palloc(S->program, op->arity, sizeof(BeamOpArg));\n  if (!op->a) return NULL;");
    replace_exactly_once(body, "beamfile_get_literal(&S->beam, Flags.val)", "load_literal(S, Flags.val)");
    regex_replace_all(body, R"(\bBSC_(\w+))", "LB_BSC_$1");
}
inline void adapt_make_fun(std::string& body) {
    replace_exactly_once(body, "S->beam.lambdas.count", "S->program->lambda_count");
    replace_exactly_once(body, "S->beam.lambdas.entries", "S->program->lambdas");
    replace_exactly_once(body, "beamfile_add_literal(&S->beam, make_fun((Eterm*)funp), 0)",
                         "load_fun_literal(S, (size_t)Idx.val, make_fun((Eterm*)funp))");
    replace_exactly_once(body, "erts_alloc(ERTS_ALC_T_LOADER_TMP, op->arity * sizeof(BeamOpArg))",
                         "lb_palloc(S->program, op->arity, sizeof(BeamOpArg))");
    replace_all(body, "op->a = lb_palloc(S->program, op->arity, sizeof(BeamOpArg));",
        "op->a = lb_palloc(S->program, op->arity, sizeof(BeamOpArg));\n  if (!op->a) return NULL;");
}
inline void adapt_select_val(std::string& body) {
    replace_all(body, "int arity = Size.val + 3;",
        "int arity;\n  if (Size.type!=TAG_u || Size.val<2 || Size.val%2 || Size.val>MAX_REG*2) { S->program->error.status=LB_BEAM_LIMIT; return NULL; }\n  arity = Size.val + 3;");
    replace_all(body, "erts_alloc(ERTS_ALC_T_LOADER_TMP, op->arity * sizeof(BeamOpArg))",
        "lb_palloc(S->program, op->arity, sizeof(BeamOpArg))");
    replace_all(body, "op->a[0] = Src;", "if (!op->a) return NULL;\n    op->a[0] = Src;");
    replace_all(body, "(BeamOpArg *) erts_alloc(ERTS_ALC_T_LOADER_TMP, sizeof(BeamOpArg)*(arity-2*align))",
        "lb_palloc(S->program, arity-2*align, sizeof(BeamOpArg))");
    replace_all(body, "  for (i = 3; i < arity - 2*align; i++)",
        "  if (!tmp) return NULL;\n  for (i = 3; i < arity - 2*align; i++)");
    replace_all(body, "erts_free(ERTS_ALC_T_LOADER_TMP, (void *) tmp);", "/* Owned temporary preparation arena. */");
}
inline void adapt_select_tuple(std::string& body) {
    replace_all(body, "erts_alloc(ERTS_ALC_T_LOADER_TMP, op->arity * sizeof(BeamOpArg))",
        "lb_palloc(S->program, op->arity, sizeof(BeamOpArg))");
    replace_all(body, "op->next = NULL;\n    op->a[0]", "if (!op->a) return NULL;\n    op->next = NULL;\n    op->a[0]");
    replace_all(body, "op->next = NULL;\n  op->a[0]", "if (!op->a) return NULL;\n  op->next = NULL;\n  op->a[0]");
    replace_all(body, "erts_alloc(ERTS_ALC_T_LOADER_TMP, sizeof(BeamOpArg)*(arity-2*align))",
        "lb_palloc(S->program, arity-2*align, sizeof(BeamOpArg))");
    replace_all(body, "  for (i = 3; i < arity - 2*align; i+=2)",
        "  if (!tmp) return NULL;\n  for (i = 3; i < arity - 2*align; i+=2)");
    replace_all(body, "erts_free(ERTS_ALC_T_LOADER_TMP, (void *) tmp);",
        "/* Temporary argument arrays belong to the preparation arena. */");
}
inline std::string adapt_transform_generator(const std::string& name, std::string body) {
    regex_replace_all(body, R"((\w+) = beamopallocator_new_op\(&\(S\)->op_allocator\);)",
        "$1 = lb_load_new_op(S);\n  if (!$1) return NULL;");
    if (name == "create_bin") adapt_create_bin(body);
    if (name == "get")
        replace_exactly_once(body, "op->a[1].val = (BeamInstr) erts_pd_make_hx(key_term);",
            "if (!dictionary_hash(S,key_term,&op->a[1].val)) return NULL;");
    if (name == "is_function2")
        replace_exactly_once(body, "if (Arity.val > MAX_ARG)", "if ((Uint)Arity.val > MAX_ARG)");
    if (name == "make_fun3") adapt_make_fun(body);
    if (name == "const_select_val" || name == "jump_tab" || name == "select_literals" || name == "split_values") {
        replace_all(body, "beamfile_get_literal(&S->beam, Src.val)", "load_literal(S, Src.val)");
        replace_all(body, "beamfile_get_literal(&S->beam, Rest[i].val)", "load_literal(S, Rest[i].val)");
        replace_all(body, "eq(term, expr)", "lb_load_literal_equal(S,term,expr)");
        replace_all(body, "beam_load_new_label(S)", "lb_load_new_label(S)");
        regex_replace_all(body, R"((op(?:1|2)?->a) = erts_alloc\(ERTS_ALC_T_LOADER_TMP, ([^;]+)\);)",
            "$1 = lb_palloc(S->program, 1, $2);\n  if (!$1) return NULL;");
    }
    if (name == "bs_match") {
        replace_all(body, "beamfile_get_literal(&S->beam, Flags.val)", "load_literal(S, Flags.val)");
        replace_all(body, "erts_extracted_bitstring_size(size)", "lb_match_extracted_words(size)");
        const std::regex fatal(R"rx(erts_exit\(ERTS_ERROR_EXIT, "loading bs_match in %T:%T/%d: no instructions loaded",\s*S->module, S->function, S->arity\);)rx");
        if (std::distance(std::sregex_iterator(body.begin(), body.end(), fatal), std::sregex_iterator()) != 1)
            throw std::runtime_error("Changed bs_match fatal boundary");
        body = std::regex_replace(body, fatal, "S->program->error.status=LB_BEAM_BAD_FORMAT; return NULL;");
    }
    if (name == "select_val") adapt_select_val(body);
    if (name == "select_tuple_arity") adapt_select_tuple(body);
    reject_unowned_allocation(body);
    return body;
}

inline void adapt_transform_engine(std::string& result) {
    replace_all(result, "BeamOp* new_instr = beamopallocator_new_op(&st->op_allocator);",
        "BeamOp* new_instr = lb_load_new_op(st);\n      if (!new_instr) return TE_NO_MEMORY;");
    replace_all(result, "beamopallocator_free_op(&st->op_allocator, first);", "lb_load_free_op(st, first);");
    std::string names;
    for (const auto& name : transform_generators) { if (!names.empty()) names += '|'; names += name; }
    regex_replace_all(result, "(BeamOp\\* new_instr = (?:" + names + R"()\([^;\n]*\);))",
        "$1\n      if (st->program->error.status != LB_BEAM_OK) return TE_NO_MEMORY;");
    regex_replace_all(result, R"(erts_alloc\(ERTS_ALC_T_LOADER_TMP,\s*instr->arity \* sizeof\(BeamOpArg\)\))",
        "lb_palloc(st->program, instr->arity, sizeof(BeamOpArg))");
    replace_all(result, "instr->a = lb_palloc(st->program, instr->arity, sizeof(BeamOpArg));",
        "instr->a = lb_palloc(st->program, instr->arity, sizeof(BeamOpArg));\n    if (!instr->a) return TE_NO_MEMORY;");
    replace_all(result, "sys_memcpy", "memcpy");
    replace_all(result, "ASSERT(", "TR_REQUIRE(");
    replace_all(result, "BifEntry *entry = st->bif_imports[i];", "const BifEntry *entry = st->bif_imports[i];");
    regex_replace_all(result, R"(int i = (instr->a\[\d+\])\.val;)",
        "Uint i; TR_REQUIRE($1.type == TAG_u && $1.val >= 0); i = (Uint)$1.val;");
    reject_unowned_allocation(result);
}

inline std::string project_transform(const std::string& text) {
    const auto begin = text.find("int erts_transform_engine(");
    const auto end = text.find("const GenOpEntry gen_opc[]", begin);
    if (begin == std::string::npos || end == std::string::npos)
        throw std::runtime_error("Missing upstream transform engine boundary");
    std::string engine = text.substr(begin, end - begin);
    const auto finish = engine.rfind("    default: ASSERT(0); return TE_FAIL;");
    if (finish == std::string::npos) throw std::runtime_error("Missing transform default boundary");
    const std::regex clause(R"(^    case (\d+): /\* ([^\n]+) \*/)", std::regex::ECMAScript | std::regex::multiline);
    std::vector<std::size_t> starts;
    for (std::sregex_iterator it(engine.begin(), engine.end(), clause), last; it != last; ++it)
        starts.push_back(static_cast<std::size_t>(it->position()));
    if (starts.empty() || starts.back() >= finish) throw std::runtime_error("Missing transform cases");
    auto result = engine.substr(0, starts.front());
    replace_exactly_once(result, "int erts_transform_engine(", "static int lb_transform_engine(");
    const std::regex dependency(R"(Call (?:predicate|generator) (\w+))");
    std::set<std::string> used;
    for (std::size_t i = 0; i < starts.size(); ++i) {
        const auto next = i + 1 == starts.size() ? finish : starts[i + 1];
        const auto body = engine.substr(starts[i], next - starts[i]);
        std::set<std::string> dependencies;
        bool missing = false;
        for (std::sregex_iterator it(body.begin(), body.end(), dependency), last; it != last; ++it) {
            const auto name = (*it)[1].str();
            dependencies.insert(name);
            missing |= !pure_predicates.count(name) && !owned_predicates.count(name) && !transform_generators.count(name);
        }
        if (i) result += '\n';
        if (missing) result += body.substr(0, body.find('\n')) + "\n      return TE_UNSUPPORTED;\n";
        else { result += body; used.insert(dependencies.begin(), dependencies.end()); }
    }
    result += "    default: return TE_UNSUPPORTED;\n  }\n}\n";
    adapt_transform_engine(result);
    std::vector<std::string> helpers;
    for (const auto& name : pure_predicates)
        if (used.count(name)) helpers.push_back(adapt_predicate(name, whole_function(text, "static int " + name + "(")));
    for (const auto& name : transform_generators)
        if (used.count(name)) helpers.push_back(adapt_transform_generator(name, whole_function(text, "static BeamOp* " + name + "(")));
    std::string output;
    for (std::size_t i = 0; i < helpers.size(); ++i) { if (i) output += '\n'; output += helpers[i]; }
    return output + '\n' + result;
}
} // namespace libbeam_build
