// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
#include <libbeam/engine.hpp>
#include "test_process.hpp"
#include <gtest/gtest.h>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <variant>

namespace {
using namespace libbeam;
std::string beam_path;

std::uint32_t read_u32(const Bytes& bytes, std::size_t offset)
{
    return (static_cast<std::uint32_t>(bytes[offset]) << 24) |
           (static_cast<std::uint32_t>(bytes[offset+1]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset+2]) << 8) |
           static_cast<std::uint32_t>(bytes[offset+3]);
}
void write_u32(Bytes& bytes, std::size_t offset, std::uint32_t value)
{
    bytes[offset]=static_cast<std::uint8_t>(value >> 24);
    bytes[offset+1]=static_cast<std::uint8_t>(value >> 16);
    bytes[offset+2]=static_cast<std::uint8_t>(value >> 8);
    bytes[offset+3]=static_cast<std::uint8_t>(value);
}

Bytes without_fun_table(Bytes bytes)
{
    if(bytes.size()<12 || std::string(bytes.begin(),bytes.begin()+4)!="FOR1" ||
       std::string(bytes.begin()+8,bytes.begin()+12)!="BEAM") return {};
    std::size_t offset=12;
    while(offset+8<=bytes.size()) {
        const std::uint32_t chunk_size=read_u32(bytes,offset+4);
        const std::size_t padding=(4u-(chunk_size&3u))&3u;
        const std::size_t padded_size=static_cast<std::size_t>(chunk_size)+padding;
        if(padded_size>bytes.size()-offset-8) return {};
        if(std::string(bytes.begin()+offset,bytes.begin()+offset+4)=="FunT") {
            bytes.erase(bytes.begin()+offset,bytes.begin()+offset+8+padded_size);
            if(bytes.size()-8>UINT32_MAX) return {};
            write_u32(bytes,4,static_cast<std::uint32_t>(bytes.size()-8));
            return bytes;
        }
        offset+=8+padded_size;
    }
    return {};
}

TEST(LambdaErrorOpcode, MissingFunEntryRejectsModuleAtLoad)
{
    std::ifstream input(beam_path,std::ios::binary);
    ASSERT_TRUE(input.good()) << beam_path;
    const Bytes valid_beam{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
    const Bytes malformed_beam=without_fun_table(valid_beam);
    ASSERT_FALSE(malformed_beam.empty()) << "fixture must contain a FunT chunk";

    auto engine_result=Engine::create();
    ASSERT_TRUE(std::holds_alternative<Engine>(engine_result));
    Engine engine=std::move(std::get<Engine>(engine_result));
    {
        auto isolate_result=engine.create_isolate();
        ASSERT_TRUE(std::holds_alternative<Isolate>(isolate_result));
        Isolate isolate=std::move(std::get<Isolate>(isolate_result));
        auto error=isolate.load_module({malformed_beam.data(),malformed_beam.size()});
        ASSERT_TRUE(error.has_value());
        EXPECT_FALSE(error->message.empty());

        EXPECT_FALSE(isolate.load_module({valid_beam.data(),valid_beam.size()}));
        auto reclamation_result=isolate.stop();
        ASSERT_TRUE(std::holds_alternative<Reclamation>(reclamation_result));
        auto reclamation=std::move(std::get<Reclamation>(reclamation_result));
        EXPECT_FALSE(reclamation.wait_until(std::chrono::steady_clock::now()+std::chrono::seconds(5)));
    }
    EXPECT_FALSE(engine.shutdown(std::chrono::steady_clock::now()+std::chrono::seconds(5)));
}
} // namespace

int main(int argc,char** argv)
{
    testing::InitGoogleTest(&argc,argv);
    if(argc!=2) return 2;
    beam_path=libbeam_test::runfile(argv[1]);
    return RUN_ALL_TESTS();
}
