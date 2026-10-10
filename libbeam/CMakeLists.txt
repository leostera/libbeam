# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Leandro Ostera <leandro@ostera.io>
cmake_minimum_required(VERSION 3.20)
project(libbeam LANGUAGES C CXX)

# RFD 0003 additive C core. Component-only until real execution infrastructure
# can back the public factory; never falls back to the diagnostic ERTS archive.
add_library(libbeam_core STATIC core/alloc.c core/beam_image.c)
target_include_directories(libbeam_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core)
target_compile_features(libbeam_core PUBLIC c_std_11)
set_target_properties(libbeam_core PROPERTIES C_EXTENSIONS OFF)

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

# Red acceptance target until real unbooted teardown/restart exists. Deliberately
# not registered as an expected-failure CTest.
add_executable(engine_lifecycle examples/engine_lifecycle.cpp)
target_link_libraries(engine_lifecycle PRIVATE libbeam::libbeam)

add_executable(two_isolates examples/two_isolates.cpp)
target_link_libraries(two_isolates PRIVATE libbeam::libbeam)

include(CTest)
if(BUILD_TESTING)
    add_executable(core_alloc_test tests/core_alloc_test.c)
    target_link_libraries(core_alloc_test PRIVATE libbeam_core)
    add_test(NAME core_allocation_lifetime COMMAND core_alloc_test)
    add_executable(core_beam_image_test tests/core_beam_image_test.c)
    target_link_libraries(core_beam_image_test PRIVATE libbeam_core)
    add_test(NAME core_beam_image_lifetime COMMAND core_beam_image_test)
    set_tests_properties(core_beam_image_lifetime PROPERTIES TIMEOUT 30)
    add_executable(api_scaffold_test tests/api_scaffold_test.cpp)
    target_link_libraries(api_scaffold_test PRIVATE libbeam::libbeam)
    if(LIBBEAM_ERTS_PACKAGE)
        target_link_libraries(api_scaffold_test PRIVATE libbeam_erts)
        target_compile_definitions(api_scaffold_test PRIVATE LIBBEAM_TEST_NATIVE=1)
        add_executable(native_engine_control_test tests/native_engine_control_test.cpp)
        target_link_libraries(native_engine_control_test PRIVATE libbeam_erts)
        add_test(NAME native_engine_control_lifetime COMMAND native_engine_control_test)
        # Internal component test against this checkout's header, not a new SDK.
        add_executable(native_module_table_test tests/native_module_table_test.cpp)
        target_include_directories(native_module_table_test PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/../beam/erts/emulator/beam)
        target_link_libraries(native_module_table_test PRIVATE libbeam_erts)
        target_compile_features(native_module_table_test PRIVATE cxx_std_17)
        add_test(NAME native_owned_module_tables COMMAND native_module_table_test
            -S 2:2 -SDcpu 1:1 -SDio 1 -A 0 -- -root /libbeam-missing-root --
            -boot /libbeam-missing-boot)
        set_tests_properties(native_owned_module_tables PROPERTIES TIMEOUT 20)
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
