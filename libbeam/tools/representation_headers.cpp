// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
// Build-time extraction of exact pinned OTP declarations for independent tests.
#include "source_projection.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

namespace {
std::string read(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot read " + path.string());
    std::string text(std::istreambuf_iterator<char>{input}, {});
    if (input.bad()) throw std::runtime_error("Cannot finish reading " + path.string());
    return text;
}
void write(const std::filesystem::path& path, const std::string& content) {
    std::ofstream output(path, std::ios::binary);
    output << content;
    output.close();
    if (!output) throw std::runtime_error("Cannot write " + path.string());
}
const std::string notice = R"(/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1996-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Exact declarations selected from pinned OTP for flat-64 reference probes.
 * See source manifests under libbeam/core/otp/. Not a capability grant.
 */
)";
}

int main(int argc, char** argv) {
    using namespace libbeam_source;
    try {
        if (argc != 8) throw std::invalid_argument("usage: representation_headers TERM BINARY BITS MAP ERROR CORE_EXCEPTION OUTPUT");
        const auto term = definitions(read(argv[1]));
        const auto binary = read(argv[2]);
        const auto bits_text = read(argv[3]);
        const auto bits = definitions(bits_text);
        const auto maps_text = read(argv[4]);
        const auto maps = definitions(maps_text);
        const auto error = definitions(read(argv[5]));
        const auto owned_exception = definitions(read(argv[6]));
        const std::filesystem::path output(argv[7]);
        std::filesystem::create_directories(output);

        std::string header = notice + R"(#include <stdint.h>
#include <stddef.h>
#include <assert.h>
typedef uintptr_t Eterm; typedef uintptr_t Uint; typedef intptr_t Sint;
#define SWORD_CONSTANT(x) ((Sint)(x))
#define ASSERT(x) assert(x)
#define _ET_APPLY(f,x) _unchecked_##f(x)
)";
        header += macros(term, R"(TAG_PTR_MASK__ TAG_LITERAL_PTR _TAG_PRIMARY_SIZE _TAG_PRIMARY_MASK TAG_PRIMARY_HEADER TAG_PRIMARY_LIST
TAG_PRIMARY_BOXED TAG_PRIMARY_IMMED1 _TAG_IMMED1_SIZE _TAG_IMMED1_MASK
_TAG_IMMED1_IMMED2 _TAG_IMMED1_SMALL _TAG_IMMED2_SIZE _TAG_IMMED2_MASK
_TAG_IMMED2_ATOM _TAG_IMMED2_NIL _TAG_HEADER_MASK _HEADER_ARITY_OFFS
__MAKE_SUBTAG ARITYVAL_SUBTAG FLOAT_SUBTAG POS_BIG_SUBTAG NEG_BIG_SUBTAG REF_SUBTAG
FUN_SUBTAG RECORD_SUBTAG HEAP_BITS_SUBTAG SUB_BITS_SUBTAG BIN_REF_SUBTAG MAP_SUBTAG
EXTERNAL_PID_SUBTAG EXTERNAL_PORT_SUBTAG EXTERNAL_REF_SUBTAG _TAG_HEADER_ARITYVAL _TAG_HEADER_FLOAT
_make_header NIL SMALL_BITS MAX_SMALL MIN_SMALL MAX_ARITYVAL make_small is_small make_atom is_atom
_unchecked_signed_val signed_val _unchecked_atom_val atom_val make_arityval
make_arityval_zero _unchecked_arityval arityval _unchecked_make_boxed make_boxed
_unchecked_ptr_val _unchecked_boxed_val boxed_val _unchecked_make_list make_list
_unchecked_list_val list_val make_tuple TUPLE2 CAR CDR CONS)");
        header += "#ifndef NDEBUG\n" + macros(term, "THE_NON_VALUE") + "#else\n" + macros(term, "THE_NON_VALUE", Branch::last) + "#endif\n";
        write(output / "term_reference.h", header);

        header = notice + R"(#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
typedef uintptr_t Uint, UWord, Eterm; typedef intptr_t SWord;
typedef uint64_t Uint64; typedef _Atomic Uint erts_refc_t;
#define ERTS_BINARY_STRUCT_ALIGNMENT
)";
        header += macros(term, "_TAG_PRIMARY_SIZE TAG_PRIMARY_HEADER _TAG_HEADER_MASK _HEADER_ARITY_OFFS __MAKE_SUBTAG SUB_BITS_SUBTAG BIN_REF_SUBTAG _TAG_HEADER_SUB_BITS _TAG_HEADER_BIN_REF _make_header");
        header += declaration(binary, "enum binary_flags {", "\n};");
        header += declaration(binary, "struct binary_internals {", "\n};");
        header += declaration(binary, "typedef struct binary {", "\n} Binary;");
        header += declaration(bits_text, "typedef struct erl_sub_bits {", "\n} ErlSubBits;");
        header += declaration(bits_text, "typedef struct bin_ref {", "\n} BinRef;");
        header += declaration(bits_text, "typedef struct erl_heap_bits {", "\n} ErlHeapBits;");
        header += macros(bits, "ERL_BIN_REF_SIZE ERL_SUB_BITS_SIZE ERL_REFC_BITS_SIZE HEADER_SUB_BITS HEADER_BIN_REF ERL_ONHEAP_BINARY_LIMIT ERL_ONHEAP_BITS_LIMIT NBYTES heap_bin_size__ heap_bits_size ERL_SUB_BITS_FLAG_MUTABLE ERL_SUB_BITS_FLAG_VOLATILE");
        header += R"(typedef Binary B; typedef BinRef R; typedef ErlSubBits S;
#define REF_HEADER HEADER_BIN_REF
#define SUB_HEADER HEADER_SUB_BITS
#define WRITABLE BIN_FLAG_WRITABLE
#define ACTIVE BIN_FLAG_ACTIVE_WRITER
#define MUTABLE ERL_SUB_BITS_FLAG_MUTABLE
#define VOLATILE ERL_SUB_BITS_FLAG_VOLATILE
#define LIMIT ERL_ONHEAP_BINARY_LIMIT
#define WORDS(bits) ((bits)<=ERL_ONHEAP_BITS_LIMIT ? heap_bits_size(bits) : ERL_REFC_BITS_SIZE)
)";
        write(output / "binary_reference.h", header);

        header = notice + "#include \"term.h\"\ntypedef uint16_t Uint16;\n";
        header += declaration(maps_text, "typedef struct flatmap_s {", "\n} flatmap_t;");
        header += macros(term, "MAP_HEADER_TAG_SZ MAP_HEADER_ARITY_SZ MAP_HEADER_TAG_FLATMAP_HEAD _TAG_HEADER_MAP make_flatmap MAP_HEADER MAP_SZ MAP3_SZ MAP3");
        header += macros(maps, "MAKE_MAP_HEADER MAP_HEADER_FLATMAP MAP_HEADER_FLATMAP_SZ");
        const std::string kinds = "BSC_APPEND BSC_PRIVATE_APPEND BSC_BINARY BSC_BINARY_FIXED_SIZE BSC_BINARY_ALL BSC_FLOAT BSC_FLOAT_FIXED_SIZE BSC_INTEGER BSC_INTEGER_FIXED_SIZE BSC_STRING BSC_UTF8 BSC_UTF16 BSC_UTF32";
        header += macros(bits, kinds);
        header += "typedef flatmap_t M;\n#define WORDS MAP3_SZ\n#define BUILD MAP3\n";
        std::istringstream names(kinds), aliases("K_APPEND K_PRIVATE K_BINARY K_BINARY_FIXED K_ALL K_FLOAT K_FLOAT_FIXED K_INTEGER K_INTEGER_FIXED K_STRING K_UTF8 K_UTF16 K_UTF32");
        std::string native, alias, core_aliases;
        while (names >> native) {
            if (!(aliases >> alias)) throw std::logic_error("Missing descriptor alias");
            header += "#define " + alias + " " + native + '\n';
            core_aliases += "#define " + alias + " LB_" + native + '\n';
        }
        if (aliases >> alias) throw std::logic_error("Unexpected descriptor alias");
        write(output / "flatmap_reference.h", header);
        write(output / "flatmap_core_aliases.h", notice + core_aliases);

        std::string names_to_copy;
        for (const auto& [name, variants] : owned_exception) {
            (void)variants;
            if (name.rfind("LB_", 0) == 0) names_to_copy += name.substr(3) + ' ';
        }
        if (names_to_copy.empty()) throw std::logic_error("Missing exception definitions");
        write(output / "exception_reference.h", notice + "#include \"term.h\"\n" + macros(error, names_to_copy));
        std::string body = R"(#include <stdio.h>
#include <inttypes.h>
#ifdef LB_REFERENCE
#define VALUE(name) name
#else
#define VALUE(name) LB_##name
#endif
int main(void) {
)";
        for (const auto& [name, variants] : owned_exception) {
            if (name.rfind("LB_", 0) == 0 && !variants.front().function_like) {
                const auto native_name = name.substr(3);
                body += "printf(\"" + native_name + "=%\" PRIuPTR \"\\n\", (Uint)VALUE(" + native_name + "));\n";
            }
        }
        body += "Uint hash=0; for(Uint code=0;code<32768;++code) {\n";
        for (const std::string name : {"GET_EXC_CLASS", "GET_EXC_INDEX", "PRIMARY_EXCEPTION", "NATIVE_EXCEPTION"})
            body += "hash=hash*UINT64_C(1099511628211)+VALUE(" + name + ")(code);\n";
        body += "} printf(\"fields=%\" PRIuPTR \"\\n\",hash); return 0; }\n";
        write(output / "exception_probe.inc", notice + body);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
