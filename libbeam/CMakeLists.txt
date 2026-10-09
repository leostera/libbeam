# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Leandro Ostera <leandro@ostera.io>
cmake_minimum_required(VERSION 3.20)
project(libbeam LANGUAGES CXX)

# Initial API scaffold only. Deliberately no OTP build, subprocess VM, or mock VM.
# Native archive integration will be added when Engine owns actual runtime state.
add_library(libbeam STATIC src/engine.cpp)
add_library(libbeam::libbeam ALIAS libbeam)
target_include_directories(libbeam PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(libbeam PUBLIC cxx_std_17)
set_target_properties(libbeam PROPERTIES CXX_EXTENSIONS OFF OUTPUT_NAME beam)

add_executable(two_isolates examples/two_isolates.cpp)
target_link_libraries(two_isolates PRIVATE libbeam::libbeam)

include(CTest)
if(BUILD_TESTING)
    add_executable(api_scaffold_test tests/api_scaffold_test.cpp)
    target_link_libraries(api_scaffold_test PRIVATE libbeam::libbeam)
    add_test(NAME api_scaffold_reports_missing_engine COMMAND api_scaffold_test)
endif()
