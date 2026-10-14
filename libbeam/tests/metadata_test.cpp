// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#include "beam_image.h"
#include "source_support.hpp"
#include "test_process.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <gtest/gtest.h>
#include <iostream>
#include <sstream>

namespace {
using libbeam_test::read_file;
using libbeam_test::run;
using libbeam_test::runfile;
std::string erl, escript;

std::string fixture(const std::string& module) {
    return runfile("libbeam/fixtures/" + module + "_fixture/" + module + ".beam");
}
std::vector<std::string> selected_lines(const std::string& text, const std::vector<std::string>& prefixes) {
    std::istringstream input(text);
    std::vector<std::string> result;
    for (std::string line; std::getline(input, line);)
        for (const auto& prefix : prefixes) if (line.rfind(prefix, 0) == 0) { result.push_back(line); break; }
    std::sort(result.begin(), result.end());
    return result;
}
std::string hex(LbBeamBytes bytes) {
    constexpr char digits[] = "0123456789ABCDEF";
    std::string result;
    for (size_t i = 0; i < bytes.size; ++i) {
        result.push_back(digits[bytes.data[i] >> 4]);
        result.push_back(digits[bytes.data[i] & 15]);
    }
    return result;
}
uint32_t big_endian_word(const std::string& bytes, size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 4) throw std::invalid_argument("Truncated BEAM word");
    uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i) value = (value << 8) | static_cast<unsigned char>(bytes[offset + i]);
    return value;
}
libbeam_test::ProcessResult metadata_reference() {
    std::vector<std::string> args{runfile("libbeam/tests/fixtures/metadata_reference.escript"), fixture("first_slice")};
    for (const std::string module : {"first_slice", "code_large", "local_slice", "compare_slice", "decimal_slice", "add_slice", "shape_slice", "binary_slice"})
        args.push_back(std::filesystem::path(fixture(module)).parent_path().string());
    return run(escript, args);
}

class BeamMetadata : public testing::Test {
protected:
    LbAllocDomain* domain = nullptr;
    LbBeamImage* image = nullptr;
    std::string bytes;
    void SetUp() override {
        bytes = read_file(fixture("first_slice"));
        ASSERT_EQ(lb_alloc_domain_create(nullptr, &domain), LB_ALLOC_OK);
        ASSERT_EQ(lb_beam_image_create(domain, bytes.data(), bytes.size(), &image), LB_BEAM_OK);
    }
    void TearDown() override {
        if (image) lb_beam_image_destroy(image);
        if (domain) {
            EXPECT_EQ(lb_alloc_domain_usage(domain).current, 0u);
            EXPECT_EQ(lb_alloc_domain_destroy(domain), LB_ALLOC_OK);
        }
    }
    std::string atom(uint32_t index) {
        LbBeamBytes value;
        if (lb_beam_image_atom(image, index, &value) != LB_BEAM_OK) throw std::runtime_error("Invalid file-local atom index");
        return hex(value);
    }
};

TEST_F(BeamMetadata, FileLocalNamesMatchIndependentOtpReference) {
    const auto reference = metadata_reference();
    ASSERT_TRUE(reference.exited(0)) << reference.output;
    const auto* info = lb_beam_image_info(image);
    ASSERT_NE(info, nullptr);
    std::vector<std::string> actual;
    for (uint32_t i = 1; i <= info->atom_count; ++i) actual.push_back("ATOM " + std::to_string(i) + " " + atom(i));
    for (uint32_t i = 0; i < info->import_count; ++i) {
        LbBeamImport value;
        ASSERT_EQ(lb_beam_image_import(image, i, &value), LB_BEAM_OK);
        actual.push_back("IMPORT_NAME " + atom(value.module) + " " + atom(value.function) + " " + std::to_string(value.arity));
    }
    for (uint32_t i = 0; i < info->export_count; ++i) {
        LbBeamExport value;
        ASSERT_EQ(lb_beam_image_export(image, i, &value), LB_BEAM_OK);
        actual.push_back("EXPORT_NAME " + atom(value.atom) + " " + std::to_string(value.arity));
    }
    std::sort(actual.begin(), actual.end());
    const auto expected = selected_lines(reference.standard_output, {"ATOM ", "IMPORT_NAME ", "EXPORT_NAME "});
    ASSERT_FALSE(expected.empty());
    EXPECT_EQ(actual, expected);
    EXPECT_NE(reference.standard_output.find("REFERENCE_SEMANTICS_OK not_libbeam_execution=true"), std::string::npos);
    EXPECT_NE(reference.standard_output.find("REFERENCE_BINARY_OK not_libbeam_execution=true"), std::string::npos);
}

TEST_F(BeamMetadata, CodeHeaderMatchesOriginalFileBytes) {
    const auto* info = lb_beam_image_info(image);
    bool found = false;
    for (size_t offset = 12; offset < bytes.size();) {
        ASSERT_GE(bytes.size() - offset, 8u);
        const auto size = static_cast<size_t>(big_endian_word(bytes, offset + 4));
        ASSERT_LE(size, bytes.size() - offset - 8);
        if (bytes.compare(offset, 4, "Code") == 0) {
            ASSERT_FALSE(found);
            found = true;
            ASSERT_GE(size, 20u);
            const auto header = static_cast<size_t>(big_endian_word(bytes, offset + 8));
            ASSERT_GE(header, 16u);
            ASSERT_LE(header, size - 4);
            EXPECT_EQ(big_endian_word(bytes, offset + 12), 0u);
            EXPECT_EQ(info->max_opcode, big_endian_word(bytes, offset + 16));
            EXPECT_EQ(info->label_count, big_endian_word(bytes, offset + 20));
            EXPECT_EQ(info->function_count, big_endian_word(bytes, offset + 24));
            EXPECT_EQ(info->code.size, size - 4 - header);
            EXPECT_EQ(std::string(reinterpret_cast<const char*>(info->code.data), info->code.size), bytes.substr(offset + 12 + header, size - 4 - header));
        }
        offset += 8 + ((size + 3) & ~size_t{3});
    }
    EXPECT_TRUE(found);
}

TEST(MetadataExecution, ActualModuleInfoMd5MatchesIndependentOtp) {
    const auto execution = run(runfile("libbeam/core_code_test"), {fixture("first_slice"), fixture("code_peer")});
    const auto reference = metadata_reference();
    ASSERT_TRUE(execution.exited(0)) << execution.output;
    ASSERT_TRUE(reference.exited(0)) << reference.output;
    const auto native_rows = selected_lines(execution.standard_output, {"CORE_MODULE_MD5 "});
    const auto reference_rows = selected_lines(reference.standard_output, {"REFERENCE_MODULE_MD5 "});
    ASSERT_EQ(native_rows.size(), 1u);
    ASSERT_EQ(reference_rows.size(), 1u);
    auto expected = reference_rows[0].substr(std::string("REFERENCE_MODULE_MD5 ").size());
    std::transform(expected.begin(), expected.end(), expected.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    EXPECT_EQ(native_rows[0].substr(std::string("CORE_MODULE_MD5 ").size()), expected);
    EXPECT_NE(execution.standard_output.find("CORE_CODE_EXECUTION_OK"), std::string::npos);
    EXPECT_NE(execution.standard_output.find("CORE_LINKED_CODE_OK"), std::string::npos);
}

TEST(MetadataDecoding, FunctionBoundariesMatchOtpDisassembly) {
    for (const auto& beam : {fixture("first_slice"), fixture("binary_slice"),
                            runfile("libbeam/fixtures/probe_a_fixture/probe.beam"), runfile("libbeam/fixtures/probe_b_fixture/probe.beam")}) {
        SCOPED_TRACE(beam);
        const auto native = run(runfile("libbeam/core_beam_program_test"), {beam});
        const auto reference = run(erl, {"-noshell", "-noinput", "-eval",
            // Passing bytes avoids beam_lib atomizing long sandbox filenames
            // while looking up absent optional chunks (atoms are limited to 255 characters).
            "[Path]=init:get_plain_arguments(),{ok,Beam}=file:read_file(Path),{beam_file,_,_,_,_,Fs}=beam_disasm:file(Beam),"
            "lists:foreach(fun({function,F,A,_,_})->io:format(\"FUNCTION ~ts/~B~n\",[atom_to_binary(F,utf8),A]) end,Fs),halt().",
            "-extra", beam});
        ASSERT_TRUE(native.exited(0)) << native.output;
        ASSERT_TRUE(reference.exited(0)) << reference.output;
        const auto expected = selected_lines(reference.standard_output, {"FUNCTION "});
        ASSERT_FALSE(expected.empty());
        EXPECT_EQ(selected_lines(native.standard_output, {"FUNCTION "}), expected);
    }
}
}  // namespace

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    if (argc != 3) { std::cerr << "Expected declared erl and escript runfiles\n"; return 2; }
    erl = runfile(argv[1]); escript = runfile(argv[2]);
    return RUN_ALL_TESTS();
}
