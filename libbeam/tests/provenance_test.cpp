// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#include "source_support.hpp"
#include "test_process.hpp"
#include <algorithm>
#include <gtest/gtest.h>
#include <map>
#include <nlohmann/json.hpp>

namespace {
using Json = nlohmann::json;
using Path = std::filesystem::path;
using libbeam_test::read_file;
using libbeam_test::runfile;
using libbeam_test::sha256;

Path reference_path(const std::string& relative) {
    Path path(relative);
    if (path.empty() || path.is_absolute()) throw std::invalid_argument("Not a relative reference path");
    for (const auto& part : path) if (part == "..") throw std::invalid_argument("Reference escapes runfiles");
    return Path(runfile(relative));
}

TEST(Provenance, ReferencePathsCannotEscapeRunfiles) {
    for (const auto* invalid : {"", "/etc/passwd", "../file", "beam/../../file"})
        EXPECT_THROW(reference_path(invalid), std::invalid_argument);
    EXPECT_EQ(reference_path("beam/input.c"), Path(runfile("beam/input.c")));
}

TEST(Provenance, RecordsOwnedSourcesAndValidationPolicy) {
    std::istringstream paths(read_file(runfile("libbeam/validation_sources.txt")));
    Json fingerprints = Json::object();
    for (std::string path; std::getline(paths, path);) {
        ASSERT_FALSE(path.empty());
        ASSERT_FALSE(fingerprints.contains(path)) << path;
        fingerprints[path] = sha256(read_file(reference_path(path)));
    }
    for (const char* required : {"libbeam/core/engine.c", "libbeam/include/libbeam/engine.hpp",
                                "libbeam/tests/provenance_test.cpp", "libbeam/core/resource_policy.h",
                                "libbeam/tests/core_batch_admission_test.c", "libbeam/BUILD.bazel", ".bazelrc",
                                "bazel/BUILD.bazel", "bazel/host_tools.bzl"})
        EXPECT_TRUE(fingerprints.contains(required)) << required;
    // This records source identity, not execution coverage for every file.
    // A final Bazel --check_tests_up_to_date rejects invalidated results.
    RecordProperty("validation_source_sha256", fingerprints.dump());
}

TEST(Provenance, AdmittedNativeSourcesHaveReviewedIdentity) {
    const std::pair<const char*, const char*> sources[] = {
        {"beam/erts/emulator/beam/beam_bif_load.c", "2eb70c13c595806337a7f4dc115a612214f7b5d9e5e7e2ba56c1d76afcd91d6d"},
        {"beam/erts/emulator/beam/beam_common.c", "95889d02311752022b0c19bcd3d793d59c4bb28326103ca0d4c3c5dcd2e5d7c5"},
        {"beam/erts/emulator/beam/beam_common.h", "af89a25f71e3246f492045ba8b01d9a1e0839a9ad091722044ced4af7ac9cf44"},
        {"beam/erts/emulator/beam/beam_file.c", "52708b4a8fa136d005d962599914b095e01b7fc311c1748309d3682d4ff56d0d"},
        {"beam/erts/emulator/beam/beam_file.h", "82a4760ac4c3b69d385a26c4c004103ca996960621d5f9bb4f62529e3b4f3718"},
        {"beam/erts/emulator/beam/beam_load.c", "ff51d848b9817243ceb3ccd9edf245697cddbc77aaf75d076b7770461b377c19"},
        {"beam/erts/emulator/beam/beam_transform_helpers.c", "5997b24ba2cd4c143753fb0a8617489573ba27fcfc7ecf36a5aa020e1f0c281c"},
        {"beam/erts/emulator/beam/beam_types.c", "3f1a23fe452b3df23b15c8615ed339b6ae6daf404beb6e228da82113eb4e680f"},
        {"beam/erts/emulator/beam/beam_types.h", "be5967d3d25980ff1b228bb2153e7dc12743aa1a96e2b9f17c1eca4cd1e6a29e"},
        {"beam/erts/emulator/beam/bif.c", "4bb45111028b9e86edf72626dd92c8ef78397dd8906979e64a899bc5ccfd4bbb"},
        {"beam/erts/emulator/beam/bif.tab", "5dee91bd713d8405b165a1d5f0b00cd1359c02e84ef41368d431621abe30a592"},
        {"beam/erts/emulator/beam/big.c", "370b63b16cea64901d1974a72f049cac0954dbf52896adf737665fe2188ef7e4"},
        {"beam/erts/emulator/beam/big.h", "07133b1fb09864bcf905f94335f5cde2600dfef644cffad99c3d1d6b6ba97f47"},
        {"beam/erts/emulator/beam/binary.c", "381e560a653fff056a4edc78774af0f42a7c24c663231c88cbc3c5e32de12f88"},
        {"beam/erts/emulator/beam/code_ix.h", "77b581edc92c2b625f0a85980091d31802fbc55532c31a7023922069c953c56e"},
        {"beam/erts/emulator/beam/copy.c", "3a545dd7dc73c816a70897d07d4c353d27f7c8589938073c87da414f3d43e860"},
        {"beam/erts/emulator/beam/dist.c", "9d28161f36c83260d9ee712a8381d7504fae51b9ab2f7b34c53e7a6195790a01"},
        {"beam/erts/emulator/beam/emu/beam_emu.c", "af0ce4436a999e91f8479c3c04b8d8bf73df8e0e053235d8d441276da934b56e"},
        {"beam/erts/emulator/beam/emu/bif_instrs.tab", "efa0551d5ffa27a197de60fb6759530a00a891031627b58d948442fe0f313aee"},
        {"beam/erts/emulator/beam/emu/bs_instrs.tab", "49f203f8c900e9da964e3c483f186c7562fdb4ddb9bb2fa8a0f9af574c2d3279"},
        {"beam/erts/emulator/beam/emu/emu_load.c", "16725d44035c5f83a17b78b1d325b96d5fa196d5c46804682d988bb733481974"},
        {"beam/erts/emulator/beam/emu/generators.tab", "e2980f167ddb6f30a920c1bd9fe5bda682d0e73b8b744a7abfba4723e7965138"},
        {"beam/erts/emulator/beam/emu/instrs.tab", "6191258ed0710adad9cdfd67779a8680461e695487fa5471594e1001a93d9647"},
        {"beam/erts/emulator/beam/emu/load.h", "5a93650da38972d179e210042fef2410b7cd57a87ff69e11c24b8eb7f7ef2d5b"},
        {"beam/erts/emulator/beam/emu/ops.tab", "d204cbdd007a51df81203211675354274b5fcabd67e8fe65e0f4badf4071e234"},
        {"beam/erts/emulator/beam/emu/predicates.tab", "cce20c6cbb968d8d579cfe1e4f481cb51a891eb20edb71ef4681e523b0e2db0d"},
        {"beam/erts/emulator/beam/erl_arith.c", "c4a4b1aa7c8a7c387d80df6c33b35ec50c943a309ad7e404cca0f4c53d499ef4"},
        {"beam/erts/emulator/beam/erl_bif_guard.c", "6a254bcedaf5f57a01a8cdd6cf94f8140693be99b4735a9283d8bffa4288cc67"},
        {"beam/erts/emulator/beam/erl_bif_info.c", "eb155df161565de32182e84e5375b66b99279dc4562602cfb60d46ada7d87f30"},
        {"beam/erts/emulator/beam/erl_bif_op.c", "74ca827e5188657cf2726a4c8a9da6f4503675714a656041722ce10a066eead3"},
        {"beam/erts/emulator/beam/erl_binary.h", "68a59f8e4567236b8bbc38621aac15a61ed486658cb0a754fcdc378da7d6c0db"},
        {"beam/erts/emulator/beam/erl_bits.c", "2b803014c329c7c2c30e60c0bffda90911985b9ce1703952e7315e28694ccdb6"},
        {"beam/erts/emulator/beam/erl_bits.h", "5a72bb9b9c14690d55ff94912faf3e8f0e9914e155593d2c6aa7474c3ff5bfec"},
        {"beam/erts/emulator/beam/erl_fun.h", "f9d4f9322b2611443c3488f3d1f46c806e81a340bceb1a93b4aefd42613876c4"},
        {"beam/erts/emulator/beam/erl_gc.c", "e420c7b61d0f3df557384a13357ba7b4c9e819ba242feae5a72e5141b97994b4"},
        {"beam/erts/emulator/beam/erl_gc.h", "f1be3b3f40976409563a861e4881a7b2a1c774e6531cc3613f6304953c4fdf2d"},
        {"beam/erts/emulator/beam/erl_map.c", "85bc8e09ec1c45855fb91838694ee14ed8daaa1a663a1a7a5a935a2cc025d698"},
        {"beam/erts/emulator/beam/erl_map.h", "30e3e82727d9ce3031972c7d83d4b27e12f0e0997b93e778d5cd47accd9018b9"},
        {"beam/erts/emulator/beam/erl_md5.c", "c653a4e51ad97c4dd8371379ef973efc1df3bbb53034deffb3037b5ecbbab80c"},
        {"beam/erts/emulator/beam/erl_md5.h", "0e3f2815eccab3bc037086fa3e222a2aa4f6d33399391d3915236537b5ea2afa"},
        {"beam/erts/emulator/beam/erl_message.c", "b35e3f0a62dcd86e02f2249d917c801f8c16dc8d35f810c46581212e7d9bab49"},
        {"beam/erts/emulator/beam/erl_message.h", "ce7b97ef8dcb560430ee5e2a0f5886b34ac62d51444c0d22a2ecef194329bdfd"},
        {"beam/erts/emulator/beam/erl_monitor_link.c", "69db809909d9a2063aa6e4c0cd2204efce0ff04fcea0cc53c7c301b933efbe7e"},
        {"beam/erts/emulator/beam/erl_monitor_link.h", "62f04c707eed5d134b656bb85a6ccbe3c5d7906cfad850522a6bd85ca510dc67"},
        {"beam/erts/emulator/beam/erl_node_tables.c", "fa200fde36b86d52ffa9b09208a701b524094f3475768f7a304c8249800bdc50"},
        {"beam/erts/emulator/beam/erl_proc_sig_queue.c", "8c09013466104034a847e3439df4d31c4d5184d2c46430a3cdb0f86710f2c519"},
        {"beam/erts/emulator/beam/erl_proc_sig_queue.h", "ca6c71cbfd860260cd218f2443d176000ff216bc0f8941ace4a7f3debb485168"},
        {"beam/erts/emulator/beam/erl_process.h", "578f07bbee67870d453714e595354c3e4e0581d426501a6673e61a596e3de957"},
        {"beam/erts/emulator/beam/erl_process_dict.c", "61324ec7c9ec188750dfd310b8da101ed7f93b49cf9fff41a202fd446a120e47"},
        {"beam/erts/emulator/beam/erl_process_dict.h", "a27ab41585833b5e54a52734c21402274ce0ddfa6b96472be1a6cb4e5121aca8"},
        {"beam/erts/emulator/beam/erl_ptab.c", "08743809f78709c07810a678cad30808927b03215c4e2c12ffd4d4b43532629f"},
        {"beam/erts/emulator/beam/erl_ptab.h", "a90826c7e6f32c8c7785b6013cf3771b871e07af0aecc35a3b7cb278465a7a09"},
        {"beam/erts/emulator/beam/erl_record.h", "5182d4032ccbdec489d71da6ea3bc4a0afdf6c5510f3f33c7f8050ced0afb5d8"},
        {"beam/erts/emulator/beam/erl_term.h", "8e016126fb7a1bc1db8d31111601c0bea21d72207211bf91e1f3c127e943b5c2"},
        {"beam/erts/emulator/beam/erl_utils.h", "dad589935f12dacf7004f68588417df30e041b34499a8dc1e6336a0a292e78c4"},
        {"beam/erts/emulator/beam/erl_vm.h", "5fc1308a52c46629307cbe0fa085ca561ba1244b0252de1a59b1a35c53d8d02b"},
        {"beam/erts/emulator/beam/error.h", "2d2c9fe1e0ab30ecd577675be575283a32ed0fc16d5342bf7faaf4fcc0d0c9a3"},
        {"beam/erts/emulator/beam/export.h", "e428ca2f675bfcf121d6300cd91f29a48d1f71deff50a876100b3edb854a9dbf"},
        {"beam/erts/emulator/beam/external.c", "cbfdfcef0f756c468c6f2fc8a23d478a01daeddb3a4db8e240a8590252a4414b"},
        {"beam/erts/emulator/beam/global.h", "328b67831da9a9036eadb79fb8774fb2f67c360ac968d5b5d8740f6d703e081f"},
        {"beam/erts/emulator/beam/hash.h", "9d4523310ac9362320b463cc48cefe28e6f5a46acdacd80229352a8252afbe22"},
        {"beam/erts/emulator/beam/jit/arm/instr_bs.cpp", "efad1d342eab262ee33edbfefb3a9192199ca16d9cb44194c12b68a29558c792"},
        {"beam/erts/emulator/beam/register.c", "604ba0db2a1d0d4063671368738fa8a6d409333f596bf8385e30ce419b376ad9"},
        {"beam/erts/emulator/beam/utils.c", "d899860f79adae3cf1c52a9e644aad3c58bfe5a2242ee9fd61163ae0bed9800f"},
        {"beam/erts/emulator/sys/unix/erl_unix_sys.h", "1d536b01ca03d79bc77ec2b9a8a63f9ea6b9bef5a7c797c9c26796a60e166758"},
        {"beam/erts/preloaded/src/erlang.erl", "96651f6f0663d626dfabae4b4a34abce65aefcbb06e99a6867e3ce7e53ab3a1b"},
        {"beam/lib/compiler/src/beam_validator.erl", "f77ffad2e94636df6475ff7f3651f521cc349ab1c5156523cb69571a313fa5a3"},
    };
    ASSERT_EQ(std::size(sources), 67u);
    for (const auto& [file, expected] : sources) {
        SCOPED_TRACE(file);
        EXPECT_EQ(sha256(read_file(reference_path(file))), expected);
    }
}

TEST(Provenance, OpcodeInputsAndGeneratorMatchPinnedProfile) {
    EXPECT_EQ(sha256(read_file(runfile("libbeam/tools/otp/beam_makeops"))),
              "40f5befb1cb6920e69f8bc4340931b13bfc52345eb0e43b4aebf72eb1c464d65");
    struct Input { const char* original; const char* admitted; const char* digest; const char* profile_digest=nullptr; };
    // These two pinned-source exceptions are libbeam-profile adaptations:
    // route light BIFs through the owned Export trampoline and turn native
    // record tests into an always-failing edge because native records are not admitted.
    const Input inputs[] = {
        {"beam/lib/compiler/src/genop.tab", "libbeam/core/otp/opcodes/genop.tab", "0f999b3797672f28bfbd4007245a95637bd38b68ac67dcfcb95eacce29e0780e"},
        {"beam/erts/emulator/beam/predicates.tab", "libbeam/core/otp/opcodes/predicates.tab", "6aa371b05724f1c762093d8555ed64b2711f98143571c9455b81fa7c578183f2"},
        {"beam/erts/emulator/beam/generators.tab", "libbeam/core/otp/opcodes/generators.tab", "cddd948034d8cf1d07b59c34885d4766cd7d929aae79ebb776909ec1b735758a"},
        {"beam/erts/emulator/beam/emu/ops.tab", "libbeam/core/otp/opcodes/emu/ops.tab", "d204cbdd007a51df81203211675354274b5fcabd67e8fe65e0f4badf4071e234", "9a44048355fe9586eb3111453c32f12aa9bff68835b8a6c0a9ea76ec786ca46d"},
        {"beam/erts/emulator/beam/emu/predicates.tab", "libbeam/core/otp/opcodes/emu/predicates.tab", "cce20c6cbb968d8d579cfe1e4f481cb51a891eb20edb71ef4681e523b0e2db0d", "7d66baddcb010a13daa1515e74c417d9019b7a8a54121033a93ebc3ba07cafd1"},
        {"beam/erts/emulator/beam/emu/generators.tab", "libbeam/core/otp/opcodes/emu/generators.tab", "e2980f167ddb6f30a920c1bd9fe5bda682d0e73b8b744a7abfba4723e7965138", "bbcbf91aaed1c54f0ba2dfb08636482c27c5f13dfa1d585a8b1dce81d3fc3155"},
        {"beam/erts/emulator/beam/emu/macros.tab", "libbeam/core/otp/opcodes/emu/macros.tab", "7cdce7719cde9115a88c8a4dd0299d4dbda31be2fb40c6eea5f045d293d212c4"},
        {"beam/erts/emulator/beam/emu/instrs.tab", "libbeam/core/otp/opcodes/emu/instrs.tab", "6191258ed0710adad9cdfd67779a8680461e695487fa5471594e1001a93d9647", "aefa41989ca5b12c0d1040767738046f5a91f5d5fbb25ab7cea20c9a797b2020"},
        {"beam/erts/emulator/beam/emu/arith_instrs.tab", "libbeam/core/otp/opcodes/emu/arith_instrs.tab", "cd590350432e121b374141c9a8df73b6862e5ec5c8a291701643018606b2f5b2"},
        {"beam/erts/emulator/beam/emu/bif_instrs.tab", "libbeam/core/otp/opcodes/emu/bif_instrs.tab", "efa0551d5ffa27a197de60fb6759530a00a891031627b58d948442fe0f313aee"},
        {"beam/erts/emulator/beam/emu/bs_instrs.tab", "libbeam/core/otp/opcodes/emu/bs_instrs.tab", "49f203f8c900e9da964e3c483f186c7562fdb4ddb9bb2fa8a0f9af574c2d3279"},
        {"beam/erts/emulator/beam/emu/float_instrs.tab", "libbeam/core/otp/opcodes/emu/float_instrs.tab", "47ce63c83e9a870583e350571e055bb804842c274a78af6ca09c1ce926d7a085"},
        {"beam/erts/emulator/beam/emu/map_instrs.tab", "libbeam/core/otp/opcodes/emu/map_instrs.tab", "7caa5a8ef0f03b4be7f1a9c9ea6a5d461abe615f4c94bebf7da53acedff56684"},
        {"beam/erts/emulator/beam/emu/msg_instrs.tab", "libbeam/core/otp/opcodes/emu/msg_instrs.tab", "3ff33000c76eb4598340289478f65a62538b9f9a0adb1deb2321d3c8d6d46ac4"},
        {"beam/erts/emulator/beam/emu/select_instrs.tab", "libbeam/core/otp/opcodes/emu/select_instrs.tab", "dbc20d1ea53a7c5f87931c5ee0f283738489c5220e79d2472cf84368f3295bd4"},
        {"beam/erts/emulator/beam/emu/trace_instrs.tab", "libbeam/core/otp/opcodes/emu/trace_instrs.tab", "fa49760f1c1e8d3ff68eb83127a2d02df1d6d8210cdc27ba822e67ac7106d9fb"},
    };
    ASSERT_EQ(std::size(inputs), 16u);
    for (const auto& input : inputs) {
        EXPECT_EQ(sha256(read_file(reference_path(input.original))), input.digest) << input.original;
        EXPECT_EQ(sha256(read_file(reference_path(input.admitted))),
                  input.profile_digest ? input.profile_digest : input.digest) << input.admitted;
    }
    const auto build = read_file(runfile("bazel/BUILD.bazel"));
    for (const char* target : {"opcode_upstream", "opcode_upstream_repeat"}) {
        const auto begin = build.find(std::string("name = \"") + target + "\"");
        ASSERT_NE(begin, std::string::npos) << target;
        const auto end = build.find("\n)", begin);
        ASSERT_NE(end, std::string::npos);
        const auto rule = build.substr(begin, end - begin);
        EXPECT_NE(rule.find("-wordsize 64 -jit no -DUSE_VM_PROBES=0"), std::string::npos) << target;
    }
}

TEST(Provenance, IndependentNumericVectorsAreNotRuntimeAnswers) {
    struct Golden { const char* name; size_t rows; const char* digest; };
    for (const auto& golden : {
        Golden{"float-vectors.txt", 3979, "a979f14eb8661adf92580663bd5656b6bad5015f94a015bc25c623d45e2d1e2c"},
        Golden{"div_vectors.txt", 363, "b39727bf99b90a5619fe1ab4676ede5fa05bb5b9e86b8cb9e7cf8e5a4fb633a5"},
    }) {
        auto bytes = read_file(runfile(std::string("libbeam/tests/fixtures/") + golden.name));
        EXPECT_EQ(static_cast<size_t>(std::count(bytes.begin(), bytes.end(), '\n')), golden.rows) << golden.name;
        EXPECT_EQ(sha256(bytes), golden.digest) << golden.name;
    }
}
}  // namespace
