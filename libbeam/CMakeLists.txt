# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Leandro Ostera <leandro@ostera.io>
cmake_minimum_required(VERSION 3.20)
project(libbeam LANGUAGES CXX)

# The optional package is a frozen, hash-checked native build artifact, NOT an
# embedded/standalone runtime mode. Without it, only API-scaffold tests can run.
add_library(libbeam STATIC src/engine.cpp)
add_library(libbeam::libbeam ALIAS libbeam)
target_include_directories(libbeam PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(libbeam PUBLIC cxx_std_17)
set_target_properties(libbeam PROPERTIES CXX_EXTENSIONS OFF OUTPUT_NAME beam)

set(LIBBEAM_ERTS_PACKAGE "" CACHE FILEPATH "runtime-package.cmake exported by export_runtime_package.py")
if(LIBBEAM_ERTS_PACKAGE)
    include("${LIBBEAM_ERTS_PACKAGE}")
    if(NOT TARGET libbeam_erts)
        message(FATAL_ERROR "Runtime package must define libbeam_erts")
    endif()
    target_link_libraries(libbeam PRIVATE libbeam_erts)
    target_compile_definitions(libbeam PRIVATE LIBBEAM_LINKED_ERTS=1)
    # Explicit process-lifetime diagnostic, not an Engine/Isolate acceptance test.
    add_executable(engine_start_probe examples/engine_start_probe.cpp)
    target_link_libraries(engine_start_probe PRIVATE libbeam::libbeam libbeam_erts)
    target_compile_definitions(engine_start_probe PRIVATE LIBBEAM_PROBE_CPP_PREFLIGHT=1)
    add_executable(engine_prepare_probe examples/engine_prepare_probe.cpp)
    target_link_libraries(engine_prepare_probe PRIVATE libbeam::libbeam libbeam_erts)
endif()

add_executable(two_isolates examples/two_isolates.cpp)
target_link_libraries(two_isolates PRIVATE libbeam::libbeam)

include(CTest)
if(BUILD_TESTING)
    add_executable(api_scaffold_test tests/api_scaffold_test.cpp)
    target_link_libraries(api_scaffold_test PRIVATE libbeam::libbeam)
    if(LIBBEAM_ERTS_PACKAGE)
        target_link_libraries(api_scaffold_test PRIVATE libbeam_erts)
        target_compile_definitions(api_scaffold_test PRIVATE LIBBEAM_TEST_NATIVE=1)
        add_executable(native_engine_control_test tests/native_engine_control_test.cpp)
        target_link_libraries(native_engine_control_test PRIVATE libbeam_erts)
        add_test(NAME native_engine_control_lifetime COMMAND native_engine_control_test)
        foreach(workers IN ITEMS 0 1 4)
            add_test(NAME native_prepare_without_otp_async_${workers}
                COMMAND engine_prepare_probe -S 2:2 -SDcpu 1:1 -SDio 1 -A ${workers}
                -i libbeam_boot_must_not_run -- -root /libbeam-missing-root --
                -boot /libbeam-missing-boot -s libbeam_boot_must_not_run run)
            set_tests_properties(native_prepare_without_otp_async_${workers} PROPERTIES
                TIMEOUT 20)
        endforeach()
    endif()
    add_test(NAME api_scaffold_reports_missing_engine COMMAND api_scaffold_test)
endif()
